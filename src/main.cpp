#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

extern "C" int  fable_main(int argc, char **argv);
extern "C" int  tui_wifi_load(char *ssid, int ssid_len, char *pass, int pass_len);
extern "C" void set_boot_wifi_ssid(const char *s);

/* POSIX timezone rule used for local time. Default = UK (GMT/BST).
 * Examples: "UTC0"  "CET-1CEST,M3.5.0,M10.5.0/3"  "EST5EDT,M3.2.0,M11.1.0" */
#define FABLE_TZ "GMT0BST,M3.5.0/1,M10.5.0"

#define WIFI_TRIES        2
#define WIFI_TIMEOUT_MS   12000
#define NTP_TIMEOUT_MS    10000

static void log_heap(const char *tag)
{
    Serial.printf("HEAP %-14s internal free=%u largest=%u | dma free=%u largest=%u\n", tag,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}

static bool wifi_connect(const char *ssid, const char *pass)
{
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(200);
    WiFi.begin(ssid, pass);

    unsigned long t0 = millis();
    int last = -1;
    while (millis() - t0 < WIFI_TIMEOUT_MS) {
        int s = WiFi.status();
        if (s != last) {
            Serial.printf("Wi-Fi: t=%lums status=%d\n", millis() - t0, s);
            last = s;
        }
        if (s == WL_CONNECTED) return true;
        delay(100);
    }
    Serial.printf("Wi-Fi: connect failed (final status=%d)\n", WiFi.status());
    return false;
}

/* Connects, syncs the system clock over NTP, then shuts Wi-Fi down.
 * The ESP32 system clock keeps running afterwards, so the editor just
 * reads time() - no boot string, no millis arithmetic. */
static void fetch_time_at_boot(void)
{
    char ssid[33], pass[65];

    if (!tui_wifi_load(ssid, sizeof ssid, pass, sizeof pass)) {
        Serial.println("Wi-Fi: no saved credentials");
        return;
    }
    Serial.printf("Wi-Fi: loaded ssid='%s' pass_len=%d\n",
                  ssid, (int)strlen(pass));

    bool synced = false;
    for (int attempt = 1; attempt <= WIFI_TRIES && !synced; attempt++) {
        Serial.printf("Wi-Fi: attempt %d/%d\n", attempt, WIFI_TRIES);
        if (!wifi_connect(ssid, pass)) continue;

        Serial.printf("Wi-Fi: connected, IP=%s\n",
                      WiFi.localIP().toString().c_str());
        set_boot_wifi_ssid(ssid);

        configTzTime(FABLE_TZ, "pool.ntp.org", "time.google.com");

        struct tm tm;
        if (getLocalTime(&tm, NTP_TIMEOUT_MS)) {
            Serial.printf("Wi-Fi: NTP time = %04d-%02d-%02d %02d:%02d\n",
                          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                          tm.tm_hour, tm.tm_min);
            synced = true;
        } else {
            Serial.println("Wi-Fi: NTP fetch failed");
        }
    }

    /* Arduino's WiFi.mode(WIFI_OFF) already stops and deinitialises the
     * driver; calling esp_wifi_stop()/esp_wifi_deinit() again is redundant. */
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(200);
    Serial.println("Wi-Fi: shut down, starting VGA");
}

void setup() {
    Serial.begin(115200);
    delay(500);

    if (psramFound()) {
        Serial.printf("PSRAM: %u bytes total, %u free\n",
                      ESP.getPsramSize(), ESP.getFreePsram());
    } else {
        Serial.println("No PSRAM detected");
    }

    /* Wi-Fi connect + NTP BEFORE VGA (frees RAM before framebuffer alloc) */
    log_heap("before wifi");
#ifndef FABLE_NO_WIFI_BOOT      /* build with -DFABLE_NO_WIFI_BOOT to test without Wi-Fi */
    fetch_time_at_boot();
#endif
    log_heap("after wifi");

    /* SD card */
    SPI.begin(14, 2, 12, 13);
    if (SD.begin(13)) {
        Serial.println("SD mounted");
        if (!SD.exists("/diary")) SD.mkdir("/diary");   /* calendar saves here */
    } else {
        Serial.println("SD mount failed");
    }

    log_heap("before vga");
    char *argv[] = { (char*)"fable", nullptr };
    fable_main(1, argv);
}

void loop() { }
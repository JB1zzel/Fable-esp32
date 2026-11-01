#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>
#include <Preferences.h>

extern "C" int  fable_main(int argc, char **argv);
extern "C" int  tui_wifi_load(char *ssid, int ssid_len, char *pass, int pass_len);
extern "C" void set_boot_time(const char *s);
extern "C" void set_boot_wifi_ssid(const char *s);

static void fetch_time_at_boot(void)
{
    char ssid[33], pass[65];

    if (!tui_wifi_load(ssid, sizeof ssid, pass, sizeof pass)) {
        Serial.println("Wi-Fi: no saved credentials");
        return;
    }

    Serial.printf("Wi-Fi: loaded ssid='%s' pass_len=%d\n",
                  ssid, (int)strlen(pass));

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(200);

    WiFi.begin(ssid, pass);

    unsigned long t0 = millis();
    int last = -1;
    while (millis() - t0 < 8000) {
        int s = WiFi.status();
        if (s != last) {
            Serial.printf("Wi-Fi: t=%lums status=%d\n", millis() - t0, s);
            last = s;
        }
        if (s == WL_CONNECTED) break;
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("Wi-Fi: connect failed (final status=%d)\n", WiFi.status());
        WiFi.mode(WIFI_OFF);
        return;
    }

    Serial.printf("Wi-Fi: connected, IP=%s\n",
                  WiFi.localIP().toString().c_str());
    set_boot_wifi_ssid(ssid);

    configTime(0, 0, "pool.ntp.org");

    struct tm tm;
    if (getLocalTime(&tm, 5000)) {
        char buf[32];
        snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min);
        set_boot_time(buf);
        Serial.printf("Wi-Fi: NTP time = %s\n", buf);
    } else {
        Serial.println("Wi-Fi: NTP fetch failed");
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(500);
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
    fetch_time_at_boot();

    /* SD card */
    SPI.begin(14, 2, 12, 13);
    if (SD.begin(13)) {
        Serial.println("SD mounted");
    } else {
        Serial.println("SD mount failed");
    }

    char *argv[] = { (char*)"fable", nullptr };
    fable_main(1, argv);
}

void loop() { }
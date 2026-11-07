/* tui_fabgl.cpp - FabGL backend for Fable on TTGO VGA32 */

#ifdef FABLE_ESP32   /* ESP32 build only (set by platformio.ini) */
#include <fabgl.h>
#include <Arduino.h>
#include "tui.h"
#include "file_fabgl.h"
#include <string.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

static fabgl::VGA16Controller VGAController;   /* was fabgl::VGAController */
//static fabgl::VGAController       VGAController;
static fabgl::Terminal            Terminal;
static fabgl::Keyboard            Keyboard;
static fabgl::PS2Controller       PS2Controller;
static fabgl::TerminalController *termctrl = nullptr;

static char linebuf[81];
static int  linepos = 0;

static char *frame_buf = NULL;
static int   frame_len = 0;
static int   frame_cap = 0;


/* Map TUI colour index (0-15) to FabGL Color enum */
static fabgl::Color tui_to_fabgl(int idx)
{
    switch (idx) {
    case 0:  return fabgl::Color::Black;
    case 1:  return fabgl::Color::Blue;
    case 2:  return fabgl::Color::Green;
    case 3:  return fabgl::Color::Cyan;
    case 4:  return fabgl::Color::Red;
    case 5:  return fabgl::Color::Magenta;
    case 6:  return fabgl::Color::Yellow;
    case 7:  return fabgl::Color::White;
    case 8:  return fabgl::Color::BrightBlack;
    case 9:  return fabgl::Color::BrightBlue;
    case 10: return fabgl::Color::BrightGreen;
    case 11: return fabgl::Color::BrightCyan;
    case 12: return fabgl::Color::BrightRed;
    case 13: return fabgl::Color::BrightMagenta;
    case 14: return fabgl::Color::BrightYellow;
    case 15: return fabgl::Color::BrightWhite;
    default: return fabgl::Color::White;
    }
}

extern "C" {
    //snake
void tui_delay(int ms)
{
    delay(ms);
}
/* Scan for networks. Fills names[] with up to max SSIDs.
 * Returns count, or -1 if Wi-Fi failed to start. */

/* Scan for networks using the ESP-IDF low-level API.
 * Fills names[] with up to max de-duplicated SSIDs.
 * Returns count (>= 0), or -1 on hard failure. */

 unsigned long tui_millis(void) { return millis(); }
int tui_wifi_scan(char names[][33], int max)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_scan_config_t scan_cfg = {0};
    uint16_t ap_count = 0;
    wifi_ap_record_t *ap_list = NULL;
    int out = 0, i, j;

    /* hard reset the wifi driver */
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    vTaskDelay(pdMS_TO_TICKS(500));

    /* re-init in station mode */
    if (esp_wifi_init(&cfg) != ESP_OK)             return -1;
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        esp_wifi_deinit();
        return -1;
    }
    if (esp_wifi_start() != ESP_OK) {
        esp_wifi_deinit();
        return -1;
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    /* blocking scan */
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        esp_wifi_stop();
        esp_wifi_deinit();
        return -1;
    }
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        esp_wifi_stop();
        esp_wifi_deinit();
        return 0;
    }

    ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_list) {
        esp_wifi_stop();
        esp_wifi_deinit();
        return -1;
    }
    if (esp_wifi_scan_get_ap_records(&ap_count, ap_list) != ESP_OK) {
        free(ap_list);
        esp_wifi_stop();
        esp_wifi_deinit();
        return -1;
    }

    /* de-duplicate by SSID */
    for (i = 0; i < ap_count && out < max; i++) {
        const char *ssid = (const char *)ap_list[i].ssid;
        int dup = 0;
        if (!ssid || !ssid[0]) continue;
        for (j = 0; j < out; j++)
            if (strcmp(names[j], ssid) == 0) { dup = 1; break; }
        if (dup) continue;
        strncpy(names[out], ssid, 32);
        names[out][32] = '\0';
        out++;
    }
    free(ap_list);

    esp_wifi_stop();
    esp_wifi_deinit();

    return out;
}

/* Save SSID + password to NVS. Returns 1 on success. */
int tui_wifi_save(const char *ssid, const char *pass)
{
    Preferences prefs;
    if (!prefs.begin("wifi", false)) return 0;
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
    return 1;
}

/* Read saved credentials. Returns 1 if present. */
int tui_wifi_load(char *ssid, int ssid_len, char *pass, int pass_len)
{
    Preferences prefs;
    if (!prefs.begin("wifi", true)) return 0;
    String s = prefs.getString("ssid", "");
    String p = prefs.getString("pass", "");
    prefs.end();
    if (s.length() == 0) return 0;
    strncpy(ssid, s.c_str(), ssid_len - 1);
    ssid[ssid_len - 1] = '\0';
    strncpy(pass, p.c_str(), pass_len - 1);
    pass[pass_len - 1] = '\0';
    return 1;
}




/* ---------- frame buffer API ---------- */

void tui_frame_begin(void)
{
    frame_len = 0;
}

void tui_frame_put(const char *s, int n)
{
    if (frame_len + n > frame_cap) {
        int newcap = frame_cap ? frame_cap * 2 : 4096;
        while (newcap < frame_len + n) newcap *= 2;
        char *nb = (char *)realloc(frame_buf, newcap);
        if (!nb) return;
        frame_buf = nb;
        frame_cap = newcap;
    }
    memcpy(frame_buf + frame_len, s, n);
    frame_len += n;
}

void tui_frame_end(void)
{
    if (frame_len > 0 && frame_buf)
        Terminal.write(frame_buf, frame_len);
    frame_len = 0;
}

/* ---------- core TUI API ---------- */

void tui_init(void)
{
    PS2Controller.begin(GPIO_NUM_33, GPIO_NUM_32, GPIO_NUM_26, GPIO_NUM_27);
    Keyboard.begin(true, true, 0);

    Serial.printf("HEAP before VGA   internal free=%u largest=%u | dma free=%u largest=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    VGAController.begin();
    VGAController.setResolution(VGA_640x480_60Hz);
    Serial.printf("HEAP after VGA    internal free=%u largest=%u | dma free=%u largest=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));

    Terminal.begin(&VGAController);
    Terminal.loadFont(&fabgl::FONT_8x8);
    Terminal.setBackgroundColor(fabgl::Color::Black);
    Terminal.setForegroundColor(fabgl::Color::White);
    Terminal.clear();
    Terminal.enableCursor(true);

    termctrl = new fabgl::TerminalController(&Terminal);

    Serial.printf("Terminal: %d cols x %d rows\n",
                  Terminal.getColumns(), Terminal.getRows());
    Serial.printf("viewport %d x %d\n",
              VGAController.getViewPortWidth(), VGAController.getViewPortHeight());
}

void tui_restore(void)
{
    Terminal.enableCursor(true);
    VGAController.end();
}

void tui_clear(void)
{
    if (linepos > 0) {
        linebuf[linepos] = '\0';
        Terminal.write(linebuf);
        linepos = 0;
    }
    Terminal.clear();
}

void tui_gotoxy(int x, int y)
{
    if (linepos > 0) {
        linebuf[linepos] = '\0';
        Terminal.write(linebuf);
        linepos = 0;
    }
    if (termctrl) termctrl->setCursorPos(x, y);
}

void tui_color(int fg, int bg)
{
    if (linepos > 0) {
        linebuf[linepos] = '\0';
        Terminal.write(linebuf);
        linepos = 0;
    }
    if (fg < 0 || fg > 15) fg = 7;
    if (bg < 0 || bg > 15) bg = 0;
    Terminal.setForegroundColor(tui_to_fabgl(fg));
    Terminal.setBackgroundColor(tui_to_fabgl(bg));
}

void tui_putc(int c)
{
    if (linepos >= 80) {
        linebuf[linepos] = '\0';
        Terminal.write(linebuf);
        linepos = 0;
    }
    linebuf[linepos++] = (char)c;
}

void tui_puts(const char *s)
{
    while (*s) tui_putc(*s++);
}

void tui_cursor(int visible, int block)
{
    Terminal.enableCursor(visible ? true : false);
}

/* ---------- keyboard ---------- */

static int pending_code = -1;          /* 2nd half of an extended key */
static int ready = -1, ready_second = -1;   /* decoded, waiting for getch */

static int vk_to_scancode(int vk)
{
    switch (vk) {
    case fabgl::VK_UP:       return 72;
    case fabgl::VK_DOWN:     return 80;
    case fabgl::VK_LEFT:     return 75;
    case fabgl::VK_RIGHT:    return 77;
    case fabgl::VK_HOME:     return 71;
    case fabgl::VK_END:      return 79;
    case fabgl::VK_PAGEUP:   return 73;
    case fabgl::VK_PAGEDOWN: return 81;
    case fabgl::VK_DELETE:   return 83;
    case fabgl::VK_F1:       return 59;
    case fabgl::VK_F2:       return 60;
    case fabgl::VK_F3:       return 61;
    case fabgl::VK_F4:       return 62;
    case fabgl::VK_F5:       return 63;
    case fabgl::VK_F6:       return 64;
    case fabgl::VK_F7:       return 65;
    case fabgl::VK_F8:       return 66;
    case fabgl::VK_F9:       return 67;
    case fabgl::VK_F10:      return 68;
    default:                 return -1;
    }
}

static int decode(const fabgl::VirtualKeyItem &it, int *second)
{
    *second = -1;
    if (!it.down) return -1;

    int code = vk_to_scancode(it.vk);
    if (code >= 0) { *second = code; return 0; }

    if (it.vk == fabgl::VK_RETURN)    return 13;
    if (it.vk == fabgl::VK_BACKSPACE) return 8;
    if (it.vk == fabgl::VK_ESCAPE)    return 27;
    if (it.vk == fabgl::VK_TAB)       return 9;

    if (it.CTRL || Keyboard.isVKDown(fabgl::VK_LCTRL) || Keyboard.isVKDown(fabgl::VK_RCTRL)) {
    int a = it.ASCII;
    if (a >= 1   && a <= 26)  return a;                  /* layout already gave control code */
    if (a >= 'a' && a <= 'z') return a - 'a' + 1;
    if (a >= 'A' && a <= 'Z') return a - 'A' + 1;
    if (it.vk >= fabgl::VK_a && it.vk <= fabgl::VK_z) return 1 + (it.vk - fabgl::VK_a);
    if (it.vk >= fabgl::VK_A && it.vk <= fabgl::VK_Z) return 1 + (it.vk - fabgl::VK_A);
    }

    if (it.ASCII >= 32 && it.ASCII < 127) return it.ASCII;
    return -1;
}

int tui_key_pending(void)
{
    if (pending_code >= 0 || ready >= 0) return 1;
    fabgl::VirtualKeyItem it;
    while (Keyboard.virtualKeyAvailable() && Keyboard.getNextVirtualKey(&it, 0)) {
        int r = decode(it, &ready_second);
        if (r >= 0) { ready = r; return 1; }
    }
    return 0;
}

int tui_getch(void)
{
    int r;
    if (linepos > 0) { linebuf[linepos] = '\0'; Terminal.write(linebuf); linepos = 0; }
    if (pending_code >= 0) { r = pending_code; pending_code = -1; return r; }
    if (ready >= 0) {
        r = ready; ready = -1;
        pending_code = ready_second; ready_second = -1;
        return r;
    }
    for (;;) {
        fabgl::VirtualKeyItem it;
        int second;
        if (Keyboard.getNextVirtualKey(&it)) {
            r = decode(it, &second);
            if (r >= 0) { pending_code = second; return r; }
        } else {
            delay(1);
        }
    }
}

int tui_shift_down(void)
{
    return (Keyboard.isVKDown(fabgl::VK_LSHIFT) ||
            Keyboard.isVKDown(fabgl::VK_RSHIFT)) ? 1 : 0;
}

int tui_list_dir(const char *path, char names[][32], int max)
{
    return fab_list_dir(path, names, max);
}

void tui_exe_dir(char *out, int len)
{
    if (len > 0) out[0] = '\0';
}

}   /* extern "C" */

#endif /* FABLE_ESP32 */

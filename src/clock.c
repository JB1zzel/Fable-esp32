/* clock.c - the date and time string, and (ESP32 only) the Wi-Fi setup page.
 *
 * ESP32:   the time is set once at boot by main.cpp (NTP); the system clock
 *          keeps running after Wi-Fi is switched off, so we just read time().
 *          F9 opens the Wi-Fi setup page.
 * Windows: the PC's clock is used; there is no Wi-Fi page.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "tui.h"
#include "fable.h"

#ifdef FABLE_ESP32
static char boot_wifi_ssid[33] = "";

void set_boot_wifi_ssid(const char *s)
{
    strncpy(boot_wifi_ssid, s, sizeof(boot_wifi_ssid) - 1);
    boot_wifi_ssid[sizeof(boot_wifi_ssid) - 1] = '\0';
}
#endif

/* Reads the system clock. Writes "YYYY-MM-DD HH:MM" local time, or an empty
 * string if the clock has never been set (ESP32 before the first NTP sync). */
void fb_current_time_str(char *out, int out_len)
{
    time_t t = time(NULL);
    struct tm *tm;

    if (t < 1700000000) {              /* before Nov 2023 = not synced */
        out[0] = '\0';
        return;
    }
    tm = localtime(&t);                /* same call on every compiler */
    if (!tm) {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_len, "%04d-%02d-%02d %02d:%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min);
}

#ifdef FABLE_ESP32
void fb_wifi_setup_page(void)
{
    char ssid[33];
    char pass[65];
    int have;
    int connected;

    have = tui_wifi_load(ssid, sizeof ssid, pass, sizeof pass);
    connected = (boot_wifi_ssid[0] != '\0');

    tui_clear();
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_gotoxy(1, 1);
    tui_puts("Wi-Fi Setup");

    tui_gotoxy(1, 3);
    if (connected) {
        char line[80];
        tui_color(TUI_LGREEN, TUI_BLACK);
        snprintf(line, sizeof line, "Connected to: %s", boot_wifi_ssid);
        tui_puts(line);
        tui_color(TUI_WHITE, TUI_BLACK);
    } else if (have) {
        char line[80];
        tui_color(TUI_LRED, TUI_BLACK);
        snprintf(line, sizeof line, "Saved SSID: %s (not connected)", ssid);
        tui_puts(line);
        tui_color(TUI_WHITE, TUI_BLACK);
    } else {
        tui_puts("No network saved yet.");
    }

    tui_gotoxy(1, 5);
    {
        char now[32];
        char line[80];
        fb_current_time_str(now, sizeof now);
        snprintf(line, sizeof line, "Network time: %s", now[0] ? now : "not available");
        tui_puts(line);
    }

    tui_gotoxy(1, 7);
    tui_puts("Type new credentials to replace them,");
    tui_gotoxy(1, 8);
    tui_puts("or press Enter alone to keep current.");

    tui_gotoxy(1, 10);
    tui_puts("SSID: ");
    ssid[0] = '\0';
    if (fb_prompt_line("", ssid, sizeof ssid) < 0) {
        tui_color(TUI_LGRAY, TUI_BLACK);
        tui_clear();
        return;
    }
    if (!ssid[0]) {
        tui_color(TUI_LGRAY, TUI_BLACK);
        tui_clear();
        return;
    }

    tui_gotoxy(1, 12);
    tui_puts("Password: ");
    pass[0] = '\0';
    if (fb_prompt_line("", pass, sizeof pass) < 0) {
        tui_color(TUI_LGRAY, TUI_BLACK);
        tui_clear();
        return;
    }

    if (tui_wifi_save(ssid, pass)) {
        tui_clear();
        tui_color(TUI_WHITE, TUI_BLACK);
        tui_gotoxy(1, 1);
        tui_puts("Wi-Fi credentials saved.");
        tui_gotoxy(1, 3);
        tui_puts("Connects on next boot.");
        tui_gotoxy(1, 5);
        tui_puts("Press a key to return to the editor.");
        tui_getch();
    } else {
        tui_clear();
        tui_color(TUI_WHITE, TUI_RED);
        tui_gotoxy(1, 1);
        tui_puts("Failed to save credentials.");
        tui_getch();
    }
    tui_color(TUI_LGRAY, TUI_BLACK);
    tui_clear();
}
#endif /* FABLE_ESP32 */

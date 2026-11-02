/* prompt.c - single-line text prompt used by the editor and Wi-Fi page */

#include "tui.h"
#include "fable.h"

int fb_prompt_line(const char *prompt, char *dest, int maxlen)
{
    int len = 0, c;
    tui_puts(prompt);
    for (;;) {
        c = tui_getch();
        if (c == 0 || c == 224) { tui_getch(); continue; }
        if (c == 13) break;
        if (c == 8) {
            if (len > 0) { len--; tui_putc('\b'); tui_putc(' '); tui_putc('\b'); }
            continue;
        }
        if (c == 27) { dest[0] = '\0'; return -1; }
        if (c >= 32 && c < 127 && len < maxlen - 1) {
            dest[len++] = (char)c;
            tui_putc(c);
        }
    }
    dest[len] = '\0';
    return 0;
}

int fb_prompt_line_edit(char *dest, int initial_len, int maxlen)
{
    int len = initial_len, c;
    tui_puts(dest);
    for (;;) {
        c = tui_getch();
        if (c == 0 || c == 224) { tui_getch(); continue; }
        if (c == 13) break;
        if (c == 8) {
            if (len > 0) {
                len--;
                tui_putc('\b');
                tui_putc(' ');
                tui_putc('\b');
            }
            continue;
        }
        if (c == 27) { dest[0] = '\0'; return -1; }
        if (c >= 32 && c < 127 && len < maxlen - 1) {
            dest[len++] = (char)c;
            tui_putc(c);
        }
    }
    dest[len] = '\0';
    return 0;
}

/* screen.c - status bar, prompt bar, word count and the main render. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tui.h"
#include "editor.h"

static int change_counter = 0;

static int cached_words = 0;

static int cached_words_counter = -1;

static int count_words(void)
{
    int n = 0, i, k;
    for (i = 0; i < ed_nlines; i++) {
        int in = 0;
        for (k = 0; ed_lines[i][k]; k++) {
            if (IS_WORD(ed_lines[i][k])) { if (!in) { n++; in = 1; } }
            else in = 0;
        }
    }
    return n;
}

void ed_status_msg(const char *msg)
{
    char buf[SCRW + 1];
    int i;
    snprintf(buf, sizeof buf, " %s", msg);
    for (i = (int)strlen(buf); i < SCRW; i++) buf[i] = ' ';
    buf[SCRW] = '\0';
    tui_gotoxy(1, SCRH + 1);
    tui_color(TUI_BLACK, TUI_LGRAY);
    tui_puts(buf);
    tui_color(TUI_LGRAY, TUI_BLACK);
}

void ed_prompt_bar(const char *text)
{
    char buf[SCRW + 1];
    int i;
    snprintf(buf, sizeof buf, " %s", text);
    for (i = (int)strlen(buf); i < SCRW; i++) buf[i] = ' ';
    buf[SCRW] = '\0';
    tui_gotoxy(1, SCRH + 1);
    tui_color(TUI_WHITE, TUI_BLUE);
    tui_puts(buf);
    tui_gotoxy((int)strlen(text) + 2, SCRH + 1);
}

static void put_status(void)
{
    char left[SCRW + 1];
    char right[SCRW + 1];
    char shown[64];
    int w = count_words();
    int len, rlen;
    const char *hint;
    const char *base;
    const char *dot;
    int n;

    base = ed_filename[0] ? ed_filename : "(unnamed)";
    {
        const char *s = base;
        while (*s) {
            if (*s == '\\' || *s == '/') base = s + 1;
            s++;
        }
    }
    dot = strrchr(base, '.');
    if (dot && dot != base) n = (int)(dot - base);
    else                     n = (int)strlen(base);
    if (n > (int)sizeof(shown) - 1) n = (int)sizeof(shown) - 1;
    memcpy(shown, base, (size_t)n);
    shown[n] = '\0';

    /* contextual hint — short, mode-aware */
    if (ed_mark_active)       hint = "^B END  ESC CANCEL";
    else if (ed_mark_exists)  hint = "^P PASTE  ^X CUT  ESC CLEAR";
    else                   hint = "F1 HELP  F3 SAVE  F4 SPELL  ESC EXIT";

    snprintf(left, sizeof left, " %s  %d:%d%s  w:%d%s",
             shown,
             ed_cur_line + 1, ed_cur_col + 1,
             ed_left_col > 0 ? "+" : "",
             w,
             ed_modified ? "  !!" : "");
    left[SCRW] = '\0';

    snprintf(right, sizeof right, "%s", hint);
    rlen = (int)strlen(right);

    len = (int)strlen(left);
    if (len + rlen > SCRW) {
        len = SCRW - rlen - 1;
        if (len < 0) len = 0;
        left[len] = '\0';
    }

    tui_gotoxy(1, SCRH + 1);
    tui_color(TUI_BLACK, TUI_LGRAY);
    {
        int i, gap = SCRW - rlen - len;
        tui_puts(left);
        for (i = 0; i < gap; i++) tui_putc(' ');
        tui_puts(right);
    }
    tui_color(TUI_LGRAY, TUI_BLACK);
}

void ed_render(void)
{
    int i;
    char esc[32];

    if (ed_cur_line < ed_top_line) ed_top_line = ed_cur_line;
    if (ed_cur_line >= ed_top_line + SCRH) ed_top_line = ed_cur_line - SCRH + 1;
    if (ed_top_line < 0) ed_top_line = 0;
    if (ed_top_line > ed_nlines - 1) ed_top_line = ed_nlines - 1;

    if (ed_cur_col < ed_left_col) ed_left_col = ed_cur_col;
    if (ed_cur_col >= ed_left_col + SCRW) ed_left_col = ed_cur_col - SCRW + 1;
    if (ed_left_col < 0) ed_left_col = 0;

    /* cache word count — only recompute when modified flag changes */
    cached_words = count_words();

    tui_frame_begin();

    tui_frame_put("\x1b[?25l\x1b[H", 9);

        for (i = 0; i < SCRH; i++) {
        int ln = ed_top_line + i;
        int prev_col = 0;                          /* was -1 */

        tui_frame_put("\x1b[37;40m", 8);           /* NEW: each row starts in normal colours */

        if (ln < ed_nlines) {
            int len = (int)strlen(ed_lines[ln]);
            int shown = 0;
            int k;
            if (ed_left_col < len) {
                int avail = len - ed_left_col;
                int lim = avail > SCRW ? SCRW : avail;
                for (k = 0; k < lim; k++) {
                    int src_col = ed_left_col + k;
                    int cid = 0;
                    if (ed_in_mark(ln, src_col))                             cid = 1;
                    else if (ed_spell_marks_on && ed_spell_is_bad(ln, src_col)) cid = 2;

                    if (cid != prev_col) {
                        if (cid == 1)      snprintf(esc, sizeof esc, "\x1b[30;46m");
                        else if (cid == 2) snprintf(esc, sizeof esc, "\x1b[91;40m");
                        else               snprintf(esc, sizeof esc, "\x1b[37;40m");
                        tui_frame_put(esc, (int)strlen(esc));
                        prev_col = cid;
                    }
                    tui_frame_put(&ed_lines[ln][src_col], 1);
                }
                shown = lim;
            }
            if (prev_col != 0) tui_frame_put("\x1b[37;40m", 8);   /* NEW: back to normal before padding */
            while (shown < SCRW) { tui_frame_put(" ", 1); shown++; }
        } else {
            int k;
            for (k = 0; k < SCRW; k++) tui_frame_put(" ", 1);
        }
        tui_frame_put("\x1b[K\r\n", 5);
    }

    /* status bar */
    {
        char left[SCRW + 1];
        char right[SCRW + 1];
        char shown[64];
        int len, rlen;
        const char *hint;
        const char *base;
        const char *dot;
        int n;

        base = ed_filename[0] ? ed_filename : "(unnamed)";
        {
            const char *s = base;
            while (*s) {
                if (*s == '\\' || *s == '/') base = s + 1;
                s++;
            }
        }
        dot = strrchr(base, '.');
        if (dot && dot != base) n = (int)(dot - base);
        else                     n = (int)strlen(base);
        if (n > (int)sizeof(shown) - 1) n = (int)sizeof(shown) - 1;
        memcpy(shown, base, (size_t)n);
        shown[n] = '\0';

        if (ed_mark_active)       hint = "^B END  ESC CANCEL";
        else if (ed_mark_exists)  hint = "^P PASTE  ^X CUT  ESC CLEAR";
        else                   hint = "F1 HELP  F3 SAVE  F4 SPELL  ESC EXIT";

        snprintf(left, sizeof left, " %s  %d:%d%s  w:%d%s",
                 shown, ed_cur_line + 1, ed_cur_col + 1,
                 ed_left_col > 0 ? "+" : "",
                 cached_words,
                 ed_modified ? "  !!" : "");
        left[SCRW] = '\0';

        snprintf(right, sizeof right, "%s", hint);
        rlen = (int)strlen(right);

        len = (int)strlen(left);
        if (len + rlen > SCRW) {
            len = SCRW - rlen - 1;
            if (len < 0) len = 0;
            left[len] = '\0';
        }

        tui_frame_put("\x1b[30;47m", 8);    /* black on white */
        tui_frame_put(left, len);
        {
            int gap = SCRW - rlen - len;
            int j;
            for (j = 0; j < gap; j++) tui_frame_put(" ", 1);
        }
        tui_frame_put(right, rlen);
        tui_frame_put("\x1b[37;40m", 8);    /* back to light grey on black */
        tui_frame_put("\x1b[K", 3);
    }

    snprintf(esc, sizeof esc, "\x1b[%d;%dH\x1b[?25h",
             ed_cur_line - ed_top_line + 1, ed_cur_col - ed_left_col + 1);
    tui_frame_put(esc, (int)strlen(esc));

    tui_frame_end();
}

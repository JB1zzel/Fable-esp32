/* help.c - F1 key reference screen */

#include <stdio.h>
#include <string.h>
#include "tui.h"
#include "fable.h"

void fb_help_screen(void)
{
    tui_clear();
    tui_color(TUI_WHITE, TUI_BLACK);

    tui_gotoxy(1, 1);
    tui_puts("FABLE - Jamie Bradbury 2026");
    tui_gotoxy(1, 3);
    tui_puts("  Arrows        move cursor");
    tui_gotoxy(1, 4);
    tui_puts("  Shift+Arrows  word left / right");
    tui_gotoxy(1, 5);
    tui_puts("  Home / End    line start / end");
    tui_gotoxy(1, 6);
    tui_puts("  PgUp / PgDn   page up / down");
    tui_gotoxy(1, 8);
    tui_puts("  Enter         split line");
    tui_gotoxy(1, 9);
    tui_puts("  Backspace     delete char / merge up");
    tui_gotoxy(1, 10);
    tui_puts("  Delete        delete char / merge down");
    tui_gotoxy(1, 12);
    tui_puts("  F1            this help");
    tui_gotoxy(1, 13);
    tui_puts("  F2            reflow paragraph");
    tui_gotoxy(1, 14);
    tui_puts("  F3 / Ctrl+S   save");
    tui_gotoxy(1, 15);
    tui_puts("  F4            spellcheck");
    tui_gotoxy(1, 16);
    tui_puts("  F5            highlight spelling in document");
    tui_gotoxy(1, 17);
    tui_puts("  F6            diary calendar (* = day has an entry)");
    tui_gotoxy(1, 18);
    tui_puts("  F7            games (snake, minesweeper, solitaire)");
#ifdef FABLE_ESP32
    tui_gotoxy(1, 19);
    tui_puts("  F9            Wi-Fi setup");
#endif
    tui_gotoxy(1, 21);
    tui_puts("  Ctrl+F        find");
    tui_gotoxy(1, 22);
    tui_puts("  Ctrl+R        find & replace");
    tui_gotoxy(1, 23);
    tui_puts("  Ctrl+B/X/P    mark / cut / paste");
    tui_gotoxy(1, 24);
    tui_puts("  Ctrl+N        new / open file (browser: N new, D del, R rename)");
    tui_gotoxy(1, 25);
    tui_puts("  Ctrl+O        open file by path");
    tui_gotoxy(1, 27);
    tui_puts("  Esc           quit (prompts if modified)");

    tui_gotoxy(1, SCRH + 1);
    tui_color(TUI_BLACK, TUI_LGRAY);
    {
        char buf[SCRW + 1];
        int i;
        snprintf(buf, sizeof buf, " F1 HELP  F2 REFLOW  F3 SAVE  ESC EXIT");
        for (i = (int)strlen(buf); i < SCRW; i++) buf[i] = ' ';
        buf[SCRW] = '\0';
        tui_puts(buf);
    }
    tui_color(TUI_LGRAY, TUI_BLACK);

    tui_getch();          /* any key returns */
}

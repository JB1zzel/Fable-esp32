/* filer.c - lists the files on the SD card (Ctrl+N helper) */

#include <stdio.h>
#include <stdlib.h>
#include "tui.h"
#include "fable.h"

#define MAXFILES 64

void fb_show_files(void)
{
    char (*names)[32] = (char (*)[32])malloc((size_t)MAXFILES * 32);   /* was: static char names[MAXFILES][32]; */
    int n, i;

    if (!names) return;
    n = tui_list_dir("/", names, MAXFILES);

    tui_clear();
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_gotoxy(1, 1);
    tui_puts("Files on drive:");

    if (n == 0) {
        tui_gotoxy(1, 3);
        tui_puts("(none found)");
    }
    for (i = 0; i < n; i++) {
        char cell[26];
        snprintf(cell, sizeof cell, "%.25s", names[i]);
        tui_gotoxy((i / 22) * 26 + 1, (i % 22) + 3);
        tui_puts(cell);
    }
    tui_color(TUI_LGRAY, TUI_BLACK);
    free(names);                                                        /* NEW */
}

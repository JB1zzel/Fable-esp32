/* games.c - F7 game launcher. Pops up a small menu, then runs the
 *           chosen game. Returns when the game exits.
 *
 * Build: add games.c to the fable.exe compile line.
 */
#include <stdio.h>
#include <string.h>
#include "tui.h"
#include "fable.h"

#define MENU_X   20
#define MENU_Y   8
#define MENU_W   22          /* 20 inner columns */

/* Draw a small popup with a highlighted selection. sel is 0-based. */
static void draw_menu(int sel)
{
    static const char *items[] = { "Snake", "Minesweeper", "Solitaire" };
    int n = (int)(sizeof items / sizeof items[0]);
    int i, j;

    tui_color(TUI_WHITE, TUI_BLACK);

    /* top border */
    tui_gotoxy(MENU_X, MENU_Y);
    tui_putc(0xDA);
    for (j = 0; j < MENU_W - 2; j++) tui_putc(0xC4);
    tui_putc(0xBF);

    /* title row */
    tui_gotoxy(MENU_X, MENU_Y + 1);
    tui_putc(0xB3);
    tui_gotoxy(MENU_X + 1, MENU_Y + 1);
    for (j = 0; j < MENU_W - 2; j++) tui_putc(' ');
    tui_gotoxy(MENU_X + 2, MENU_Y + 1);
    tui_puts("GAMES");
    tui_gotoxy(MENU_X + MENU_W - 1, MENU_Y + 1);
    tui_putc(0xB3);

    /* divider under title */
    tui_gotoxy(MENU_X, MENU_Y + 2);
    tui_putc(0xC3);
    for (j = 0; j < MENU_W - 2; j++) tui_putc(0xC4);
    tui_putc(0xB4);

    /* item rows */
    for (i = 0; i < n; i++) {
        int y = MENU_Y + 3 + i;
        char line[64];
        tui_gotoxy(MENU_X, y);
        tui_putc(0xB3);
        tui_gotoxy(MENU_X + 1, y);
        for (j = 0; j < MENU_W - 2; j++) tui_putc(' ');

        snprintf(line, sizeof line, " %d. %s", i + 1, items[i]);
        if (i == sel) tui_color(TUI_BLACK, TUI_LGRAY);
        else          tui_color(TUI_WHITE, TUI_BLACK);
        tui_gotoxy(MENU_X + 2, y);
        tui_puts(line);
        /* pad the highlight to the full inner row */
        for (j = (int)strlen(line); j < MENU_W - 4; j++) tui_putc(' ');
        tui_color(TUI_WHITE, TUI_BLACK);

        tui_gotoxy(MENU_X + MENU_W - 1, y);
        tui_putc(0xB3);
    }

    /* two blank rows so the frame has breathing room */
    for (i = n; i < n + 1; i++) {
        int y = MENU_Y + 3 + i;
        tui_gotoxy(MENU_X, y);
        tui_putc(0xB3);
        tui_gotoxy(MENU_X + 1, y);
        for (j = 0; j < MENU_W - 2; j++) tui_putc(' ');
        tui_gotoxy(MENU_X + MENU_W - 1, y);
        tui_putc(0xB3);
    }

    /* hint row */
    {
        int y = MENU_Y + 3 + n + 1;
        const char *hint = "UP/DOWN ENTER  ESC";
        tui_gotoxy(MENU_X, y);
        tui_putc(0xB3);
        tui_gotoxy(MENU_X + 1, y);
        for (j = 0; j < MENU_W - 2; j++) tui_putc(' ');
        tui_gotoxy(MENU_X + 1 + ((MENU_W - 2) - (int)strlen(hint)) / 2, y);
        tui_puts(hint);
        tui_gotoxy(MENU_X + MENU_W - 1, y);
        tui_putc(0xB3);
    }

    /* bottom border */
    {
        int y = MENU_Y + 3 + n + 2;
        tui_gotoxy(MENU_X, y);
        tui_putc(0xC0);
        for (j = 0; j < MENU_W - 2; j++) tui_putc(0xC4);
        tui_putc(0xD9);
    }
}

/* Clear the popup rectangle so it doesn't leave bits behind. */
static void clear_menu(int rows)
{
    int i, j;
    tui_color(TUI_LGRAY, TUI_BLACK);
    for (i = 0; i < rows; i++) {
        tui_gotoxy(MENU_X, MENU_Y + i);
        for (j = 0; j < MENU_W; j++) tui_putc(' ');
    }
}

int fb_game_menu(void)
{
    static const int count = 3;   /* keep in sync with draw_menu() items */
    int sel = 0;
    int chosen = -1;
    //int rows   = 3 + count + 1 + 1;   /* border + items + hint + border */
    int rows   = 3 + count + 1 + 1 + 1;   /* border+title+divider+items+blank+hint+border */

    tui_cursor(0, 0);

    draw_menu(sel);

    for (;;) {
        int c = tui_getch();

        if (c == 0 || c == 224) {
            int code = tui_getch();
            if (code == 72) {                       /* up */
                if (sel > 0) sel--;
                draw_menu(sel);
            } else if (code == 80) {                /* down */
                if (sel < count - 1) sel++;
                draw_menu(sel);
            }
            continue;
        }

        if (c == 27) {                              /* Esc: cancel */
            chosen = -1;
            break;
        }
        if (c == 13) {                              /* Enter */
            chosen = sel;
            break;
        }
        if (c >= '1' && c <= '0' + count) {         /* direct number */
            chosen = c - '1';
            break;
        }
    }

    clear_menu(rows);
    tui_cursor(0, 0);

    if (chosen == 0) { fb_snake();       return 0; }
    if (chosen == 1) { fb_minesweeper(); return 0; }
	if (chosen == 2) { fb_solitaire(); return 1; }
    return 0;   /* Esc: caller resumes editor */
}
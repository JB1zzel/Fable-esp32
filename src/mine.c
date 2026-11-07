/* mine.c - Minesweeper, drawn in a popup frame like snake.c
 *
 * Standalone test build:
 *   tcc mine_main.c mine.c snake.c games.c tui_console.c file_win.c -luser32 -o games.exe
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tui.h"
#include "fable.h"

/* Same popup geometry as the cobra window */
#define POP_X   20
#define POP_Y   1
#define POP_W   22
#define GOY     (POP_Y + 3)

#define GW      15
#define GH      15
#define NCELLS  (GW * GH)

/* centre the 15-column grid inside the frame */
#define GRID_OFF_X  ((POP_W - 2 - GW) / 2)
#define GOX         (POP_X + 1 + GRID_OFF_X)

#define BOMB    9
#define HIDDEN  0
#define FLAGGED 1
#define OPENED  2

static unsigned char grid[NCELLS + 1];
static unsigned char flags[NCELLS + 1];

static int difficulty = 2;
static int bombs = 0, flagno = 0, score = 0;
static int cur_row = 7, cur_col = 7, cur_loc = 1;
static int gameover = 0, won = 0;

/* ---------- edge helpers ---------- */
static int is_left_edge(int idx)
{
    switch (idx) {
    case 16: case 31: case 46: case 61: case 76: case 91: case 106:
    case 121: case 136: case 151: case 166: case 181: case 196:
    case 211: case 226: return 1;
    default: return 0;
    }
}
static int is_right_edge(int idx)
{
    switch (idx) {
    case 15: case 30: case 45: case 60: case 75: case 90: case 105:
    case 120: case 135: case 150: case 165: case 180: case 195:
    case 210: case 225: return 1;
    default: return 0;
    }
}
static int in_bounds(int idx) { return idx >= 1 && idx <= NCELLS; }
static void sync_loc(void)    { cur_loc = cur_row * GW + cur_col + 1; }

/* ---------- setup ---------- */
static void place_bombs(void)
{
    int i;
    bombs = 0;
    for (i = 1; i <= NCELLS; i++) {
        grid[i] = 0;
        flags[i] = HIDDEN;
        if ((rand() % 10) + 1 < difficulty) { grid[i] = BOMB; bombs++; }
    }
    score  = NCELLS - bombs;
    flagno = 0;
}

static void compute_numbers(void)
{
    int a;
    for (a = 1; a <= NCELLS; a++) {
        if (grid[a] != BOMB) continue;
        if (!is_right_edge(a)) {
            if (in_bounds(a+1)  && grid[a+1]  != BOMB) grid[a+1]++;
            if (in_bounds(a-14) && grid[a-14] != BOMB) grid[a-14]++;
            if (in_bounds(a+16) && grid[a+16] != BOMB) grid[a+16]++;
        }
        if (!is_left_edge(a)) {
            if (in_bounds(a-1)  && grid[a-1]  != BOMB) grid[a-1]++;
            if (in_bounds(a+14) && grid[a+14] != BOMB) grid[a+14]++;
            if (in_bounds(a-16) && grid[a-16] != BOMB) grid[a-16]++;
        }
        if (in_bounds(a-15) && grid[a-15] != BOMB) grid[a-15]++;
        if (in_bounds(a+15) && grid[a+15] != BOMB) grid[a+15]++;
    }
}

static void make_start_safe(void)
{
    sync_loc();
    while (grid[cur_loc] == BOMB) {
        if (cur_col < GW - 1)        cur_col++;
        else if (cur_row < GH - 1) { cur_row++; cur_col = 0; }
        else                       { cur_row = 0; cur_col = 0; }
        sync_loc();
    }
}

/* ---------- reveal: BASIC-style row-major scan ---------- */

/* Paint one cell using the same colours draw() uses. */
static void paint_cell(int idx)
{
    int r = (idx - 1) / GW;
    int c = (idx - 1) % GW;
    unsigned char ch;

    if (flags[idx] == FLAGGED)       ch = 'F';
    else if (flags[idx] == OPENED) {
        if (grid[idx] == BOMB)       ch = '*';
        else if (grid[idx] == 0)     ch = ' ';
        else                         ch = '0' + grid[idx];
    } else ch = '.';

    tui_gotoxy(GOX + c, GOY + r);
    if (r == cur_row && c == cur_col) {
        tui_color(TUI_BLACK, TUI_LGRAY);
        tui_putc(ch);
        tui_color(TUI_WHITE, TUI_BLACK);
    } else if (ch >= '1' && ch <= '8') {
        tui_color(TUI_YELLOW, TUI_BLACK);
        tui_putc(ch);
        tui_color(TUI_WHITE, TUI_BLACK);
    } else if (ch == 'F') {
        tui_color(TUI_LRED, TUI_BLACK);
        tui_putc(ch);
        tui_color(TUI_WHITE, TUI_BLACK);
    } else {
        tui_putc(ch);
    }
}

/* Neighbour deltas in (dr, dc) form. */
static const int fdr[8] = { -1, -1, -1,  0,  0,  1,  1,  1 };
static const int fdc[8] = { -1,  0,  1, -1,  1, -1,  0,  1 };

/* Open a region by scanning the whole grid top-to-bottom, left-to-right
 * on each pass (like the BASIC empty() / friendisempty() pair). Pauses
 * after each row that changed so the player can watch the wave. */
static void empty(int start)
{
    int row, col, k;
    int opened_any = 1;

    if (flags[start] != HIDDEN) return;
    flags[start] = OPENED;
    score--;
    paint_cell(start);

    while (opened_any) {
        opened_any = 0;

        for (row = 0; row < GH; row++) {
            int row_opened = 0;

            for (col = 0; col < GW; col++) {
                int idx = row * GW + col + 1;
                int touches = 0;

                if (flags[idx] != HIDDEN) continue;
                if (grid[idx] == BOMB)    continue;

                for (k = 0; k < 8; k++) {
                    int nr = row + fdr[k];
                    int nc = col + fdc[k];
                    int nb;
                    if (nr < 0 || nr >= GH) continue;
                    if (nc < 0 || nc >= GW) continue;
                    nb = nr * GW + nc + 1;
                    if (flags[nb] != OPENED) continue;
                    if (grid[nb]  != 0)      continue;   /* only blanks spread */
                    touches = 1;
                    break;
                }

                if (!touches) continue;

                flags[idx] = OPENED;
                score--;
                paint_cell(idx);
                row_opened = 1;
                opened_any = 1;
            }

            if (row_opened) tui_delay(10);
        }
    }
}

static void reveal_all(void)
{
    int i;
    for (i = 1; i <= NCELLS; i++) flags[i] = OPENED;
}

/* ---------- the frame ---------- */
static void draw_frame(void)
{
    int i;

    tui_color(TUI_WHITE, TUI_BLACK);

    /* top border */
    tui_gotoxy(POP_X, POP_Y);
    tui_putc(0xDA);
    for (i = 0; i < POP_W - 2; i++) tui_putc(0xC4);
    tui_putc(0xBF);

    /* title row (status is drawn here by draw_status()) */
    tui_gotoxy(POP_X, POP_Y + 1);
    tui_putc(0xB3);
    tui_gotoxy(POP_X + 1, POP_Y + 1);
    for (i = 0; i < POP_W - 2; i++) tui_putc(' ');
    tui_gotoxy(POP_X + POP_W - 1, POP_Y + 1);
    tui_putc(0xB3);

    /* divider under title */
    tui_gotoxy(POP_X, POP_Y + 2);
    tui_putc(0xC3);
    for (i = 0; i < POP_W - 2; i++) tui_putc(0xC4);
    tui_putc(0xB4);

    /* side borders for playfield rows */
    for (i = 0; i < GH; i++) {
        tui_gotoxy(POP_X, GOY + i);
        tui_putc(0xB3);
        tui_gotoxy(POP_X + POP_W - 1, GOY + i);
        tui_putc(0xB3);
    }

    /* divider after playfield */
    tui_gotoxy(POP_X, POP_Y + GH + 3);
    tui_putc(0xC3);
    for (i = 0; i < POP_W - 2; i++) tui_putc(0xC4);
    tui_putc(0xB4);

    /* hint row */
    tui_gotoxy(POP_X, POP_Y + GH + 4);
    tui_putc(0xB3);
    tui_gotoxy(POP_X + 1, POP_Y + GH + 4);
    for (i = 0; i < POP_W - 2; i++) tui_putc(' ');
    tui_gotoxy(POP_X + 2, POP_Y + GH + 4);
    tui_puts("WASD SPC M X");
    tui_gotoxy(POP_X + POP_W - 1, POP_Y + GH + 4);
    tui_putc(0xB3);

    /* bottom border */
    tui_gotoxy(POP_X, POP_Y + GH + 5);
    tui_putc(0xC0);
    for (i = 0; i < POP_W - 2; i++) tui_putc(0xC4);
    tui_putc(0xD9);
}

static void draw_status(void)
{
    char buf[32];
    snprintf(buf, sizeof buf, "B:%02d F:%02d", bombs - flagno, flagno);
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_gotoxy(POP_X + POP_W - 2 - (int)strlen(buf), POP_Y + 1);
    tui_puts(buf);
}

/* One cursor move per row — same technique as snake's draw(). */
static void draw(void)
{
    static unsigned char fb[GH][GW];
    int i, j;

    memset(fb, ' ', sizeof fb);
    for (i = 0; i < GH; i++) {
        for (j = 0; j < GW; j++) {
            int idx = i * GW + j + 1;
            unsigned char ch;
            if (flags[idx] == FLAGGED)       ch = 'F';
            else if (flags[idx] == OPENED) {
                if (grid[idx] == BOMB)       ch = '*';
                else if (grid[idx] == 0)     ch = ' ';
                else                         ch = '0' + grid[idx];
            } else ch = '.';
            fb[i][j] = ch;
        }
    }

    for (i = 0; i < GH; i++) {
        tui_gotoxy(GOX, GOY + i);
        for (j = 0; j < GW; j++) {
            unsigned char ch = fb[i][j];
            if (i == cur_row && j == cur_col) {
                tui_color(TUI_BLACK, TUI_LGRAY);
                tui_putc(ch);
                tui_color(TUI_WHITE, TUI_BLACK);
            } else if (ch >= '1' && ch <= '8') {
                tui_color(TUI_YELLOW, TUI_BLACK);
                tui_putc(ch);
                tui_color(TUI_WHITE, TUI_BLACK);
            } else if (ch == 'F') {
                tui_color(TUI_LRED, TUI_BLACK);
                tui_putc(ch);
                tui_color(TUI_WHITE, TUI_BLACK);
            } else {
                tui_putc(ch);
            }
        }
    }
}

static void handle_input(void)
{
    while (tui_key_pending()) {
        int c = tui_getch();
        if (c == 0 || c == 224) {
            int code = tui_getch();
            switch (code) {
            case 75: if (cur_col > 0)       cur_col--; sync_loc(); break;
            case 77: if (cur_col < GW - 1)  cur_col++; sync_loc(); break;
            case 72: if (cur_row > 0)       cur_row--; sync_loc(); break;
            case 80: if (cur_row < GH - 1)  cur_row++; sync_loc(); break;
            }
        } else {
            switch (c) {
            case 'a': case 'A':
                if (cur_col > 0) { cur_col--; sync_loc(); } break;
            case 'd': case 'D':
                if (cur_col < GW - 1) { cur_col++; sync_loc(); } break;
            case 'w': case 'W':
                if (cur_row > 0) { cur_row--; sync_loc(); } break;
            case 's': case 'S':
                if (cur_row < GH - 1) { cur_row++; sync_loc(); } break;
            case ' ': case 'o': case 'O':
                if (grid[cur_loc] == BOMB)          gameover = 1;
                else if (flags[cur_loc] == HIDDEN)  empty(cur_loc);
                break;
            case 'm': case 'M':
                if (flags[cur_loc] == HIDDEN)      { flags[cur_loc] = FLAGGED; flagno++; }
                else if (flags[cur_loc] == FLAGGED){ flags[cur_loc] = HIDDEN;  flagno--; }
                break;
            case 'x': case 'X': case 27:
                gameover = 1; break;
            }
        }
    }
}

/* ---------- entry point ---------- */
int fb_minesweeper(void)
{
    int k;

    tui_cursor(0, 0);
    srand((unsigned)tui_millis());

    /* difficulty picker inside the frame */
    draw_frame();
    {
        const char *prompt = "2 easy ->5 hard";
        tui_color(TUI_WHITE, TUI_BLACK);
        tui_gotoxy(POP_X + 1 + ((POP_W - 2) - (int)strlen(prompt)) / 2,
                   GOY + GH / 2);
        tui_puts(prompt);
    }
    do { k = tui_getch(); } while (k < '2' || k > '5');
    difficulty = k - '0';

    cur_row = 7; cur_col = 7; sync_loc();
    place_bombs();
    compute_numbers();
    make_start_safe();
    gameover = 0; won = 0;

    draw_frame();
    draw_status();

    while (!gameover) {
        draw();
        if (score == 0) { won = 1; break; }
        handle_input();
        draw_status();
        tui_delay(30);
    }

    /* loss: reveal every cell so the player can see where the bombs were */
    if (!won) {
        reveal_all();
        draw();
        draw_status();
    }

    /* draw the end message on top of the field, so the bombs stay visible */
    {
        char buf[32];
        int  cx = POP_X + 1 + (POP_W - 2) / 2;
        const char *title = won ? "WIN!" : "BOOM!";
        int row = GOY + GH / 2 - 1;

        tui_color(TUI_WHITE, TUI_RED);
        tui_gotoxy(cx - (int)strlen(title) / 2, row);
        tui_puts(title);
        tui_putc(' ');
        tui_putc(' ');
        tui_putc(' ');
        tui_putc(' ');

        snprintf(buf, sizeof buf, " Score: %d ", score);
        tui_gotoxy(cx - (int)strlen(buf) / 2, row + 2);
        tui_puts(buf);

        tui_color(TUI_WHITE, TUI_BLUE);
        tui_gotoxy(cx - 6, row + 4);
        tui_puts("Press any key");

        tui_color(TUI_WHITE, TUI_BLACK);
    }

    while (tui_key_pending()) tui_getch();
    tui_getch();

    tui_cursor(1, 0);
    return 0;
}
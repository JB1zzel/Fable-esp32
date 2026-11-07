#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tui.h"
#include "fable.h"

#define POP_X   20
#define POP_Y   1
#define POP_W   42
#define POP_H   26
#define GW      40
#define GH      20
#define GOX     (POP_X + 1)
#define GOY     (POP_Y + 3)

#define MAX_TAIL (GW * GH)        /* the snake can never be longer than the board */

static int *tailX = NULL, *tailY = NULL;
static int tailLen;
static int snakeX, snakeY, fruitX, fruitY;
static int dir;           /* 0=stopped, 1=left, 2=right, 3=up, 4=down */
static int moved_dir;     /* direction of the last step actually taken */
static int score;
static int gameover;

/* true if (x,y) is under the head or any tail segment */
static int on_snake(int x, int y)
{
    int k;
    if (x == snakeX && y == snakeY) return 1;
    for (k = 0; k < tailLen; k++)
        if (tailX[k] == x && tailY[k] == y) return 1;
    return 0;
}

static void place_fruit(void)
{
    int tries;
    for (tries = 0; tries < 200; tries++) {
        fruitX = 1 + rand() % (GW - 2);
        fruitY = 1 + rand() % (GH - 2);
        if (!on_snake(fruitX, fruitY)) return;
    }
    /* very full board: take the first free cell */
    for (fruitY = 0; fruitY < GH; fruitY++)
        for (fruitX = 0; fruitX < GW; fruitX++)
            if (!on_snake(fruitX, fruitY)) return;
}

static void game_setup(void)
{
    gameover = 0;
    score = 0;
    tailLen = 0;
    snakeX = GW / 2;
    snakeY = GH / 2;
    dir = 0;
    moved_dir = 0;
    srand((unsigned)tui_millis());
    place_fruit();
}

static void draw_score(void)
{
    char buf[32];
    snprintf(buf, sizeof buf, "Score: %-6d", score);
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_gotoxy(POP_X + POP_W - 2 - (int)strlen(buf), POP_Y + 1);
    tui_puts(buf);
}

static void draw_frame(void)
{
    int i;

    tui_color(TUI_WHITE, TUI_BLACK);

    /* top border */
    tui_gotoxy(POP_X, POP_Y);
    tui_putc(0xDA);
    for (i = 0; i < POP_W - 2; i++) tui_putc(0xC4);
    tui_putc(0xBF);

    /* title row */
    tui_gotoxy(POP_X, POP_Y + 1);
    tui_putc(0xB3);
    tui_gotoxy(POP_X + 1, POP_Y + 1);
    for (i = 0; i < POP_W - 2; i++) tui_putc(' ');
    tui_gotoxy(POP_X + 2, POP_Y + 1);
    tui_puts("COBRA");

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

    /* hints row */
    tui_gotoxy(POP_X, POP_Y + GH + 4);
    tui_putc(0xB3);
    tui_gotoxy(POP_X + 1, POP_Y + GH + 4);
    for (i = 0; i < POP_W - 2; i++) tui_putc(' ');
    tui_gotoxy(POP_X + 2, POP_Y + GH + 4);
    tui_puts("Arrows or WASD move    X or ESC to quit");
    tui_gotoxy(POP_X + POP_W - 1, POP_Y + GH + 4);
    tui_putc(0xB3);

    /* bottom border */
    tui_gotoxy(POP_X, POP_Y + GH + 5);
    tui_putc(0xC0);
    for (i = 0; i < POP_W - 2; i++) tui_putc(0xC4);
    tui_putc(0xD9);
}

/* Draws the playfield one row at a time: one cursor move per row instead
 * of one per cell, which is far fewer writes to the terminal. */
static void draw(void)
{
    static unsigned char grid[GH][GW];
    int i, j;

    memset(grid, ' ', sizeof grid);
    for (i = 0; i < tailLen; i++)
        if (tailX[i] >= 0 && tailX[i] < GW && tailY[i] >= 0 && tailY[i] < GH)
            grid[tailY[i]][tailX[i]] = 0xB2;              /* body */
    if (snakeX >= 0 && snakeX < GW && snakeY >= 0 && snakeY < GH)
        grid[snakeY][snakeX] = 0xDB;                      /* head */
    grid[fruitY][fruitX] = 0xB2;                          /* fruit */

    tui_color(TUI_WHITE, TUI_BLACK);
    for (i = 0; i < GH; i++) {
        tui_gotoxy(GOX, GOY + i);
        for (j = 0; j < GW; j++) {
            if (i == fruitY && j == fruitX) {
                tui_color(TUI_YELLOW, TUI_BLACK);
                tui_putc(grid[i][j]);
                tui_color(TUI_WHITE, TUI_BLACK);
            } else {
                tui_putc(grid[i][j]);
            }
        }
    }
}

static void handle_input(void)
{
    while (tui_key_pending()) {
        int c = tui_getch();
        int code;

        if (c == 0 || c == 224) {
            code = tui_getch();
            switch (code) {
            case 75: if (moved_dir != 2) dir = 1; break;   /* left */
            case 77: if (moved_dir != 1) dir = 2; break;   /* right */
            case 72: if (moved_dir != 4) dir = 3; break;   /* up */
            case 80: if (moved_dir != 3) dir = 4; break;   /* down */
            }
        } else {
            switch (c) {
            case 'a': case 'A': if (moved_dir != 2) dir = 1; break;
            case 'd': case 'D': if (moved_dir != 1) dir = 2; break;
            case 'w': case 'W': if (moved_dir != 4) dir = 3; break;
            case 's': case 'S': if (moved_dir != 3) dir = 4; break;
            case 'x': case 'X': gameover = 1; break;
            case 27:            gameover = 1; break;   /* Esc */
            }
        }
    }
}

static void step(void)
{
    int prevX, prevY, i;

    /* tail follows head */
    prevX = tailX[0];
    prevY = tailY[0];
    tailX[0] = snakeX;
    tailY[0] = snakeY;
    for (i = 1; i < tailLen; i++) {
        int prev2X = tailX[i];
        int prev2Y = tailY[i];
        tailX[i] = prevX;
        tailY[i] = prevY;
        prevX = prev2X;
        prevY = prev2Y;
    }

    /* move head */
    switch (dir) {
    case 1: snakeX--; break;
    case 2: snakeX++; break;
    case 3: snakeY--; break;
    case 4: snakeY++; break;
    }
    moved_dir = dir;

    /* wall collision */
    if (snakeX < 0 || snakeX >= GW || snakeY < 0 || snakeY >= GH)
        gameover = 1;

    /* self collision */
    for (i = 0; i < tailLen; i++) {
        if (tailX[i] == snakeX && tailY[i] == snakeY)
            gameover = 1;
    }

    /* eat fruit */
    if (snakeX == fruitX && snakeY == fruitY && tailLen < MAX_TAIL - 1) {
        score += 10;
        if (tailLen > 0) {
            tailX[tailLen] = tailX[tailLen - 1];
            tailY[tailLen] = tailY[tailLen - 1];
        }
        tailLen++;
        place_fruit();
    }
}

int fb_snake(void)
{
    int i, j;  
 
    if (!tailX) {
        tailX = (int *)fable_big_calloc(MAX_TAIL, sizeof(int));
        tailY = (int *)fable_big_calloc(MAX_TAIL, sizeof(int));
        if (!tailX || !tailY) {
            free(tailX);
            free(tailY);
            tailX = tailY = NULL;
            return 1;
        }
    }

    tui_cursor(0, 0);

    game_setup();
    draw_frame();                      /* static parts: drawn once */
    draw_score();

    while (!gameover) {
        int old_score = score;
        draw();
        handle_input();
        step();
        if (score != old_score) draw_score();
        tui_delay(120);
    }

    /* clear the playfield area and show game over inside the frame */
    tui_color(TUI_WHITE, TUI_BLACK);
    for (i = 0; i < GH; i++) {
        tui_gotoxy(GOX, GOY + i);
        for (j = 0; j < GW; j++) tui_putc(' ');
    }
    {
        char buf[32];
        tui_gotoxy(GOX + (GW - 9) / 2, GOY + GH / 2 - 1);
        tui_puts("GAME OVER");
        snprintf(buf, sizeof buf, "Score: %d", score);
        tui_gotoxy(GOX + (GW - (int)strlen(buf)) / 2, GOY + GH / 2 + 1);
        tui_puts(buf);
        tui_gotoxy(GOX + (GW - 21) / 2, GOY + GH / 2 + 3);
        tui_puts("Press any key to exit");
    }
    while (tui_key_pending()) tui_getch();   /* keys pressed as we died */
    tui_getch();

    tui_cursor(1, 0);
    return 0;
}
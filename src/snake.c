#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tui.h"
#include "fable.h"

#ifdef FABLE_ESP32
#include "esp_heap_caps.h"
#endif

#define GW  40
#define GH  20
#define GOX 20
#define GOY 15

static int *tailX = NULL, *tailY = NULL;
static int tailLen;
static int snakeX, snakeY, fruitX, fruitY;
static int dir;           /* 0=stopped, 1=left, 2=right, 3=up, 4=down */
static int score;
static int gameover;

static void setup(void)
{
    gameover = 0;
    score = 0;
    tailLen = 0;
    snakeX = GW / 2;
    snakeY = GH / 2;
    dir = 0;
    fruitX = 1 + rand() % (GW - 2);
    fruitY = 1 + rand() % (GH - 2);
}

static void draw_border(void)
{
    int i;

    tui_color(TUI_WHITE, TUI_BLACK);
    tui_gotoxy(GOX - 1, GOY - 1);
    tui_putc(0xDA);                       /* ┌ */
    for (i = 0; i < GW; i++) tui_putc(0xC4);
    tui_putc(0xBF);                       /* ┐ */

    for (i = 0; i < GH; i++) {
        tui_gotoxy(GOX - 1, GOY + i);
        tui_putc(0xB3);                   /* │ */
        tui_gotoxy(GOX + GW, GOY + i);
        tui_putc(0xB3);
    }

    tui_gotoxy(GOX - 1, GOY + GH);
    tui_putc(0xC0);                       /* └ */
    for (i = 0; i < GW; i++) tui_putc(0xC4);
    tui_putc(0xD9);                       /* ┘ */
}

static void draw(void)
{
    int i, j;

    /* title bar */
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_gotoxy(1, 1);
    tui_puts("COBRA");
    {
        char buf[32];
        snprintf(buf, sizeof buf, "Score: %d", score);
        tui_gotoxy(80 - (int)strlen(buf) - 1, 1);
        tui_puts(buf);
    }

    draw_border();

    /* playfield contents */
    for (i = 0; i < GH; i++) {
        for (j = 0; j < GW; j++) {
            char c = ' ';
            int k;
            int is_fruit = 0;

            if (i == snakeY && j == snakeX) {
                c = 0xDB;                 /* █ head */
            } else if (i == fruitY && j == fruitX) {
                c = 0xB2;                 /* ▓ fruit */
                is_fruit = 1;
            } else {
                for (k = 0; k < tailLen; k++) {
                    if (tailX[k] >= 0 && tailX[k] == j && tailY[k] == i) {
                        c = 0xB2;         /* ▓ body */
                        break;
                    }
                }
            }

            tui_gotoxy(GOX + j, GOY + i);
            if (is_fruit) tui_color(TUI_YELLOW, TUI_BLACK);
            else          tui_color(TUI_WHITE, TUI_BLACK);
            tui_putc(c);
        }
    }

    /* footer hints */
    tui_color(TUI_LGRAY, TUI_BLACK);
    tui_gotoxy(1, GOY + GH + 2);
    tui_puts("Arrows or WASD to move    X to quit");
    tui_color(TUI_WHITE, TUI_BLACK);
}
static void handle_input(void)
{
    while (tui_key_pending()) {
        int c = tui_getch();
        int code;

        if (c == 0 || c == 224) {
            code = tui_getch();
            switch (code) {
            case 75: if (dir != 2) dir = 1; break;   /* left */
            case 77: if (dir != 1) dir = 2; break;   /* right */
            case 72: if (dir != 4) dir = 3; break;   /* up */
            case 80: if (dir != 3) dir = 4; break;   /* down */
            }
        } else {
            switch (c) {
            case 'a': case 'A': if (dir != 2) dir = 1; break;
            case 'd': case 'D': if (dir != 1) dir = 2; break;
            case 'w': case 'W': if (dir != 4) dir = 3; break;
            case 's': case 'S': if (dir != 3) dir = 4; break;
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

    /* wall collision */
    if (snakeX < 0 || snakeX >= GW || snakeY < 0 || snakeY >= GH)
        gameover = 1;

    /* self collision */
    for (i = 0; i < tailLen; i++) {
        if (tailX[i] == snakeX && tailY[i] == snakeY)
            gameover = 1;
    }

    /* eat fruit */
    if (snakeX == fruitX && snakeY == fruitY) {
    score += 10;
    if (tailLen > 0) {
        tailX[tailLen] = tailX[tailLen - 1];
        tailY[tailLen] = tailY[tailLen - 1];
    }
    tailLen++;
    fruitX = 1 + rand() % (GW - 2);
    fruitY = 1 + rand() % (GH - 2);
    }
}
int fb_snake(void)
{
    if (!tailX) {
        tailX = (int *)heap_caps_malloc(100 * sizeof(int), MALLOC_CAP_SPIRAM);
        tailY = (int *)heap_caps_malloc(100 * sizeof(int), MALLOC_CAP_SPIRAM);
        if (!tailX || !tailY) return 1;
    }

    tui_cursor(0, 0);
    tui_clear();

    setup();

    while (!gameover) {
        draw();
        handle_input();
        step();
        tui_delay(120);
    }

    tui_clear();
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_gotoxy(1, 1);
    tui_puts("Game Over");
    {
        char buf[32];
        snprintf(buf, sizeof buf, "Score: %d", score);
        tui_gotoxy(1, 3);
        tui_puts(buf);
    }
    tui_gotoxy(1, 5);
    tui_puts("Press any key to return to the editor.");
    tui_getch();

    tui_cursor(1, 0);
    tui_clear();
    return 0;
}
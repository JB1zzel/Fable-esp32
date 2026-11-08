/* mine_main.c - standalone launcher for the games menu.
 * Standalone test of the games menu (not part of the Fable build: it has its
 * own main()). From the Fable folder, in Windows CMD:
 *   tcc -Isrc tools\games-test\mine_main.c src\games.c src\mine.c src\sol.c src\snake.c src\tui_console.c src\file_win.c -luser32 -o games.exe
 */
#include <stdio.h>
#include "tui.h"
#include "fable.h"

int main(void)
{
    tui_init();
    tui_clear();

    /* keep showing the menu until Esc is pressed on it */
    while (fb_game_menu()) {
        tui_clear();
    }

    tui_restore();
    tui_clear();
    tui_gotoxy(1, 1);
    printf("Thanks for playing.\n");
    return 0;
}
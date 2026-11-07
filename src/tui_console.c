/* tui_console.c - Windows console backend for Fable.
 *
 * Uses Win32 console API for cursor positioning, colour, and input.
 * Handles the standard 80x25 (or whatever the console reports) character grid.
 *
 * Build with: tcc fableGL.c ... tui_console.c file_win.c -luser32 -o fable.exe
 */

#include "platform.h"

#ifdef FABLE_WINDOWS
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <string.h>
#include "tui.h"

#include <time.h>

struct tm *localtime_r(const time_t *t, struct tm *out)
{
    if (t && out) {
        localtime_s(out, t);
        return out;
    }
    return NULL;
}

static HANDLE hOut;
static HANDLE hIn;
static CONSOLE_SCREEN_BUFFER_INFO saved_info;
static int  raw_mode = 0;
static WORD saved_attr;

/* ---------- colour mapping ---------- */

static WORD tui_to_win_attr(int fg, int bg)
{
    /* TUI colour indices 0-15 map directly to Win32 FOREGROUND_* bits,
       except that 0-7 are the low-intensity set and 8-15 are high. */
    WORD fg_attr = 0, bg_attr = 0;

    if (fg < 0 || fg > 15) fg = 7;
    if (bg < 0 || bg > 15) bg = 0;

    /* foreground */
    if (fg & 1) fg_attr |= FOREGROUND_BLUE;
    if (fg & 2) fg_attr |= FOREGROUND_GREEN;
    if (fg & 4) fg_attr |= FOREGROUND_RED;
    if (fg & 8) fg_attr |= FOREGROUND_INTENSITY;

    /* background */
    if (bg & 1) bg_attr |= BACKGROUND_BLUE;
    if (bg & 2) bg_attr |= BACKGROUND_GREEN;
    if (bg & 4) bg_attr |= BACKGROUND_RED;
    if (bg & 8) bg_attr |= BACKGROUND_INTENSITY;

    return fg_attr | bg_attr;
}

/* ---------- lifecycle ---------- */

void tui_init(void)
{
    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    hIn  = GetStdHandle(STD_INPUT_HANDLE);

    GetConsoleScreenBufferInfo(hOut, &saved_info);
    saved_attr = saved_info.wAttributes;

    /* hide cursor */
    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(hOut, &ci);
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &ci);

    /* clear screen */
    {
        COORD origin = {0, 0};
        DWORD written;
        DWORD cells = (DWORD)saved_info.dwSize.X * saved_info.dwSize.Y;
        FillConsoleOutputCharacter(hOut, ' ', cells, origin, &written);
        FillConsoleOutputAttribute(hOut, tui_to_win_attr(TUI_LGRAY, TUI_BLACK),
                                   cells, origin, &written);
    }
    SetConsoleCursorPosition(hOut, (COORD){0, 0});

    raw_mode = 1;
}

void tui_restore(void)
{
    if (raw_mode) {
        SetConsoleTextAttribute(hOut, saved_attr);
        CONSOLE_CURSOR_INFO ci;
        GetConsoleCursorInfo(hOut, &ci);
        ci.bVisible = TRUE;
        SetConsoleCursorInfo(hOut, &ci);
        raw_mode = 0;
    }
}

/* ---------- screen ---------- */

void tui_clear(void)
{
    COORD origin = {0, 0};
    DWORD written;
    DWORD cells = (DWORD)saved_info.dwSize.X * saved_info.dwSize.Y;
    FillConsoleOutputCharacter(hOut, ' ', cells, origin, &written);
    FillConsoleOutputAttribute(hOut, tui_to_win_attr(TUI_LGRAY, TUI_BLACK),
                               cells, origin, &written);
    SetConsoleCursorPosition(hOut, origin);
}

void tui_gotoxy(int x, int y)
{
    COORD c;
    c.X = (SHORT)(x - 1);
    c.Y = (SHORT)(y - 1);
    if (c.X < 0) c.X = 0;
    if (c.Y < 0) c.Y = 0;
    SetConsoleCursorPosition(hOut, c);
}

void tui_color(int fg, int bg)
{
    SetConsoleTextAttribute(hOut, tui_to_win_attr(fg, bg));
}

void tui_putc(int c)
{
    DWORD written;
    WriteConsoleA(hOut, (const char *)&c, 1, &written, NULL);
}

void tui_puts(const char *s)
{
    DWORD written;
    WriteConsoleA(hOut, s, (DWORD)strlen(s), &written, NULL);
}

void tui_cursor(int visible, int block)
{
    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(hOut, &ci);
    ci.bVisible = visible ? TRUE : FALSE;
    if (block) ci.dwSize = 100; else ci.dwSize = 25;
    SetConsoleCursorInfo(hOut, &ci);
}

/* Frame buffer API — not needed on Windows, just pass through. */

void tui_frame_begin(void) { }

void tui_frame_put(const char *s, int n)
{
    DWORD written;
    WriteConsoleA(hOut, s, (DWORD)n, &written, NULL);
}

void tui_frame_end(void) { }

void tui_boot_colors(int delay_ms) { (void)delay_ms; }

/* ---------- keyboard ---------- */

int tui_getch(void)
{
    int c = _getch();
    if (c == 0 || c == 224) {
        /* extended key: return the 0/224 and let the caller read the next byte */
        return c;
    }
    return c;
}

int tui_key_pending(void)
{
    return _kbhit() ? 1 : 0;
}

int tui_shift_down(void)
{
    return (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 1 : 0;
}

/* ---------- platform stubs ---------- */

unsigned long tui_millis(void)
{
    return (unsigned long)GetTickCount();
}

void tui_delay(int ms)
{
    Sleep((DWORD)ms);
}

int tui_list_dir(const char *path, char names[][32], int max)
{
    /* unused on Windows — the Filer uses fab_dir_entries in file_win.c */
    (void)path; (void)names; (void)max;
    return 0;
}

/* Folder that holds fable.exe, with a trailing backslash ("C:\\tools\\fable\\").
 * The dictionary is looked up here, so it is found wherever Fable is
 * started from. Returns "" if the folder cannot be determined (the default
 * "/dict.smd" in the current folder is then used). */
void tui_exe_dir(char *out, int len)
{
    char path[MAX_PATH];
    DWORD n;
    char *slash;

    if (len > 0) out[0] = '\0';
    n = GetModuleFileNameA(NULL, path, sizeof path);
    if (n == 0 || n >= sizeof path) return;
    slash = strrchr(path, '\\');
    if (!slash) return;
    slash[1] = '\0';                         /* keep the trailing backslash */
    if ((int)strlen(path) < len) strcpy(out, path);
}

#else
typedef int tui_console_c_is_windows_only;   /* keeps this file from being empty */
#endif

/* calendar.c - month grid view for Fable's diary (Z88 Filer tribute)
 *
 * Draws a month calendar on the TUI, lets the user navigate days/months,
 * and returns the day number the user selected (or 0 if they pressed Esc).
 *
 * Nothing in this file knows about the editor, the clipboard, or files.
 * It talks only to tui.h.
 */

#include <stdio.h>
#include <string.h>
#include "tui.h"

#define CAL_COLS    7
#define CAL_CELL_W  4
#define CAL_GRID_X  6
#define CAL_GRID_Y  6

/* ---------- date helpers ---------- */

static int is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int m)
{
    static const int d[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1 || m > 12) return 0;
    if (m == 2 && is_leap(y)) return 29;
    return d[m];
}

/* 0 = Sunday, 1 = Monday, ... 6 = Saturday */
static int day_of_week(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

/* month arithmetic that wraps year */
static void prev_month(int *y, int *m)
{
    (*m)--;
    if (*m < 1) { *m = 12; (*y)--; }
}

static void next_month(int *y, int *m)
{
    (*m)++;
    if (*m > 12) { *m = 1; (*y)++; }
}

/* ---------- drawing ---------- */

static const char *month_name(int m)
{
    static const char *names[] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    if (m < 1 || m > 12) return "";
    return names[m];
}

static void draw_calendar(int year, int month, int sel_day, int today)
{
    int first_dow, d, row, col, dim;
    char line[80];

    tui_clear();
    tui_color(TUI_WHITE, TUI_BLACK);

    /* title */
    tui_gotoxy(1, 2);
    snprintf(line, sizeof line, "%s %d", month_name(month), year);
    tui_puts(line);

    /* weekday header, Monday first */
    tui_gotoxy(CAL_GRID_X, CAL_GRID_Y - 1);
    tui_puts("Mon Tue Wed Thu Fri Sat Sun");

    /* first day-of-week for this month, Monday = 0 */
    first_dow = day_of_week(year, month, 1);
    first_dow = (first_dow + 6) % 7;    /* 0=Sun -> shift to 0=Mon */

    dim = days_in_month(year, month);
    row = CAL_GRID_Y;
    col = first_dow;

    for (d = 1; d <= dim; d++) {
        char cell[8];
        snprintf(cell, sizeof cell, "%3d", d);
        tui_gotoxy(CAL_GRID_X + col * CAL_CELL_W, row);

        if (d == sel_day) {
            tui_color(TUI_BLACK, TUI_LGRAY);
            tui_puts(cell);
            tui_color(TUI_WHITE, TUI_BLACK);
        } else if (d == today) {
            tui_color(TUI_YELLOW, TUI_BLACK);
            tui_puts(cell);
            tui_color(TUI_WHITE, TUI_BLACK);
        } else {
            tui_puts(cell);
        }

        col++;
        if (col >= CAL_COLS) { col = 0; row++; }
    }

    /* footer */
    tui_gotoxy(1, CAL_GRID_Y + 8);
    tui_puts("Arrows: move    PgUp/PgDn: month");
    tui_gotoxy(1, CAL_GRID_Y + 9);
    tui_puts("Enter: open day    Esc: back");
}

/* ---------- main entry ---------- */

/* Returns the day number (1..31) the user chose, or 0 to cancel.
 * year/month are in/out: on entry they are the month to show,
 * on return they are the month the user ended on (so the caller
 * knows which month the chosen day belongs to). */
int calendar_show(const char *boot_time, char *out_path, int out_len)
{
    int year = 2026, month = 1, today = 1;
    int sel, dim, c, code;

    /* parse boot_time = "YYYY-MM-DD HH:MM" */
    if (boot_time && boot_time[0]) {
        year  = (boot_time[0]-'0')*1000 + (boot_time[1]-'0')*100
              + (boot_time[2]-'0')*10   + (boot_time[3]-'0');
        month = (boot_time[5]-'0')*10   + (boot_time[6]-'0');
        today = (boot_time[8]-'0')*10   + (boot_time[9]-'0');
    }
    if (month < 1 || month > 12) month = 1;

    dim = days_in_month(year, month);
    if (today < 1 || today > dim) today = 1;
    sel = today;

    for (;;) {
        draw_calendar(year, month, sel, today);
        c = tui_getch();
        if (c == 27) return 0;                       /* Esc */
        if (c == 13) {                               /* Enter */
            snprintf(out_path, out_len, "/diary/%04d-%02d-%02d.txt",
                     year, month, sel);
            return 1;
        }
        if (c == 0 || c == 224) {
            code = tui_getch();
            switch (code) {
            case 72:                                 /* Up */
                sel -= 7;
                while (sel < 1) {
                    prev_month(&year, &month);
                    sel += days_in_month(year, month);
                }
                break;
            case 80:                                 /* Down */
                sel += 7;
                while (sel > days_in_month(year, month)) {
                    sel -= days_in_month(year, month);
                    next_month(&year, &month);
                }
                break;
            case 75:                                 /* Left */
                sel--;
                if (sel < 1) {
                    prev_month(&year, &month);
                    sel = days_in_month(year, month);
                }
                break;
            case 77:                                 /* Right */
                sel++;
                if (sel > days_in_month(year, month)) {
                    next_month(&year, &month);
                    sel = 1;
                }
                break;
            case 73:                                 /* PgUp */
                prev_month(&year, &month);
                dim = days_in_month(year, month);
                if (sel > dim) sel = dim;
                break;
            case 81:                                 /* PgDn */
                next_month(&year, &month);
                dim = days_in_month(year, month);
                if (sel > dim) sel = dim;
                break;
            }
        }
    }
}
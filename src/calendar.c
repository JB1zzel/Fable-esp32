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
#include <ctype.h>
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

/* ty/tm/td = today's date; the "today" mark is only drawn when the
 * displayed month is that month (td == 0 means clock unknown). */
static void draw_calendar(int year, int month, int sel_day,
                          int ty, int tm, int td)
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
        } else if (year == ty && month == tm && d == td) {
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

/* "YYYY-MM-DD..." -> y/m/d. Returns 1 if it looks valid. */
static int parse_date(const char *s, int *y, int *m, int *d)
{
    int i;
    if (!s || strlen(s) < 10) return 0;
    for (i = 0; i < 10; i++) {
        if (i == 4 || i == 7) { if (s[i] != '-') return 0; }
        else if (!isdigit((unsigned char)s[i])) return 0;
    }
    *y = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
    *m = (s[5]-'0')*10 + (s[6]-'0');
    *d = (s[8]-'0')*10 + (s[9]-'0');
    if (*m < 1 || *m > 12) return 0;
    if (*d < 1 || *d > days_in_month(*y, *m)) return 0;
    return 1;
}

/* Fallback when the clock is not synced: the firmware's build date
 * (__DATE__ is "Mmm dd yyyy"). */
static void build_date(int *y, int *m)
{
    static const char *mon = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__;
    int i;
    *m = 1;
    for (i = 0; i < 12; i++)
        if (strncmp(d, mon + i * 3, 3) == 0) { *m = i + 1; break; }
    *y = (d[7]-'0')*1000 + (d[8]-'0')*100 + (d[9]-'0')*10 + (d[10]-'0');
}

/* now = "YYYY-MM-DD HH:MM" from the system clock, or "" if not synced.
 * Returns 1 and fills out_path with /diary/YYYY-MM-DD.txt if a day was
 * chosen, 0 if the user pressed Esc. */
int calendar_show(const char *now, char *out_path, int out_len)
{
    int year, month, ty = 0, tm = 0, td = 0;
    int sel, dim, c, code;

    if (parse_date(now, &ty, &tm, &td)) {
        year = ty; month = tm; sel = td;
    } else {
        ty = tm = td = 0;                 /* no "today" mark */
        build_date(&year, &month);
        sel = 1;
    }

    for (;;) {
        draw_calendar(year, month, sel, ty, tm, td);
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
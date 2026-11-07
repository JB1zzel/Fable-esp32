/* diary.c - diary file naming and day picker
 *
 * One diary file per day: YYYY-MM-DD.txt in the diary folder.
 *
 *   ESP32:   /diary/ on the SD card
 *   Windows: the "diary" folder next to fable.exe, so the diary is the same
 *            wherever Fable is started from
 *
 * This is the only place that knows where the files are and what they are
 * called; calendar.c and the editor both use it.
 */

#include <stdio.h>
#include <string.h>
#include "tui.h"
#include "fable.h"
#include "fab_file.h"

/* Diary folder with a trailing separator: "/diary/" or "C:\tools\fable\diary\".
 * If the exe folder cannot be found on Windows this is "diary\", i.e. a
 * folder in the current directory. */
static void diary_dir(char *out, int out_len)
{
#ifdef FABLE_WINDOWS
    char exe[260];
    tui_exe_dir(exe, sizeof exe);            /* "C:\tools\fable\" or "" */
    snprintf(out, out_len, "%sdiary\\", exe);
#else
    snprintf(out, out_len, "/diary/");
#endif
}

void diary_path(char *out, int out_len, int year, int month, int day)
{
    char dir[280];
    diary_dir(dir, sizeof dir);
    snprintf(out, out_len, "%s%04d-%02d-%02d.txt", dir, year, month, day);
}

/* Creates the diary folder if it is missing. */
void diary_init(void)
{
    char dir[280];
    size_t n;

    diary_dir(dir, sizeof dir);
    n = strlen(dir);
    if (n > 1) dir[n - 1] = '\0';             /* drop the trailing separator */
    fab_mkdir(dir);
}

/* Shows the calendar (starting on today when the clock is set) and returns
 * 1 with the chosen day's file path in out_path, 0 on Esc. */
int diary_pick(char *out_path, int out_len)
{
    char now[32];
    fb_current_time_str(now, sizeof now);
    return calendar_show(now, out_path, out_len);
}

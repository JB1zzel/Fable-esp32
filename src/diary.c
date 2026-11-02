/* diary.c - diary file naming and day picker
 *
 * One diary file per day: /diary/YYYY-MM-DD.txt. This is the only place
 * that knows the naming scheme; calendar.c and the editor both use it.
 */

#include <stdio.h>
#include "fable.h"

void diary_path(char *out, int out_len, int year, int month, int day)
{
    snprintf(out, out_len, "/diary/%04d-%02d-%02d.txt", year, month, day);
}

/* Shows the calendar (starting on today when the clock is synced) and
 * returns 1 with the chosen day's file path in out_path, 0 on Esc. */
int diary_pick(char *out_path, int out_len)
{
    char now[32];
    fb_current_time_str(now, sizeof now);
    return calendar_show(now, out_path, out_len);
}

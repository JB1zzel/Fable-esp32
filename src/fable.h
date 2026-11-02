/* fable.h - constants and cross-file prototypes for Fable.
 *
 * Editor state (lines, cursor, filename...) is still private to fableGL.c;
 * everything declared here is independent of it.
 */
#ifndef FABLE_H
#define FABLE_H

#define MAXLINES 256
#define MAXLINE  1024
#define SCRW     80
#define SCRH     55   /* 55 text rows + 1 status row = 56 total (see FabGL DMA reserve) */

/* prompt.c - single-line input on the current cursor position.
 * Return 0 on Enter, -1 on Esc (dest is then empty). */
int  fb_prompt_line(const char *prompt, char *dest, int maxlen);
int  fb_prompt_line_edit(char *dest, int initial_len, int maxlen);

/* help.c */
void fb_help_screen(void);

/* filer.c - interactive file picker.
 * Returns 1 and fills out_path with the chosen file's path.
 * Returns 0 on Esc. */
int  fb_show_files(char *out_path, int out_len);
/* clock.c */
void fb_current_time_str(char *out, int out_len);   /* "YYYY-MM-DD HH:MM" or "" */
void fb_wifi_setup_page(void);
void set_boot_wifi_ssid(const char *s);

/* calendar.c */
int  calendar_show(const char *now, char *out_path, int out_len);

/* diary.c */
void diary_path(char *out, int out_len, int year, int month, int day);
int  diary_pick(char *out_path, int out_len);   /* 1 = a day was chosen */

#endif

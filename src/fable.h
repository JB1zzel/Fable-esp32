/* fable.h - constants and cross-file prototypes for Fable.
 *
 * Editor state (lines, cursor, filename...) is still private to fableGL.c;
 * everything declared here is independent of it.
 */
#ifndef FABLE_H
#define FABLE_H

#include "platform.h"

#define MAXLINES 256
#define MAXLINE  1024
#define SCRW     80
#ifdef FABLE_ESP32
#define SCRH     59
#else
#define SCRH     24
#endif

/* games (F7 opens the menu) */
int fb_game_menu(void);   /* 1 = show the menu again, 0 = back to the editor */
int fb_snake(void);
int fb_minesweeper(void);
int fb_solitaire(void);
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
#ifdef FABLE_ESP32
void fb_wifi_setup_page(void);
void set_boot_wifi_ssid(const char *s);
#endif

/* calendar.c */
int  calendar_show(const char *now, char *out_path, int out_len);

/* diary.c */
void diary_path(char *out, int out_len, int year, int month, int day);
int  diary_pick(char *out_path, int out_len);   /* 1 = a day was chosen */
void diary_init(void);                          /* create the diary folder */

#endif

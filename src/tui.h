/* tui.h - terminal UI abstraction for the Z88-like suite
 *
 * Two backends exist:
 *   tui_console.c  - Windows CMD / ANSI terminal (for testing on PC)
 *   tui_fabgl.cpp  - ESP32 / FabGL (for the TTGO VGA32)
 *
 * Apps include this header and nothing else console-related.
 */

#ifndef TUI_H
#define TUI_H

#ifdef __cplusplus
extern "C" {
#endif
//snake
void tui_delay(int ms);
/* Colour indices, IBM-PC / ANSI compatible (0-15) */
#define TUI_BLACK     0
#define TUI_BLUE      1
#define TUI_GREEN     2
#define TUI_CYAN      3
#define TUI_RED       4
#define TUI_MAGENTA   5
#define TUI_BROWN     6
#define TUI_LGRAY     7
#define TUI_DGRAY     8
#define TUI_LBLUE     9
#define TUI_LGREEN    10
#define TUI_LCYAN     11
#define TUI_LRED      12
#define TUI_LMAGENTA  13
#define TUI_YELLOW    14
#define TUI_WHITE     15

/* Lifecycle */
void tui_init(void);        /* enter full-screen mode */
void tui_restore(void);     /* undo init; hand terminal back to the shell */

/* Screen */

void tui_frame_begin(void);
void tui_frame_put(const char *s, int n);
void tui_frame_end(void);

void tui_clear(void);
void tui_gotoxy(int x, int y);          /* 1-based */
void tui_color(int fg, int bg);
void tui_putc(int c);
void tui_puts(const char *s);
void tui_cursor(int visible, int block); /* block=1 solid, block=0 underline */
void tui_boot_colors(int delay_ms);
/* wifi */
void set_boot_wifi_ssid(const char *s);

int tui_wifi_scan(char names[][33], int max);
int tui_wifi_save(const char *ssid, const char *pass);
int tui_wifi_load(char *ssid, int ssid_len, char *pass, int pass_len);
/* Input */
int  tui_getch(void);       /* blocking; returns key code, or -1 on EOF */
int  tui_shift_down(void);  /* 1 if Shift is currently held */
int tui_key_pending(void);
int tui_list_dir(const char *path, char names[][32], int max);
/* Platform */
void tui_exe_dir(char *out, int len);   /* folder containing the exe, trailing slash; "" on ESP32 */
unsigned long tui_millis(void);
#ifdef __cplusplus

}
#endif

#endif
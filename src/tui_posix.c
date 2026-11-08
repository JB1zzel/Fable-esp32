/* tui_posix.c - Linux (and other POSIX) terminal backend for Fable.
 *
 * Draws with ANSI escape sequences and reads keys in raw mode, so it works in
 * any ordinary terminal (xterm, gnome-terminal, konsole, the Linux console,
 * ssh, tmux...).
 *
 * The editor was written for a PC text screen (code page 437) and the
 * Windows console keys, so this file translates in both directions:
 *   - bytes 128..255 written to the screen are shown as the CP437 glyph
 *     (box drawing, shades, accented letters), sent to the terminal as UTF-8;
 *   - UTF-8 typed on the keyboard is stored as the matching CP437 byte;
 *   - escape sequences from the keyboard (arrows, F-keys...) become the same
 *     two-value keys the Windows console gives: 0, then a scan code.
 */

/* feature macros must come before the first system header */
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE 1
#endif

#include "platform.h"

#ifdef FABLE_POSIX

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "tui.h"

/* ---------- code page 437 -> Unicode ---------- */

static const unsigned short cp437_hi[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

static int cp437_from_unicode(unsigned cp)
{
    int i;
    if (cp < 128) return (int)cp;
    for (i = 0; i < 128; i++)
        if (cp437_hi[i] == cp) return 128 + i;
    return '?';
}

/* ---------- buffered output ---------- */

static char   obuf[1 << 16];
static size_t on;

static void flush_out(void)
{
    size_t off = 0;
    while (off < on) {
        ssize_t n = write(STDOUT_FILENO, obuf + off, on - off);
        if (n <= 0) break;
        off += (size_t)n;
    }
    on = 0;
}

static void out_raw(const char *s, size_t n)
{
    if (on + n > sizeof obuf) flush_out();
    if (n > sizeof obuf) { (void)!write(STDOUT_FILENO, s, n); return; }
    memcpy(obuf + on, s, n);
    on += n;
}

static void out_str(const char *s) { out_raw(s, strlen(s)); }

/* one screen byte: ASCII as is, 128..255 as the CP437 glyph in UTF-8 */
static void out_byte(unsigned char c)
{
    if (c < 128) {
        out_raw((const char *)&c, 1);
    } else {
        unsigned cp = cp437_hi[c - 128];
        char u[4];
        int n;
        if (cp < 0x800) {
            u[0] = (char)(0xC0 | (cp >> 6));
            u[1] = (char)(0x80 | (cp & 0x3F));
            n = 2;
        } else {
            u[0] = (char)(0xE0 | (cp >> 12));
            u[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            u[2] = (char)(0x80 | (cp & 0x3F));
            n = 3;
        }
        out_raw(u, (size_t)n);
    }
}

/* ---------- lifecycle ---------- */

static struct termios saved_tio;
static int  have_tio = 0;
static int  raw_mode = 0;

static void on_term(int sig)
{
    (void)sig;
    tui_restore();
    _exit(1);
}

void tui_restore(void)
{
    if (!raw_mode) return;
    out_str("\x1b[0m\x1b[0 q\x1b[?25h\x1b[?1049l");
    flush_out();
    if (have_tio) tcsetattr(STDIN_FILENO, TCSANOW, &saved_tio);
    raw_mode = 0;
}

static int  q[16], qh, qt;              /* decoded keys waiting to be returned */
static int  shift_flag;

static int read_byte(int timeout_ms);

void tui_init(void)
{
    struct termios t;
    struct winsize ws;

    if (tcgetattr(STDIN_FILENO, &saved_tio) == 0) {
        have_tio = 1;
        t = saved_tio;
        /* no echo, no line editing, no signals, no flow control (Ctrl+S is
         * Save!), Enter arrives as 13 */
        t.c_iflag &= (tcflag_t)~(IXON | IXOFF | ICRNL | INLCR | IGNCR | ISTRIP |
                                 BRKINT | PARMRK | INPCK);
        t.c_lflag &= (tcflag_t)~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
        t.c_cflag |= CS8;
        t.c_cc[VMIN]  = 1;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    raw_mode = 1;
    atexit(tui_restore);
    signal(SIGTERM, on_term);
    signal(SIGHUP,  on_term);

    out_str("\x1b[?1049h\x1b[37;40m\x1b[2J\x1b[H\x1b[?25l");
    flush_out();

    /* the games need 80x26; ask for a bigger window if this one is small */
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 &&
        (ws.ws_col < 80 || ws.ws_row < 26)) {
        char msg[160];
        snprintf(msg, sizeof msg,
                 "Fable needs a terminal of at least 80 x 26 (this one is %d x %d).",
                 ws.ws_col, ws.ws_row);
        out_str("\x1b[1;1H");
        out_str(msg);
        out_str("\x1b[2;1HEnlarge the window, then press a key.");
        flush_out();
        tui_getch();
        out_str("\x1b[2J\x1b[H");
        flush_out();
    }
}

/* ---------- screen ---------- */

void tui_clear(void)
{
    out_str("\x1b[37;40m\x1b[2J\x1b[H");
}

void tui_gotoxy(int x, int y)
{
    char b[32];
    if (x < 1) x = 1;
    if (y < 1) y = 1;
    snprintf(b, sizeof b, "\x1b[%d;%dH", y, x);
    out_str(b);
}

/* TUI colours are IBM-PC numbers: bit 0 blue, 1 green, 2 red, 3 bright.
 * ANSI numbers its colours red=1, green=2, blue=4. */
static int ansi_of(int c)
{
    return ((c & 4) ? 1 : 0) | ((c & 2) ? 2 : 0) | ((c & 1) ? 4 : 0);
}

void tui_color(int fg, int bg)
{
    char b[32];
    if (fg < 0 || fg > 15) fg = 7;
    if (bg < 0 || bg > 15) bg = 0;
    snprintf(b, sizeof b, "\x1b[%d;%dm",
             ((fg & 8) ? 90 : 30) + ansi_of(fg),
             ((bg & 8) ? 100 : 40) + ansi_of(bg));
    out_str(b);
}

void tui_putc(int c)
{
    out_byte((unsigned char)c);
}

void tui_puts(const char *s)
{
    while (*s) out_byte((unsigned char)*s++);
}

void tui_cursor(int visible, int block)
{
    out_str(visible ? "\x1b[?25h" : "\x1b[?25l");
    if (visible) out_str(block ? "\x1b[2 q" : "\x1b[4 q");
}

void tui_frame_begin(void) { }

void tui_frame_put(const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++) out_byte((unsigned char)s[i]);
}

void tui_frame_end(void) { flush_out(); }

void tui_boot_colors(int delay_ms) { (void)delay_ms; }

/* ---------- keyboard ---------- */

static int read_byte(int timeout_ms)
{
    struct pollfd p;
    unsigned char c;
    p.fd = STDIN_FILENO;
    p.events = POLLIN;
    p.revents = 0;
    if (poll(&p, 1, timeout_ms) <= 0) return -1;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;
    return c;
}

static void push(int v)
{
    if (((qt + 1) & 15) != qh) { q[qt] = v; qt = (qt + 1) & 15; }
}

static int pop(void)
{
    int v = q[qh];
    qh = (qh + 1) & 15;
    return v;
}

/* a special key: Windows console style, 0 first and the scan code next */
static int special(int code)
{
    push(code);
    return 0;
}

/* After ESC [ or ESC O: read the rest and turn it into a scan code. */
static int parse_escape(int intro)
{
    int p1 = 0, p2 = 0, which = 0, c;

    for (;;) {
        c = read_byte(40);
        if (c < 0) return 27;                       /* cut short: just Esc */
        if (c >= '0' && c <= '9') {
            if (which == 0) p1 = p1 * 10 + (c - '0');
            else            p2 = p2 * 10 + (c - '0');
        } else if (c == ';') {
            which = 1;
        } else if (c == '[' && intro == '[') {
            /* Linux console: ESC [ [ A..E are F1..F5 */
            c = read_byte(40);
            if (c >= 'A' && c <= 'E') return special(59 + (c - 'A'));
            return -1;
        } else {
            break;                                  /* final byte */
        }
    }

    if (p2 == 2) shift_flag = 1;                    /* ESC [ 1 ; 2 A = Shift+Up */

    if (intro == 'O') {
        switch (c) {
        case 'P': return special(59);  case 'Q': return special(60);
        case 'R': return special(61);  case 'S': return special(62);
        case 'A': return special(72);  case 'B': return special(80);
        case 'C': return special(77);  case 'D': return special(75);
        case 'H': return special(71);  case 'F': return special(79);
        }
        return -1;
    }

    switch (c) {
    case 'A': return special(72);                   /* up    */
    case 'B': return special(80);                   /* down  */
    case 'C': return special(77);                   /* right */
    case 'D': return special(75);                   /* left  */
    case 'H': return special(71);                   /* home  */
    case 'F': return special(79);                   /* end   */
    case 'P': return special(59);                   /* F1 (some terminals) */
    case 'Q': return special(60);
    case 'R': return special(61);
    case 'S': return special(62);
    case '~':
        switch (p1) {
        case 1: case 7:  return special(71);        /* home */
        case 2:          return special(82);        /* insert */
        case 3:          return special(83);        /* delete */
        case 4: case 8:  return special(79);        /* end */
        case 5:          return special(73);        /* page up */
        case 6:          return special(81);        /* page down */
        case 11: return special(59);  case 12: return special(60);   /* F1..F5 */
        case 13: return special(61);  case 14: return special(62);
        case 15: return special(63);
        case 17: return special(64);  case 18: return special(65);   /* F6..F8 */
        case 19: return special(66);
        case 20: return special(67);  case 21: return special(68);   /* F9, F10 */
        }
        break;
    }
    return -1;                                      /* unknown: ignore */
}

int tui_getch(void)
{
    int c;

    flush_out();

    for (;;) {
        if (qh != qt) return pop();

        c = read_byte(-1);
        if (c < 0) continue;                        /* interrupted: wait again */
        shift_flag = 0;

        if (c == 27) {
            int n = read_byte(40);                  /* lone Esc, or a sequence? */
            if (n < 0) return 27;
            if (n == '[' || n == 'O') {
                int r = parse_escape(n);
                if (r == 0) return 0;               /* special key: 0, then its code */
                if (r == 27) return 27;
                continue;                           /* unknown sequence: ignore */
            }
            push(n);                                /* Alt+key: Esc, then the key */
            return 27;
        }
        if (c == 127) return 8;                     /* Backspace */
        if (c == 10)  return 13;                    /* Enter */

        if (c >= 0xC0) {                            /* UTF-8: store as CP437 */
            int more = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
            unsigned cp = (unsigned)(c & (0x3F >> more));
            while (more-- > 0) {
                int d = read_byte(40);
                if (d < 0 || (d & 0xC0) != 0x80) break;
                cp = (cp << 6) | (unsigned)(d & 0x3F);
            }
            return cp437_from_unicode(cp);
        }
        if (c >= 0x80) continue;                    /* stray continuation byte */
        return c;
    }
}

int tui_key_pending(void)
{
    struct pollfd p;
    if (qh != qt) return 1;
    flush_out();
    p.fd = STDIN_FILENO;
    p.events = POLLIN;
    p.revents = 0;
    return poll(&p, 1, 0) > 0 ? 1 : 0;
}

int tui_shift_down(void)
{
    return shift_flag;
}

/* ---------- platform helpers ---------- */

unsigned long tui_millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

void tui_delay(int ms)
{
    flush_out();
    if (ms > 0) {
        struct timespec ts;
        ts.tv_sec  = ms / 1000;
        ts.tv_nsec = (long)(ms % 1000) * 1000000L;
        nanosleep(&ts, NULL);
    }
}

int tui_list_dir(const char *path, char names[][32], int max)
{
    /* unused here: the Filer uses fab_dir_entries in file_posix.c */
    (void)path; (void)names; (void)max;
    return 0;
}

/* Folder that holds the fable program, e.g. "//home/me/fable/". The leading
 * "//" marks an absolute path for file_posix.c (a single "/" means the folder
 * Fable was started in). "" if it cannot be found. */
void tui_exe_dir(char *out, int len)
{
    char path[1024];
    ssize_t n;
    char *slash;

    if (len > 0) out[0] = '\0';
    n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n <= 0) return;
    path[n] = '\0';
    slash = strrchr(path, '/');
    if (!slash) return;
    slash[1] = '\0';
    if ((int)strlen(path) + 2 < len) {
        out[0] = '/';
        strcpy(out + 1, path);
    }
}

#else
typedef int tui_posix_is_posix_only;    /* keeps this file from being empty */
#endif

/* filer.c - single-pane file picker for Fable. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tui.h"
#include "fable.h"

#include "fab_file.h"

#define MAX_ENTRIES  512
#define LIST_TOP     4
#define LIST_BOTTOM  (SCRH - 4)         /* 51 when SCRH is 55 */
#define VISIBLE_ROWS (LIST_BOTTOM - LIST_TOP + 1)

#define MARK_ROOT    '.'
#define MARK_DIR     '>'

#define SHADE_FULL   0xB2
#define SHADE_EMPTY  0xB0
#define BOX_TL       0xDA
#define BOX_TR       0xBF
#define BOX_BL       0xC0
#define BOX_BR       0xD9
#define BOX_H        0xC4
#define BOX_V        0xB3
#define BOX_LT       0xC3
#define BOX_RT       0xB4

typedef struct {
    char        path[260];
    FabDirEntry entries[MAX_ENTRIES];
    int         count;
    int         more;           /* 1 = folder has entries beyond MAX_ENTRIES */
    int         cursor;
    int         scroll;
    uint64_t    card_total;     /* cached: SD.usedBytes() walks the FAT, */
    uint64_t    card_used;      /* so it is read once, not on every redraw */
} FilerState;

static void filer_card_refresh(FilerState *s)
{
    s->card_total = fab_card_total();
    s->card_used  = fab_card_used();
}

/* FAT does not allow these in names; also keeps names inside the list. */
static int name_ok(const char *n)
{
    const char *bad = "\\/:*?\"<>|";
    size_t i;
    if (!n[0] || strcmp(n, ".") == 0 || strcmp(n, "..") == 0) return 0;
    if (strlen(n) > 100) return 0;
    for (i = 0; n[i]; i++)
        if (strchr(bad, n[i]) || (unsigned char)n[i] < 32) return 0;
    return 1;
}

/* case-insensitive match against the listed entries (FAT ignores case) */
static int name_in_use(const FilerState *s, const char *n, int dirs_only)
{
    int i;
    for (i = 0; i < s->count; i++) {
        const char *a = s->entries[i].name;
        if (dirs_only && !s->entries[i].is_dir) continue;
        const char *b = n;
        while (*a && *b && ((*a | 32) == (*b | 32))) { a++; b++; }
        if (!*a && !*b) return 1;
    }
    return 0;
}

/* path helpers */
static void filer_descend(FilerState *s, const char *name)
{
    size_t len = strlen(s->path);
    if (len >= sizeof(s->path) - 2) return;
    if (len > 1) snprintf(s->path + len, sizeof(s->path) - len, "%s/", name);
    else         snprintf(s->path + 1, sizeof(s->path) - 1, "%s/", name);
}

#ifdef FABLE_WINDOWS
/* On Windows "/" is the folder Fable was started in, and it is not the top
 * of anything: the user may walk above it. Going above it turns the path
 * into "/../", "/../../" ... which file_win.c maps to "..\", "..\..\".
 * True for "/", "/../", "/../../" (only ".." segments). */
static int path_is_only_dotdots(const char *p)
{
    while (*p == '/') p++;
    while (*p) {
        if (p[0] != '.' || p[1] != '.' || (p[2] != '/' && p[2] != '\0')) return 0;
        p += 2;
        while (*p == '/') p++;
    }
    return 1;
}
#endif

static void filer_go_up(FilerState *s)
{
    size_t len = strlen(s->path);
#ifdef FABLE_WINDOWS
    if (path_is_only_dotdots(s->path)) {
        if (len + 4 < sizeof s->path) {
            if (len == 0 || s->path[len - 1] != '/') strcat(s->path, "/");
            strcat(s->path, "../");
        }
        return;
    }
#endif
    if (len <= 1) return;
    if (s->path[len - 1] == '/') len--;
    while (len > 0 && s->path[len - 1] != '/') len--;
    if (len == 0) strcpy(s->path, "/");
    else          s->path[len] = '\0';
}

/* Enter on a folder entry: ".." goes up, anything else goes down. */
static void filer_enter_dir(FilerState *s, const char *name)
{
    if (strcmp(name, "..") == 0) filer_go_up(s);
    else                         filer_descend(s, name);
}

static void filer_rescan(FilerState *s)
{
    s->count = fab_dir_entries(s->path, s->entries, MAX_ENTRIES, &s->more);
#ifdef FABLE_WINDOWS
    /* a ".." entry at the top of every list, so folders (and the way back
     * up) are always visible */
    if (s->count < MAX_ENTRIES) {
        memmove(&s->entries[1], &s->entries[0], (size_t)s->count * sizeof(FabDirEntry));
        memset(&s->entries[0], 0, sizeof(FabDirEntry));
        strcpy(s->entries[0].name, "..");
        s->entries[0].is_dir = 1;
        s->count++;
    }
    /* start on the first real entry, so Enter picks a file as before;
     * ".." is one Up key away */
    if (s->cursor == 0 && s->count > 1 && strcmp(s->entries[0].name, "..") == 0)
        s->cursor = 1;
#endif
    if (s->cursor >= s->count) s->cursor = s->count > 0 ? s->count - 1 : 0;
    if (s->cursor < 0) s->cursor = 0;
    if (s->scroll > s->cursor) s->scroll = s->cursor;
    if (s->cursor >= s->scroll + VISIBLE_ROWS)
        s->scroll = s->cursor - VISIBLE_ROWS + 1;
}

/* formatting */
static void fmt_size(char *out, int out_len, uint32_t size)
{
    if (size < 1024)
        snprintf(out, out_len, "%lu", (unsigned long)size);
    else if (size < 1024UL * 1024UL)
        snprintf(out, out_len, "%lu.%luK",
                 (unsigned long)(size / 1024),
                 (unsigned long)((size * 10 / 1024) % 10));
    else
        snprintf(out, out_len, "%lu.%luM",
                 (unsigned long)(size / (1024UL * 1024UL)),
                 (unsigned long)((size * 10 / (1024UL * 1024UL)) % 10));
}

static void fmt_gb(char *out, int out_len, uint64_t bytes)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        snprintf(out, out_len, "%lu.%luG",
                 (unsigned long)(bytes / (1024ULL * 1024ULL * 1024ULL)),
                 (unsigned long)((bytes * 10 / (1024ULL * 1024ULL * 1024ULL)) % 10));
    else
        snprintf(out, out_len, "%luM",
                 (unsigned long)(bytes / (1024ULL * 1024ULL)));
}

/* simple breadcrumb */
static void format_breadcrumb(const char *path, char *out, int out_len)
{
    int bi = 0;
    const char *p;

    if (!path || !path[0] || (path[0] == '/' && path[1] == '\0')) {
        snprintf(out, out_len, "%c", MARK_ROOT);
        return;
    }

    out[bi++] = MARK_ROOT;
    out[bi++] = ' ';
    p = path;
    while (*p && *p == '/') p++;
    while (*p && bi < out_len - 4) {
        if (*p == '/') {
            if (p[1] == '\0') break;
            out[bi++] = ' ';
            out[bi++] = MARK_DIR;
            out[bi++] = ' ';
            p++;
        } else {
            out[bi++] = *p++;
        }
    }
    out[bi] = '\0';
}

/* drawing */
static void draw_hline(int row, int left_char, int right_char)
{
    int i;
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_gotoxy(1, row);
    tui_putc(left_char);
    for (i = 0; i < SCRW - 2; i++) tui_putc(BOX_H);
    tui_putc(right_char);
}

static void draw_entry(FilerState *s, int idx, int row)
{
    FabDirEntry *e = &s->entries[idx];
    char sz[16];
    int name_len;
    int selected = (idx == s->cursor);
    int i;

    tui_color(TUI_WHITE, TUI_BLACK);
    tui_gotoxy(1, row);
    tui_putc(BOX_V);

    tui_gotoxy(2, row);
    for (i = 0; i < SCRW - 2; i++) tui_putc(' ');

    fmt_size(sz, sizeof sz, e->size);
    {
        /* longest name that still leaves room for the size column */
        int sz_col  = SCRW - 3 - (int)strlen(sz);
        int name_max = sz_col - 6;
        char shown[SCRW];
        if (name_max > (int)sizeof shown - 1) name_max = (int)sizeof shown - 1;
        if (name_max < 1) name_max = 1;
        snprintf(shown, sizeof shown, "%.*s", name_max, e->name);

        if (e->is_dir) {
            tui_gotoxy(3, row);
            if (selected) tui_color(TUI_BLACK, TUI_LGRAY);
            tui_putc(MARK_DIR);
            tui_putc(' ');
            tui_puts(shown);
            name_len = (int)strlen(shown) + 2;
        } else {
            tui_gotoxy(5, row);
            if (selected) tui_color(TUI_BLACK, TUI_LGRAY);
            tui_puts(shown);
            name_len = (int)strlen(shown) + 4;
        }

        if (sz_col > 2 + name_len) {
            tui_gotoxy(sz_col, row);
            if (selected) tui_color(TUI_BLACK, TUI_LGRAY);
            else          tui_color(TUI_DGRAY, TUI_BLACK);
            tui_puts(sz);
        }
    }
    tui_color(TUI_WHITE, TUI_BLACK);

    tui_gotoxy(SCRW, row);
    tui_putc(BOX_V);
}

static void filer_draw(FilerState *s)
{
    char line[SCRW + 2];
    char bread[128];
    int i, row, end;

    tui_clear();
    tui_color(TUI_WHITE, TUI_BLACK);

    /* row 1: top border */
    tui_gotoxy(1, 1);
    tui_putc(BOX_TL);
    for (i = 0; i < SCRW - 2; i++) tui_putc(BOX_H);
    tui_putc(BOX_TR);

    /* row 2: title */
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_gotoxy(1, 2);
    tui_putc(BOX_V);
    tui_gotoxy(2, 2);
    for (i = 0; i < SCRW - 2; i++) tui_putc(' ');

    tui_gotoxy(3, 2);
    tui_puts("Filer");

    format_breadcrumb(s->path, bread, sizeof bread);
    {
        int bcol = SCRW - (int)strlen(bread);
        if (bcol < 10) bcol = 10;
        tui_gotoxy(bcol, 2);
        tui_puts(bread);
    }

    tui_gotoxy(SCRW, 2);
    tui_putc(BOX_V);

    /* row 3: divider */
    draw_hline(3, BOX_LT, BOX_RT);

    /* rows 4-51: list */
    row = LIST_TOP;
    end = s->scroll + VISIBLE_ROWS;
    if (end > s->count) end = s->count;

    for (i = s->scroll; i < end; i++) {
        draw_entry(s, i, row);
        row++;
    }
    for (; row <= LIST_BOTTOM; row++) {
        tui_color(TUI_WHITE, TUI_BLACK);
        tui_gotoxy(1, row);
        tui_putc(BOX_V);
        tui_gotoxy(2, row);
        for (i = 0; i < SCRW - 2; i++) tui_putc(' ');
        tui_gotoxy(SCRW, row);
        tui_putc(BOX_V);
    }

    if (s->count == 0) {
        tui_gotoxy(4, LIST_TOP);
        tui_color(TUI_DGRAY, TUI_BLACK);
        tui_puts("(empty)");
        tui_color(TUI_WHITE, TUI_BLACK);
    }

    /* row 52: divider */
    draw_hline(LIST_BOTTOM + 1, BOX_LT, BOX_RT);

    /* row 53: status */
    {
        int nfiles = 0, ndirs = 0;
        uint64_t total = 0;
        for (i = 0; i < s->count; i++) {
            if (strcmp(s->entries[i].name, "..") == 0) continue;
            if (s->entries[i].is_dir) ndirs++;
            else { nfiles++; total += s->entries[i].size; }
        }
        {
            char total_str[16];
            if (total > 1024ULL * 1024ULL)
                snprintf(total_str, sizeof total_str, "%lu.%luM",
                         (unsigned long)(total / (1024ULL * 1024ULL)),
                         (unsigned long)((total * 10 / (1024ULL * 1024ULL)) % 10));
            else if (total >= 1024ULL)
                snprintf(total_str, sizeof total_str, "%luK",
                         (unsigned long)(total / 1024));
            else
                snprintf(total_str, sizeof total_str, "%luB",
                         (unsigned long)total);
            snprintf(line, sizeof line, " %d file%s, %d dir%s, %s total%s",
                     nfiles, nfiles == 1 ? "" : "s",
                     ndirs,  ndirs == 1 ? "" : "s",
                     total_str,
                     s->more ? "  (list cut off)" : "");
        }
        tui_color(TUI_WHITE, TUI_BLACK);
        tui_gotoxy(1, LIST_BOTTOM + 2);
        tui_putc(BOX_V);
        tui_gotoxy(2, LIST_BOTTOM + 2);
        {
            int len = (int)strlen(line);
            tui_puts(line);
            for (i = len; i < SCRW - 2; i++) tui_putc(' ');
        }
        tui_gotoxy(SCRW, LIST_BOTTOM + 2);
        tui_putc(BOX_V);
    }

    /* row 54: card bar */
    {
        uint64_t total_bytes = s->card_total;
        uint64_t used_bytes  = s->card_used;
        uint64_t free_bytes  = total_bytes > used_bytes ? total_bytes - used_bytes : 0;
        int pct = total_bytes > 0 ? (int)(used_bytes * 100 / total_bytes) : 0;
        int fill = pct / 5;
        int bar_col, k;
        char label[64];

        if (fill > 20) fill = 20;

        tui_color(TUI_WHITE, TUI_BLACK);
        tui_gotoxy(1, LIST_BOTTOM + 3);
        tui_putc(BOX_V);
        tui_gotoxy(2, LIST_BOTTOM + 3);
        for (i = 0; i < SCRW - 2; i++) tui_putc(' ');

        tui_gotoxy(3, LIST_BOTTOM + 3);
        {
            char t[16], f[16];
            fmt_gb(t, sizeof t, total_bytes);
            fmt_gb(f, sizeof f, free_bytes);
            snprintf(label, sizeof label, "Card %s total, %s free  ", t, f);
        }
        tui_puts(label);

        bar_col = 3 + (int)strlen(label);
        tui_color(TUI_WHITE, TUI_BLACK);
        tui_gotoxy(bar_col, LIST_BOTTOM + 3);
        tui_putc('|');
        for (k = 0; k < 20; k++) {
            tui_putc(k < fill ? SHADE_FULL : SHADE_EMPTY);
        }
        tui_putc('|');
        tui_gotoxy(bar_col + 22, LIST_BOTTOM + 3);
        {
            char pct_str[16];
            snprintf(pct_str, sizeof pct_str, " %3d%% used", pct);
            tui_puts(pct_str);
        }

        tui_color(TUI_WHITE, TUI_BLACK);
        tui_gotoxy(SCRW, LIST_BOTTOM + 3);
        tui_putc(BOX_V);
    }

    /* row 55: key hints */
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_gotoxy(1, LIST_BOTTOM + 4);
    tui_putc(BOX_V);
    tui_gotoxy(2, LIST_BOTTOM + 4);
    {
        const char *hints = " Up/Down select  S+Up parent  ENTER open  N new  D del  R ren  ESC back";
        int len = (int)strlen(hints);
        tui_puts(hints);
        for (i = len; i < SCRW - 2; i++) tui_putc(' ');
    }
    tui_gotoxy(SCRW, LIST_BOTTOM + 4);
    tui_putc(BOX_V);

    /* row 56: bottom border */
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_gotoxy(1, LIST_BOTTOM + 5);
    tui_putc(BOX_BL);
    for (i = 0; i < SCRW - 2; i++) tui_putc(BOX_H);
    tui_putc(BOX_BR);
}

/* one-line message on the hint row; any key dismisses it */
static void filer_msg(const char *msg)
{
    char line[SCRW + 2];
    int c, row = LIST_BOTTOM + 4;

    snprintf(line, sizeof line, " %.*s  (press a key)", SCRW - 20, msg);
    for (c = (int)strlen(line); c < SCRW; c++) line[c] = ' ';
    line[SCRW] = '\0';
    tui_gotoxy(1, row);
    tui_color(TUI_WHITE, TUI_RED);
    tui_puts(line);
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_getch();
}

/* prompt */
static int filer_prompt(const char *prompt, char *dest, int maxlen)
{
    int len = 0, c;
    char line[SCRW + 2];
    int row = LIST_BOTTOM + 4;

    tui_cursor(1, 0);
    tui_gotoxy(1, row);
    tui_color(TUI_WHITE, TUI_BLUE);
    {
        int plen = (int)strlen(prompt);
        snprintf(line, sizeof line, " %s", prompt);
        for (c = plen + 1; c < SCRW; c++) line[c] = ' ';
        line[SCRW] = '\0';
        tui_puts(line);
    }
    tui_gotoxy((int)strlen(prompt) + 2, row);

    for (;;) {
        c = tui_getch();
        if (c == 0 || c == 224) { tui_getch(); continue; }
        if (c == 13) break;
        if (c == 8) {
            if (len > 0) { len--; tui_putc('\b'); tui_putc(' '); tui_putc('\b'); }
            continue;
        }
        if (c == 27) { dest[0] = '\0'; tui_cursor(0, 0); return -1; }
        if (c >= 32 && c < 127 && len < maxlen - 1 &&
            (int)strlen(prompt) + 3 + len < SCRW - 1) {
            dest[len++] = (char)c;
            tui_putc(c);
        }
    }
    dest[len] = '\0';
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_cursor(0, 0);
    return 0;
}

/* main entry */
int fb_show_files(char *out_path, int out_len)
{
    FilerState *s = (FilerState *)fable_big_calloc(1, sizeof(FilerState));
    int result = 0;

    if (!s) return 0;

    tui_cursor(0, 0);
    memset(s, 0, sizeof *s);
    strcpy(s->path, "/");
    filer_card_refresh(s);
    filer_rescan(s);

    tui_clear();
    filer_draw(s);

    for (;;) {
        int c, code;
        int old_cursor = s->cursor;
        int old_scroll = s->scroll;
        int needs_full_redraw = 0;

        c = tui_getch();

        if (c == 27) { result = 0; break; }

        if (c == 13) {
            if (s->count > 0) {
                FabDirEntry *e = &s->entries[s->cursor];
                if (e->is_dir) {
                    filer_enter_dir(s, e->name);
                    s->cursor = 0; s->scroll = 0;
                    filer_rescan(s);
                    needs_full_redraw = 1;
                } else {
                    char tmp[260];
                    if (strcmp(s->path, "/") == 0)
                        snprintf(tmp, sizeof tmp, "/%s", e->name);
                    else
                        snprintf(tmp, sizeof tmp, "%s%s", s->path, e->name);
                    strncpy(out_path, tmp, out_len - 1);
                    out_path[out_len - 1] = '\0';
                    result = 1;
                    break;
                }
            }
            if (needs_full_redraw) { tui_clear(); filer_draw(s); }
            continue;
        }

        if (c == 0 || c == 224) {
            code = tui_getch();

            if (code == 72) {
                if (tui_shift_down()) {
                    filer_go_up(s);
                    s->cursor = 0; s->scroll = 0;
                    filer_rescan(s);
                    needs_full_redraw = 1;
                } else if (s->cursor > 0) {
                    s->cursor--;
                    if (s->cursor < s->scroll) s->scroll = s->cursor;
                }
            } else if (code == 80) {
                if (tui_shift_down()) {
                    if (s->count > 0) {
                        FabDirEntry *e = &s->entries[s->cursor];
                        if (e->is_dir) {
                            filer_enter_dir(s, e->name);
                            s->cursor = 0; s->scroll = 0;
                            filer_rescan(s);
                            needs_full_redraw = 1;
                        }
                    }
                } else if (s->cursor < s->count - 1) {
                    s->cursor++;
                    if (s->cursor >= s->scroll + VISIBLE_ROWS)
                        s->scroll = s->cursor - VISIBLE_ROWS + 1;
                }
            } else if (code == 71) {
                s->cursor = 0; s->scroll = 0;
            } else if (code == 79) {
                s->cursor = s->count > 0 ? s->count - 1 : 0;
                if (s->cursor >= VISIBLE_ROWS)
                    s->scroll = s->cursor - VISIBLE_ROWS + 1;
                else
                    s->scroll = 0;
            } else if (code == 73) {
                s->cursor -= VISIBLE_ROWS;
                if (s->cursor < 0) s->cursor = 0;
                s->scroll -= VISIBLE_ROWS;
                if (s->scroll < 0) s->scroll = 0;
            } else if (code == 81) {
                s->cursor += VISIBLE_ROWS;
                if (s->cursor >= s->count) s->cursor = s->count > 0 ? s->count - 1 : 0;
                s->scroll += VISIBLE_ROWS;
                if (s->scroll > s->cursor) s->scroll = s->cursor;
                if (s->scroll + VISIBLE_ROWS > s->count)
                    s->scroll = s->count > VISIBLE_ROWS ? s->count - VISIBLE_ROWS : 0;
            }

            if (needs_full_redraw || s->scroll != old_scroll) {
                tui_clear();
                filer_draw(s);
            } else if (s->cursor != old_cursor) {
                draw_entry(s, old_cursor, LIST_TOP + (old_cursor - s->scroll));
                draw_entry(s, s->cursor,  LIST_TOP + (s->cursor  - s->scroll));
            }
            continue;
        }

        if (c == 'n' || c == 'N') {
            char nm[128];
            if (filer_prompt("New file name: ", nm, sizeof nm) == 0 && nm[0]) {
                if (!name_ok(nm)) {
                    filer_msg("Invalid name (no \\ / : * ? \" < > |)");
                } else {
                    if (name_in_use(s, nm, 1)) {
                        filer_msg("A folder already has that name");
                    } else {
                        char tmp[260];
                        if (strcmp(s->path, "/") == 0)
                            snprintf(tmp, sizeof tmp, "/%s", nm);
                        else
                            snprintf(tmp, sizeof tmp, "%s%s", s->path, nm);
                        strncpy(out_path, tmp, out_len - 1);
                        out_path[out_len - 1] = '\0';
                        result = 1;
                        break;
                    }
                }
            }
            tui_clear();
            filer_draw(s);
            continue;
        }

        if (c == 'd' || c == 'D') {
            if (s->count > 0) {
                FabDirEntry *e = &s->entries[s->cursor];
                if (!e->is_dir) {
                    char prompt[100], ans[8];
                    snprintf(prompt, sizeof prompt, "Delete %.40s? (y/n) ", e->name);
                    if (filer_prompt(prompt, ans, sizeof ans) == 0 &&
                        (ans[0] == 'y' || ans[0] == 'Y')) {
                        char tmp[260];
                        if (strcmp(s->path, "/") == 0)
                            snprintf(tmp, sizeof tmp, "/%s", e->name);
                        else
                            snprintf(tmp, sizeof tmp, "%s%s", s->path, e->name);
                        if (fab_remove(tmp) != 0) {
                            filer_msg("Delete failed");
                        } else {
                            filer_card_refresh(s);
                        }
                        filer_rescan(s);
                    }
                    tui_clear();
                    filer_draw(s);
                }
            }
            continue;
        }

        if (c == 'r' || c == 'R') {
            if (s->count > 0 && strcmp(s->entries[s->cursor].name, "..") != 0) {
                FabDirEntry *e = &s->entries[s->cursor];
                {
                    char newname[64];
                    char prompt[100];
                    char tmp_from[260], tmp_to[260];

                    snprintf(prompt, sizeof prompt, "Rename %.40s to: ", e->name);
                    newname[0] = '\0';
                    if (filer_prompt(prompt, newname, sizeof newname) == 0 &&
                        newname[0] != '\0' && strcmp(newname, e->name) != 0) {
                        if (!name_ok(newname)) {
                            filer_msg("Invalid name (no \\ / : * ? \" < > |)");
                        } else if (name_in_use(s, newname, 0)) {
                            filer_msg("That name is already used here");
                        } else {
                            if (strcmp(s->path, "/") == 0) {
                                snprintf(tmp_from, sizeof tmp_from, "/%s", e->name);
                                snprintf(tmp_to,   sizeof tmp_to,   "/%s", newname);
                            } else {
                                snprintf(tmp_from, sizeof tmp_from, "%s%s", s->path, e->name);
                                snprintf(tmp_to,   sizeof tmp_to,   "%s%s", s->path, newname);
                            }
                            if (fab_rename(tmp_from, tmp_to) != 0)
                                filer_msg("Rename failed");
                            filer_rescan(s);
                        }
                    }
                    tui_clear();
                    filer_draw(s);
                }
            }
            continue;
        }
    }

    free(s);
    tui_cursor(1, 0);
    tui_clear();
    return result;
}
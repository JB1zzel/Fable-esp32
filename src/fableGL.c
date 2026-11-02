/* tiny.c - tiny text editor for TCC / Windows CMD, built on tui.h
 * compile: tcc tiny.c tui_console.c tinyspell.c -luser32 -o tiny.exe
 *
 * Keys:
 *   Arrows          move cursor
 *   Shift+Left/Right word left / right
 *   Home / End      line start / end
 *   PgUp / PgDn     page up / down
 *   Enter           split line
 *   Backspace       delete char / merge up at col 0
 *   Delete          delete char / merge down at end
 *   F1              help screen
 *   F2              reflow paragraph (prompts for width, default 80)
 *   F3 / Ctrl+S     save (prompts for name if untitled)
 *   F4              interactive spellcheck (in-process, no save)
 *   F5              toggle spell marks (red misspellings)
 *   Ctrl+O          open file
 *   Ctrl+B          mark block; when you end the mark it is copied to clipboard
 *   Ctrl+X          mark, cut (delete) the block, clipboard keeps it
 *   Ctrl+P          paste clipboard at cursor
 *   Ctrl+F          find text (prompts on the status bar)
 *   Ctrl+R          find and replace all (prompts on the status bar)
 *   Ctrl+N          new document
 *   Esc             cancel mark / prompt (if any), else quit (prompt if modified)
 *
 * Spell marks (F5) are a snapshot: pressing F5 highlights misspellings,
 * any edit clears them, F5 again takes a fresh snapshot.
 *
 * The dictionary (dict.smd) is loaded once at startup. If not found, you
 * are prompted for its path; skipping it leaves Tiny usable without
 * spellcheck.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "tui.h"
#include "tinyspell.h"
#include "file_fabgl.h"
#include "esp_heap_caps.h"

#include "fable.h"


#define IS_WORD(c) (isalnum((unsigned char)(c)) || (c) == '_')
static char (*lines)[MAXLINE] = NULL;
static int change_counter = 0;
static int cached_words = 0;
static int cached_words_counter = -1;
static int   nlines;
static int   cur_line, cur_col;
static int   top_line  = 0;
static int   left_col  = 0;
static int   modified;
static char  filename[260];
static char dict_path[260] = "/dict.smd";
/*static char  dict_path[260] = "dict.smd";
static char  tmp_path[264];*/

static void insert_char(int c);
static void split_line(void);


static void resolve_dict_path(void)
{
    char dir[260];
    tui_exe_dir(dir, sizeof dir);
    if (dir[0])
        snprintf(dict_path, sizeof dict_path, "%sdict.smd", dir);
}

/* ---------- spell marks (F5) ---------- */

static int spell_marks_on = 0;
static int spell_ok = 0;
static unsigned char (*spell_bad)[MAXLINE / 8];   /* was: static unsigned char spell_bad[MAXLINES][MAXLINE / 8]; */

static void spell_set(int line, int col) {
    spell_bad[line][col / 8] |= (unsigned char)(1 << (col % 8));
}
static int spell_is_bad(int line, int col) {
    return (spell_bad[line][col / 8] >> (col % 8)) & 1;
}
static void spell_clear_line(int line) {
    if (line >= 0 && line < MAXLINES)
        memset(spell_bad[line], 0, MAXLINE / 8);
}

static void spell_mark_line(int line)
{
    char *p;
    int col = 0;

    spell_clear_line(line);
    if (!spell_ok || !spell_marks_on) return;
    if (line < 0 || line >= nlines) return;

    p = lines[line];
    while (*p) {
        char word[128], key[128];
        int wlen = 0, start, j;

        while (*p && !IS_WORD(*p)) { p++; col++; }
        start = col;
        while (*p && IS_WORD(*p) && wlen < 127) { word[wlen++] = *p; p++; col++; }
        word[wlen] = '\0';
        if (!wlen) continue;

        for (j = 0; j < wlen; j++) key[j] = toupper((unsigned char)word[j]);
        key[wlen] = '\0';

        if (!ts_check(key)) {
            for (j = start; j < start + wlen; j++)
                spell_set(line, j);
        }
    }
}

static void spell_line_dirty(int line)
{
    spell_clear_line(line);
    if (spell_marks_on && spell_ok) spell_mark_line(line);
}

static void spell_mark_all(void)
{
    int i;
    memset(spell_bad, 0, (size_t)MAXLINES * (MAXLINE / 8));
    for (i = 0; i < nlines; i++) spell_mark_line(i);
}

static void spell_marks_off(void)
{
    spell_marks_on = 0;
    /*memset(spell_bad, 0, sizeof(spell_bad)); */
}

static void para_up(void)
{
    int ln = cur_line;
    /* skip blank lines above */
    while (ln > 0 && lines[ln][0] == '\0') ln--;
    /* if we're on a non-blank line, we're in a paragraph — go to its top */
    while (ln > 0 && lines[ln - 1][0] != '\0') ln--;
    /* if we were already at the top, jump to the previous paragraph */
    if (ln == cur_line) {
        while (ln > 0 && lines[ln - 1][0] == '\0') ln--;
        while (ln > 0 && lines[ln - 1][0] != '\0') ln--;
    }
    cur_line = ln;
    cur_col  = 0;
}

static void para_down(void)
{
    int ln = cur_line;
    /* skip blank lines below */
    while (ln < nlines - 1 && lines[ln][0] == '\0') ln++;
    /* if we're on a non-blank line, we're in a paragraph — go to its bottom */
    while (ln < nlines - 1 && lines[ln + 1][0] != '\0') ln++;
    /* if we were already at the bottom, jump to the next paragraph */
    if (ln == cur_line) {
        while (ln < nlines - 1 && lines[ln + 1][0] == '\0') ln++;
        while (ln < nlines - 1 && lines[ln + 1][0] != '\0') ln++;
    }
    cur_line = ln;
    cur_col  = 0;
}


/* ---------- mark + clipboard ---------- */

typedef struct { int sl, sc, el, ec; } Mark;

static Mark mark;
static int  mark_active = 0;
static int  mark_exists = 0;

static void clear_mark(void)
{
    mark_active = 0;
    mark_exists = 0;
}

static void mark_range(int *l1, int *l2, int *c1, int *c2)
{
    if (mark.sl < mark.el) {
        *l1 = mark.sl; *l2 = mark.el;
        *c1 = mark.sc; *c2 = mark.ec;
    } else if (mark.sl > mark.el) {
        *l1 = mark.el; *l2 = mark.sl;
        *c1 = mark.ec; *c2 = mark.sc;
    } else {
        *l1 = *l2 = mark.sl;
        if (mark.sc < mark.ec) { *c1 = mark.sc; *c2 = mark.ec; }
        else                   { *c1 = mark.ec; *c2 = mark.sc; }
    }
}

static int in_mark(int line, int col)
{
    int l1, l2, c1, c2;
    if (!mark_exists) return 0;
    mark_range(&l1, &l2, &c1, &c2);
    if (line < l1 || line > l2) return 0;
    if (l1 == l2) return col >= c1 && col < c2;
    if (line == l1) return col >= c1;
    if (line == l2) return col < c2;
    return 1;
}

static char *clip = NULL;
static long  clip_len = 0;
static long  clip_cap = 0;

static void clip_clear(void) { clip_len = 0; }

static int clip_append(const char *s, long n)
{
    if (clip_len + n > clip_cap) {
        long new_cap = clip_cap ? clip_cap : 256;
        char *new_buf;
        while (new_cap < clip_len + n) new_cap *= 2;
        new_buf = (char *)realloc(clip, (size_t)new_cap);
        if (!new_buf) return 0;
        clip = new_buf;
        clip_cap = new_cap;
    }
    memcpy(clip + clip_len, s, (size_t)n);
    clip_len += n;
    return 1;
}

static void copy_mark_to_clip(void)
{
    int l1, l2, c1, c2, l;
    clip_clear();
    if (!mark_exists) return;
    mark_range(&l1, &l2, &c1, &c2);
    for (l = l1; l <= l2; l++) {
        int start = (l == l1) ? c1 : 0;
        int end   = (l == l2) ? c2 : (int)strlen(lines[l]);
        int len   = end - start;
        if (len < 0) len = 0;
        if (len > 0) clip_append(&lines[l][start], (long)len);
        if (l < l2)  clip_append("\n", 1);
    }
}

static void delete_mark(void)
{
    int l1, l2, c1, c2, i, len;

    if (!mark_exists) return;
    mark_range(&l1, &l2, &c1, &c2);

    if (l1 == l2) {
        len = (int)strlen(lines[l1]);
        if (c2 > len) c2 = len;
        if (c1 > c2) c1 = c2;
        memmove(&lines[l1][c1],
                &lines[l1][c2],
                (size_t)(len - c2 + 1));
        cur_line = l1;
        cur_col  = c1;
    } else {
        int first_len = (int)strlen(lines[l1]);
        int last_len  = (int)strlen(lines[l2]);
        int keep_first, keep_last, after;

        if (c1 > first_len) c1 = first_len;
        if (c2 > last_len)  c2 = last_len;
        keep_first = c1;
        keep_last  = last_len - c2;

        if (keep_first + keep_last + 1 <= MAXLINE) {
            memmove(&lines[l1][keep_first],
                    &lines[l2][c2],
                    (size_t)(keep_last + 1));
        } else {
            lines[l1][keep_first] = '\0';
        }

        after = nlines - l2 - 1;
        for (i = 0; i < after; i++) {
            size_t ln = strlen(lines[l2 + 1 + i]) + 1;
            memcpy(lines[l1 + 1 + i], lines[l2 + 1 + i], ln);
        }
        nlines -= (l2 - l1);

        cur_line = l1;
        cur_col  = keep_first;
    }
    modified = 1;
    spell_marks_off();
}

static void paste_clip(void)
{
    long i;
    for (i = 0; i < clip_len; i++) {
        if (clip[i] == '\n') split_line();
        else                  insert_char((unsigned char)clip[i]);
    }
    modified = 1;
    spell_marks_off();
}

/* ---------- find ---------- */

static int find_next(const char *needle)
{
    int nlen = (int)strlen(needle);
    int start_line = cur_line;
    int start_col  = cur_col + 1;
    int i;

    if (nlen == 0) return 0;

    for (i = 0; i <= nlines; i++) {              /* <= so we come back to the start line */
        int ln = (start_line + i) % nlines;
        const char *s = lines[ln];
        int slen = (int)strlen(s);
        int from = (i == 0) ? start_col : 0;
        int to   = (i == nlines) ? start_col + nlen - 1 : slen;   /* last pass: stop where we began */
        int k;

        if (from > slen) from = slen;
        if (to   > slen) to   = slen;
        for (k = from; k + nlen <= to; k++) {
            if (memcmp(&s[k], needle, (size_t)nlen) == 0) {
                cur_line = ln;
                cur_col  = k;
                return 1;
            }
        }
    }
    return 0;
}

/* ---------- find and replace all ---------- */

typedef struct {
    const char *needle;
    const char *replacement;
    int         case_sensitive;
} FindSpec;

static int replace_all(const FindSpec *spec)
{
    int nlen, rlen, count = 0;
    int l;
    int replaced_any = 0;

    if (!spec || !spec->needle || !spec->needle[0]) return 0;
    if (strchr(spec->needle, '\n')) return 0;

    nlen = (int)strlen(spec->needle);
    rlen = (int)strlen(spec->replacement);

    for (l = 0; l < nlines; l++) {
        char out[MAXLINE];
        int outlen = 0;
        int pos = 0;
        int linelen = (int)strlen(lines[l]);
        int line_changed = 0;

        while (pos < linelen) {
            int match = -1;
            int k;

            for (k = pos; k + nlen <= linelen; k++) {
                int ok;
                if (spec->case_sensitive) {
                    ok = memcmp(&lines[l][k], spec->needle, (size_t)nlen) == 0;
                } else {
                    int j;
                    ok = 1;
                    for (j = 0; j < nlen; j++) {
                        if (tolower((unsigned char)lines[l][k + j]) !=
                            tolower((unsigned char)spec->needle[j])) {
                            ok = 0;
                            break;
                        }
                    }
                }
                if (ok) { match = k; break; }
            }

            if (match < 0) {
                int rest = linelen - pos;
                if (outlen + rest > MAXLINE - 1) rest = MAXLINE - 1 - outlen;
                if (rest > 0) {
                    memcpy(out + outlen, &lines[l][pos], (size_t)rest);
                    outlen += rest;
                }
                break;
            }

            {
                int pre = match - pos;
                if (outlen + pre > MAXLINE - 1) pre = MAXLINE - 1 - outlen;
                if (pre > 0) {
                    memcpy(out + outlen, &lines[l][pos], (size_t)pre);
                    outlen += pre;
                }
            }

            {
                int rep = rlen;
                if (outlen + rep > MAXLINE - 1) rep = MAXLINE - 1 - outlen;
                if (rep > 0) {
                    memcpy(out + outlen, spec->replacement, (size_t)rep);
                    outlen += rep;
                }
            }

            count++;
            line_changed = 1;
            pos = match + nlen;
        }

        if (line_changed) {
            out[outlen] = '\0';
            memcpy(lines[l], out, (size_t)(outlen + 1));
            replaced_any = 1;
        }
    }

    if (replaced_any) {
        modified = 1;
        if (cur_line >= nlines) cur_line = nlines - 1;
        if (cur_line < 0) cur_line = 0;
        {
            int len = (int)strlen(lines[cur_line]);
            if (cur_col > len) cur_col = len;
        }
        spell_marks_off();
    }

    return count;
}

/* ---------- reflow ---------- */

/* length of the next output line from p (remain chars left) */
static int wrap_len(const char *p, int remain, int width)
{
    int brk;
    if (remain <= width) return remain;
    brk = width;
    while (brk > 0 && p[brk] != ' ') brk--;
    return brk > 0 ? brk : width;        /* no space: hard-break at width */
}

static void reflow(int width)
{
    int  pstart, pend, i, j;
    char *para;
    long total;
    int  plen, pos, outcount, old_count, diff;
    int  changed = 0;

    if (width < 10)  width = 10;
    if (width > 120) width = 120;

    if (lines[cur_line][0] == '\0') return;

    /* paragraph bounds around the cursor */
    pstart = cur_line;
    pend   = cur_line;
    while (pstart > 0 && lines[pstart - 1][0] != '\0') pstart--;
    while (pend < nlines - 1 && lines[pend + 1][0] != '\0') pend++;

    /* indented paragraph: treat as preformatted, don't touch */
    if (lines[pstart][0] == ' ' || lines[pstart][0] == '\t') return;

    /* joined text is never longer than the sum of the lines + joiners */
    total = 1;
    for (i = pstart; i <= pend; i++) total += (long)strlen(lines[i]) + 1;
    para = (char *)malloc((size_t)total);
    if (!para) return;

    /* join lines; leading whitespace dropped, runs of spaces/tabs -> one space */
    plen = 0;
    for (i = pstart; i <= pend; i++) {
        const char *s = lines[i];
        int at_line_start = 1;
        while (*s) {
            if (*s == ' ' || *s == '\t') {
                if (!at_line_start && plen > 0 && para[plen - 1] != ' ')
                    para[plen++] = ' ';
            } else {
                para[plen++] = *s;
                at_line_start = 0;
            }
            s++;
        }
        if (i < pend && plen > 0 && para[plen - 1] != ' ')
            para[plen++] = ' ';
    }
    while (plen > 0 && para[plen - 1] == ' ') plen--;
    para[plen] = '\0';
    if (plen == 0) { free(para); return; }

    /* count output lines */
    outcount = 0;
    pos = 0;
    while (pos < plen) {
        pos += wrap_len(para + pos, plen - pos, width);
        outcount++;
        if (pos < plen && para[pos] == ' ') pos++;
    }

    /* grow or shrink lines[] to fit */
    old_count = pend - pstart + 1;
    diff = outcount - old_count;
    if (diff > 0 && nlines + diff > MAXLINES) { free(para); return; }
    if (diff != 0) {
        int tail = nlines - pend - 1;
        if (diff > 0) {
            for (j = 0; j < tail; j++)
                memcpy(lines[nlines + diff - 1 - j], lines[nlines - 1 - j], MAXLINE);
        } else {
            for (j = 0; j < tail; j++)
                memcpy(lines[pend + 1 + diff + j], lines[pend + 1 + j], MAXLINE);
        }
        nlines += diff;
        changed = 1;
    }

    /* write wrapped lines */
    pos = 0;
    for (i = pstart; pos < plen; i++) {
        int len = wrap_len(para + pos, plen - pos, width);
        if (strlen(lines[i]) != (size_t)len ||
            memcmp(lines[i], para + pos, (size_t)len) != 0)
            changed = 1;
        memcpy(lines[i], para + pos, (size_t)len);
        lines[i][len] = '\0';
        pos += len;
        if (pos < plen && para[pos] == ' ') pos++;
    }

    free(para);
    if (changed) modified = 1;

    cur_line = pstart;          /* cursor to start of the reflowed paragraph */
    cur_col  = 0;
    spell_marks_off();
}

/* ---------- file io ---------- */

static void status_msg(const char *msg);

/* Set when a file did not fit (more than MAXLINES lines, or a line longer
 * than MAXLINE-1). Saving over that file is then refused so the original
 * is never replaced by a shortened copy. */
static int  load_truncated = 0;
static char trunc_name[260] = "";

/* returns 1 if the file was loaded, 0 if it could not be opened
 * (the current buffer is left untouched in that case) */
static int load_file(const char *fn)
{
    FILE *f;
    int c;
    if (!fab_exists(fn)) return 0;         /* avoids a VFS error log */
    f = fopen(fn, "rb");
    if (!f) return 0;
    load_truncated = 0;
    nlines = 0;
    for (;;) {
        int len = 0;
        c = fgetc(f);
        while (c != EOF && c != '\n' && c != '\r') {
            if (len < MAXLINE - 1) lines[nlines][len++] = (char)c;
            else load_truncated = 1;
            c = fgetc(f);
        }
        lines[nlines][len] = '\0';
        nlines++;
        if (c == EOF) break;
        if (c == '\r') {
            int c2 = fgetc(f);
            if (c2 != '\n' && c2 != EOF) ungetc(c2, f);
        }
        if (nlines >= MAXLINES) {
            if (fgetc(f) != EOF) load_truncated = 1;   /* more data left */
            break;
        }
    }
    fclose(f);
    if (nlines == 0) { lines[0][0] = '\0'; nlines = 1; }
    modified = 0;
    top_line = 0;
    left_col = 0;
    cur_line = 0;
    cur_col = 0;
    clear_mark();
    spell_marks_off();
    if (load_truncated) {
        strncpy(trunc_name, fn, sizeof(trunc_name) - 1);
        trunc_name[sizeof(trunc_name) - 1] = '\0';
        status_msg("File too big (max 256 lines / 1023 cols): saving it is disabled");
        tui_getch();
    }
    return 1;
}

/* Writes to <name>.tmp first, and only replaces the real file once the
 * whole temp file has been written and closed without error. If power
 * fails mid-save the original is still intact. */
static int save_file(void)
{
    char tmp_path[264];
    FILE *f;
    int i, ok = 1;

    if (load_truncated && strcmp(filename, trunc_name) == 0) {
        status_msg("Not saved: file was truncated when loaded (256 lines / 1023 cols)");
        tui_getch();
        return 0;
    }

    snprintf(tmp_path, sizeof tmp_path, "%s.tmp", filename);
    f = fopen(tmp_path, "wb");
    if (!f) return 0;
    for (i = 0; i < nlines; i++) {
        if (fputs(lines[i], f) == EOF) ok = 0;
        if (i < nlines - 1 && fputs("\r\n", f) == EOF) ok = 0;
    }
    if (fclose(f) == EOF) ok = 0;
    if (!ok) { fab_remove(tmp_path); return 0; }

    if (fab_exists(filename)) fab_remove(filename);
    if (fab_rename(tmp_path, filename) != 0) return 0;  /* data kept in .tmp */

    modified = 0;
    return 1;
}

/* ---------- word count ---------- */

static int count_words(void)
{
    int n = 0, i, k;
    for (i = 0; i < nlines; i++) {
        int in = 0;
        for (k = 0; lines[i][k]; k++) {
            if (IS_WORD(lines[i][k])) { if (!in) { n++; in = 1; } }
            else in = 0;
        }
    }
    return n;
}

/* ---------- status bar and prompt bar ---------- */

static void status_msg(const char *msg)
{
    char buf[SCRW + 1];
    int i;
    snprintf(buf, sizeof buf, " %s", msg);
    for (i = (int)strlen(buf); i < SCRW; i++) buf[i] = ' ';
    buf[SCRW] = '\0';
    tui_gotoxy(1, SCRH + 1);
    tui_color(TUI_BLACK, TUI_LGRAY);
    tui_puts(buf);
    tui_color(TUI_LGRAY, TUI_BLACK);
}

static void prompt_bar(const char *text)
{
    char buf[SCRW + 1];
    int i;
    snprintf(buf, sizeof buf, " %s", text);
    for (i = (int)strlen(buf); i < SCRW; i++) buf[i] = ' ';
    buf[SCRW] = '\0';
    tui_gotoxy(1, SCRH + 1);
    tui_color(TUI_WHITE, TUI_BLUE);
    tui_puts(buf);
    tui_gotoxy((int)strlen(text) + 2, SCRH + 1);
}

static void put_status(void)
{
    char left[SCRW + 1];
    char right[SCRW + 1];
    char shown[64];
    int w = count_words();
    int len, rlen;
    const char *hint;
    const char *base;
    const char *dot;
    int n;

    base = filename[0] ? filename : "(unnamed)";
    {
        const char *s = base;
        while (*s) {
            if (*s == '\\' || *s == '/') base = s + 1;
            s++;
        }
    }
    dot = strrchr(base, '.');
    if (dot && dot != base) n = (int)(dot - base);
    else                     n = (int)strlen(base);
    if (n > (int)sizeof(shown) - 1) n = (int)sizeof(shown) - 1;
    memcpy(shown, base, (size_t)n);
    shown[n] = '\0';

    /* contextual hint — short, mode-aware */
    if (mark_active)       hint = "^B END  ESC CANCEL";
    else if (mark_exists)  hint = "^P PASTE  ^X CUT  ESC CLEAR";
    else                   hint = "F1 HELP  F3 SAVE  F4 SPELL  ESC EXIT";

    snprintf(left, sizeof left, " %s  %d:%d%s  w:%d%s",
             shown,
             cur_line + 1, cur_col + 1,
             left_col > 0 ? "+" : "",
             w,
             modified ? "  !!" : "");
    left[SCRW] = '\0';

    snprintf(right, sizeof right, "%s", hint);
    rlen = (int)strlen(right);

    len = (int)strlen(left);
    if (len + rlen > SCRW) {
        len = SCRW - rlen - 1;
        if (len < 0) len = 0;
        left[len] = '\0';
    }

    tui_gotoxy(1, SCRH + 1);
    tui_color(TUI_BLACK, TUI_LGRAY);
    {
        int i, gap = SCRW - rlen - len;
        tui_puts(left);
        for (i = 0; i < gap; i++) tui_putc(' ');
        tui_puts(right);
    }
    tui_color(TUI_LGRAY, TUI_BLACK);
}
/* ---------- render ---------- */

static void render(void)
{
    int i;
    char esc[32];

    if (cur_line < top_line) top_line = cur_line;
    if (cur_line >= top_line + SCRH) top_line = cur_line - SCRH + 1;
    if (top_line < 0) top_line = 0;
    if (top_line > nlines - 1) top_line = nlines - 1;

    if (cur_col < left_col) left_col = cur_col;
    if (cur_col >= left_col + SCRW) left_col = cur_col - SCRW + 1;
    if (left_col < 0) left_col = 0;

    /* cache word count — only recompute when modified flag changes */
    cached_words = count_words();

    tui_frame_begin();

    tui_frame_put("\x1b[?25l\x1b[H", 9);

        for (i = 0; i < SCRH; i++) {
        int ln = top_line + i;
        int prev_col = 0;                          /* was -1 */

        tui_frame_put("\x1b[37;40m", 8);           /* NEW: each row starts in normal colours */

        if (ln < nlines) {
            int len = (int)strlen(lines[ln]);
            int shown = 0;
            int k;
            if (left_col < len) {
                int avail = len - left_col;
                int lim = avail > SCRW ? SCRW : avail;
                for (k = 0; k < lim; k++) {
                    int src_col = left_col + k;
                    int cid = 0;
                    if (in_mark(ln, src_col))                             cid = 1;
                    else if (spell_marks_on && spell_is_bad(ln, src_col)) cid = 2;

                    if (cid != prev_col) {
                        if (cid == 1)      snprintf(esc, sizeof esc, "\x1b[30;46m");
                        else if (cid == 2) snprintf(esc, sizeof esc, "\x1b[91;40m");
                        else               snprintf(esc, sizeof esc, "\x1b[37;40m");
                        tui_frame_put(esc, (int)strlen(esc));
                        prev_col = cid;
                    }
                    tui_frame_put(&lines[ln][src_col], 1);
                }
                shown = lim;
            }
            if (prev_col != 0) tui_frame_put("\x1b[37;40m", 8);   /* NEW: back to normal before padding */
            while (shown < SCRW) { tui_frame_put(" ", 1); shown++; }
        } else {
            int k;
            for (k = 0; k < SCRW; k++) tui_frame_put(" ", 1);
        }
        tui_frame_put("\x1b[K\r\n", 5);
    }

    /* status bar */
    {
        char left[SCRW + 1];
        char right[SCRW + 1];
        char shown[64];
        int len, rlen;
        const char *hint;
        const char *base;
        const char *dot;
        int n;

        base = filename[0] ? filename : "(unnamed)";
        {
            const char *s = base;
            while (*s) {
                if (*s == '\\' || *s == '/') base = s + 1;
                s++;
            }
        }
        dot = strrchr(base, '.');
        if (dot && dot != base) n = (int)(dot - base);
        else                     n = (int)strlen(base);
        if (n > (int)sizeof(shown) - 1) n = (int)sizeof(shown) - 1;
        memcpy(shown, base, (size_t)n);
        shown[n] = '\0';

        if (mark_active)       hint = "^B END  ESC CANCEL";
        else if (mark_exists)  hint = "^P PASTE  ^X CUT  ESC CLEAR";
        else                   hint = "F1 HELP  F3 SAVE  F4 SPELL  ESC EXIT";

        snprintf(left, sizeof left, " %s  %d:%d%s  w:%d%s",
                 shown, cur_line + 1, cur_col + 1,
                 left_col > 0 ? "+" : "",
                 cached_words,
                 modified ? "  !!" : "");
        left[SCRW] = '\0';

        snprintf(right, sizeof right, "%s", hint);
        rlen = (int)strlen(right);

        len = (int)strlen(left);
        if (len + rlen > SCRW) {
            len = SCRW - rlen - 1;
            if (len < 0) len = 0;
            left[len] = '\0';
        }

        tui_frame_put("\x1b[30;47m", 8);    /* black on white */
        tui_frame_put(left, len);
        {
            int gap = SCRW - rlen - len;
            int j;
            for (j = 0; j < gap; j++) tui_frame_put(" ", 1);
        }
        tui_frame_put(right, rlen);
        tui_frame_put("\x1b[37;40m", 8);    /* back to light grey on black */
        tui_frame_put("\x1b[K", 3);
    }

    snprintf(esc, sizeof esc, "\x1b[%d;%dH\x1b[?25h",
             cur_line - top_line + 1, cur_col - left_col + 1);
    tui_frame_put(esc, (int)strlen(esc));

    tui_frame_end();
}
/* ---------- cursor helpers ---------- */

static void clamp_col(void)
{
    int len = (int)strlen(lines[cur_line]);
    if (cur_col > len) cur_col = len;
    if (cur_col < 0) cur_col = 0;
}

static void word_left(void)
{
    if (cur_col == 0) {
        if (cur_line > 0) {
            cur_line--;
            cur_col = (int)strlen(lines[cur_line]);
        }
        return;
    }
    while (cur_col > 0 && !IS_WORD(lines[cur_line][cur_col - 1])) cur_col--;
    while (cur_col > 0 &&  IS_WORD(lines[cur_line][cur_col - 1])) cur_col--;
}

static void word_right(void)
{
    int len = (int)strlen(lines[cur_line]);
    if (cur_col >= len) {
        if (cur_line < nlines - 1) { cur_line++; cur_col = 0; }
        return;
    }
    while (cur_col < len &&  IS_WORD(lines[cur_line][cur_col])) cur_col++;
    while (cur_col < len && !IS_WORD(lines[cur_line][cur_col])) cur_col++;
}

/* ---------- edits ---------- */

static void insert_char(int c)
{
    int len = (int)strlen(lines[cur_line]);
    if (cur_col > len) cur_col = len;
    if (len >= MAXLINE - 1) return;
    memmove(&lines[cur_line][cur_col + 1],
            &lines[cur_line][cur_col],
            (size_t)(len - cur_col + 1));
    lines[cur_line][cur_col] = (char)c;
    cur_col++;
    modified = 1;
    spell_marks_off();
}

static void split_line(void)
{
    char tailbuf[MAXLINE];
    int len, tail;
    if (nlines >= MAXLINES) return;
    len = (int)strlen(lines[cur_line]);
    if (cur_col > len) cur_col = len;
    tail = len - cur_col;

    memcpy(tailbuf, &lines[cur_line][cur_col], (size_t)(tail + 1));
    lines[cur_line][cur_col] = '\0';

    memmove(&lines[cur_line + 2], &lines[cur_line + 1],
            (size_t)((nlines - cur_line - 1) * MAXLINE));

    memcpy(lines[cur_line + 1], tailbuf, (size_t)(tail + 1));

    nlines++;
    cur_line++;
    cur_col = 0;
    modified = 1;
    spell_marks_off();
}

static void join(int a, int b)
{
    int alen = (int)strlen(lines[a]);
    int blen = (int)strlen(lines[b]);
    int i;
    if (alen + blen >= MAXLINE - 1) return;
    memcpy(&lines[a][alen], lines[b], (size_t)(blen + 1));
    for (i = b; i < nlines - 1; i++) {
        size_t l = strlen(lines[i + 1]) + 1;
        memcpy(lines[i], lines[i + 1], l);
    }
    nlines--;
    cur_line = a;
    cur_col  = alen;
    modified = 1;
    spell_marks_off();
}

static void del_back(void)
{
    int len = (int)strlen(lines[cur_line]);
    if (cur_col > 0) {
        memmove(&lines[cur_line][cur_col - 1],
                &lines[cur_line][cur_col],
                (size_t)(len - cur_col + 1));
        cur_col--;
        modified = 1;
    } else if (cur_line > 0) {
        join(cur_line - 1, cur_line);
    }
    spell_marks_off();
}

static void del_forward(void)
{
    int len = (int)strlen(lines[cur_line]);
    if (cur_col < len) {
        memmove(&lines[cur_line][cur_col],
                &lines[cur_line][cur_col + 1],
                (size_t)(len - cur_col));
        modified = 1;
    } else if (cur_line < nlines - 1) {
        join(cur_line, cur_line + 1);
    }
    spell_marks_off();
}

/* ---------- input ---------- */

static int shift_down(void)
{
    return tui_shift_down();
}

static void move_arrow(int code)
{
    int shift = shift_down();
    switch (code) {
    case 72:                                       /* Up */
        if (shift) para_up();
        else if (cur_line > 0) { cur_line--; clamp_col(); }
        break;
    case 80:                                       /* Down */
        if (shift) para_down();
        else if (cur_line < nlines - 1) { cur_line++; clamp_col(); }
        break;
    case 75:
        if (shift) word_left();
        else if (cur_col > 0) cur_col--;
        else if (cur_line > 0) {
            cur_line--;
            cur_col = (int)strlen(lines[cur_line]);
        }
        break;
    case 77:
        if (shift) word_right();
        else if (cur_col < (int)strlen(lines[cur_line])) cur_col++;
        else if (cur_line < nlines - 1) { cur_line++; cur_col = 0; }
        break;
    case 71: cur_col = 0; break;
    case 79: cur_col = (int)strlen(lines[cur_line]); break;
    case 73:
        cur_line -= SCRH - 1;
        if (cur_line < 0) cur_line = 0;
        clamp_col();
        break;
    case 81:
        cur_line += SCRH - 1;
        if (cur_line >= nlines) cur_line = nlines - 1;
        clamp_col();
        break;
    }
}

/* ---------- save with name prompt (if untitled) ---------- */

static int save_current(void)
{
    if (!filename[0]) {
        char path[260];
        prompt_bar("Save as: ");
        path[0] = '\0';
        fb_prompt_line("", path, sizeof path);
        tui_color(TUI_LGRAY, TUI_BLACK);
        if (!path[0]) return 0;
        strncpy(filename, path, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    }
    return save_file();
}

/* ---------- Ctrl+N: new named document ---------- */

static void new_document(void)
{
    char path[260];
    FILE *probe;

    clear_mark();

    if (modified) {
        int k;
        prompt_bar("Save changes? (y/n/c) ");
        k = tui_getch();
        tui_color(TUI_LGRAY, TUI_BLACK);
        if (k == 'c' || k == 'C' || k == 27) return;
        if (k == 'y' || k == 'Y') {
            if (!save_current()) return;
        }
    }

   
    fb_show_files();
    prompt_bar("New/open file: ");
    path[0] = '\0';
    fb_prompt_line("", path, sizeof path);
    tui_color(TUI_LGRAY, TUI_BLACK);
    if (!path[0]) return;

    nlines = 1;
    lines[0][0] = '\0';
    modified = 0;
    top_line = 0;
    left_col = 0;
    cur_line = 0;
    cur_col  = 0;

    strncpy(filename, path, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';

    probe = fopen(filename, "rb");
    if (probe) {
        fclose(probe);
        load_file(filename);
        cur_line = 0;
        cur_col  = 0;
    } else {
        FILE *out = fopen(filename, "wb");
        if (out) fclose(out);
    }
}

/* ---------- F4: save, spellcheck, reload ---------- */

static int spell_buffer(void)
{
    int i, n = 0;
    for (i = 0; i < nlines; i++) {
        char *p = lines[i];
        while (*p) {
            char word[128], key[128];
            int wlen = 0, k;
            while (*p && !IS_WORD(*p)) p++;
            while (*p &&  IS_WORD(*p) && wlen < 127) word[wlen++] = *p++;
            word[wlen] = '\0';
            if (!wlen) continue;
            for (k = 0; k < wlen; k++)
                key[k] = toupper((unsigned char)word[k]);
            key[wlen] = '\0';
            if (!ts_check(key)) n++;
        }
    }
    return n;
}

static void spell_check(void)
{
    char *text;
    int total = 0, len, fixes, i, off;

    clear_mark();

    if (!spell_ok) {
        status_msg("Dictionary not loaded.");
        tui_getch();
        return;
    }

    for (i = 0; i < nlines; i++) total += (int)strlen(lines[i]) + 1;
    text = (char *)malloc((size_t)total + 1);
    if (!text) { status_msg("Out of memory."); tui_getch(); return; }

    off = 0;
    for (i = 0; i < nlines; i++) {
        int l = (int)strlen(lines[i]);
        memcpy(text + off, lines[i], (size_t)l);
        off += l;
        text[off++] = '\n';
    }
    text[off] = '\0';
    len = off;

    tui_clear();
    fixes = ts_fix_interactive(&text, &len);

    /* rebuild lines[] from text */
    nlines = 0;
    {
        int start = 0, k;
        for (k = 0; k <= len; k++) {
            if (k == len || text[k] == '\n') {
                int l = k - start;
                if (l > MAXLINE - 1) l = MAXLINE - 1;
                memcpy(lines[nlines], text + start, (size_t)l);
                lines[nlines][l] = '\0';
                nlines++;
                if (nlines >= MAXLINES) break;
                start = k + 1;
            }
        }
        if (nlines == 0) { lines[0][0] = '\0'; nlines = 1; }
    }

    free(text);

    if (fixes > 0) modified = 1;

    {
        char msg[64];
        snprintf(msg, sizeof msg, "%d fix%s", fixes, fixes == 1 ? "" : "es");
        status_msg(msg);
    }

    if (cur_line >= nlines) cur_line = nlines - 1;
    if (cur_line < 0) cur_line = 0;
    clamp_col();
    top_line = 0;
    left_col = 0;
    spell_marks_off(); 
}
/* ---------- main ---------- */

int fable_main(int argc, char **argv)
{
    int ch;

    lines     = (char (*)[MAXLINE])heap_caps_calloc(MAXLINES, MAXLINE, MALLOC_CAP_SPIRAM);
    spell_bad = (unsigned char (*)[MAXLINE / 8])heap_caps_calloc(MAXLINES, MAXLINE / 8, MALLOC_CAP_SPIRAM);
    if (!lines || !spell_bad) return 1;

    tui_init();
    tui_clear();

    if (argc > 1) {
        strncpy(filename, argv[1], sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    }

    tui_clear();

    lines[0][0] = '\0';
    nlines = 1;
    if (filename[0]) load_file(filename);
    resolve_dict_path();

    if (ts_init(dict_path)) {
        spell_ok = 1;
    } else {
        char path[260];
        path[0] = '\0';
        prompt_bar("Dictionary not found. Path [skip]: ");
        if (fb_prompt_line("", path, sizeof path) >= 0 && path[0]) {
            strncpy(dict_path, path, sizeof(dict_path) - 1);
            dict_path[sizeof(dict_path) - 1] = '\0';
            if (ts_init(dict_path)) spell_ok = 1;
        }
        tui_color(TUI_LGRAY, TUI_BLACK);
        tui_clear();
    }

    for (;;) {

        if (!tui_key_pending()) render();
        ch = tui_getch();

        if (ch == 0 || ch == 224) {
            int code = tui_getch();
            if (code == 59) {          /* F1 */
                clear_mark();
                fb_help_screen();
            } else if (code == 60) {                     /* F2: reflow */
                char buf[16];
                int width = 80;
                buf[0] = '\0';
                prompt_bar("Width [80]: ");
                if (fb_prompt_line("", buf, sizeof buf) < 0) {
                    tui_color(TUI_LGRAY, TUI_BLACK);
                    continue;                            /* Esc cancels */
                }
                tui_color(TUI_LGRAY, TUI_BLACK);
                if (buf[0]) width = atoi(buf);
                if (width < 20)  width = 20;
                if (width > 120) width = 120;
                clear_mark();
                reflow(width);
            } else if (code == 61) {              /* F3 */
                clear_mark();
                save_current();
            } else if (code == 64) {              /* F6: calendar */
                char path[64];
                clear_mark();
                if (diary_pick(path, sizeof path)) {
                    if (!load_file(path)) {
                        /* new diary day: start with an empty buffer so we
                         * never save the previous document into this file */
                        nlines = 1;
                        lines[0][0] = '\0';
                        modified = 0;
                        top_line = left_col = 0;
                        cur_line = cur_col = 0;
                        clear_mark();
                        spell_marks_off();
                    }
                    strncpy(filename, path, sizeof(filename) - 1);
                    filename[sizeof(filename) - 1] = '\0';
                }
            } else if (code == 67) {              /* F9: Wi-Fi setup */
               clear_mark();
               fb_wifi_setup_page();
            } else if (code == 62) {              /* F4 */
                spell_check();
            } else if (code == 63) {              /* F5: toggle spell marks */
                clear_mark();
                if (spell_marks_on) {
                    spell_marks_on = 0;
                    status_msg("Spell marks off.");
                } else {
                    if (!spell_ok) {
                        if (!ts_init(dict_path)) {
                            status_msg("Dictionary not found.");
                            tui_getch();
                            continue;
                        }
                        spell_ok = 1;
                    }
                    spell_marks_on = 1;
                    spell_mark_all();
                    status_msg("Spell marks on.");
                }
            } else if (code == 83) {              /* Delete */
                clear_mark();
                del_forward();
            } else {
                move_arrow(code);
            }
            continue;
        }
        switch (ch) {      
        case 19:                                  /* Ctrl+S: save */
            clear_mark();
            save_current();
            break;
        case 15:                                  /* Ctrl+O: open */
            {
                char path[260];
                path[0] = '\0';
                prompt_bar("Open: ");
                if (fb_prompt_line("", path, sizeof path) >= 0 && path[0]) {
                    if (load_file(path)) {
                        strncpy(filename, path, sizeof(filename) - 1);
                        filename[sizeof(filename) - 1] = '\0';
                    } else {
                        status_msg("File not found");
                        tui_getch();
                    }
                }
                tui_color(TUI_LGRAY, TUI_BLACK);
            }
            break;
        case 2:                                   /* Ctrl+B */
            if (mark_active) {
                mark.el = cur_line;
                mark.ec = cur_col;
                mark_active = 0;
                mark_exists = 1;
                copy_mark_to_clip();
            } else {
                mark.sl = cur_line;
                mark.sc = cur_col;
                mark.el = cur_line;
                mark.ec = cur_col;
                mark_active = 1;
                mark_exists = 0;
                clip_clear();
            }
            break;
        case 24:                                  /* Ctrl+X: cut */
            if (mark_exists) {
                copy_mark_to_clip();
                delete_mark();
                clear_mark();
            }
            break;
        case 16:                                  /* Ctrl+P: paste */
            if (clip_len > 0) paste_clip();
            clear_mark();
            break;
        case 6:                                   /* Ctrl+F: find */
            {
                char needle[128];
                needle[0] = '\0';
                prompt_bar("Find: ");
                fb_prompt_line("", needle, sizeof needle);
                tui_color(TUI_LGRAY, TUI_BLACK);
                if (needle[0] && !find_next(needle)) {
                    prompt_bar("Not found - press a key");
                    tui_getch();
                    tui_color(TUI_LGRAY, TUI_BLACK);
                }
            }
            break;
        
        case 18:                                  /* Ctrl+R: find and replace all */
            {
                char find_buf[128];
                char replace_buf[128];
                FindSpec spec;
                int n;

                find_buf[0] = '\0';

                /* Pre-fill from clipboard if short, single-line, printable */
                if (clip_len > 0 && clip_len <= 15) {
                    long i;
                    int ok = 1;
                    for (i = 0; i < clip_len; i++) {
                        unsigned char c = (unsigned char)clip[i];
                        if (c == '\n' || c == '\r' || c < 32 || c > 126) {
                            ok = 0;
                            break;
                        }
                    }
                    if (ok) {
                        memcpy(find_buf, clip, (size_t)clip_len);
                        find_buf[clip_len] = '\0';
                    }
                }

                prompt_bar("Find: ");
                if (find_buf[0]) {
                    if (fb_prompt_line_edit(find_buf, (int)strlen(find_buf),
                                        sizeof find_buf) < 0) {
                        tui_color(TUI_LGRAY, TUI_BLACK);
                        break;
                    }
                } else {
                    fb_prompt_line("", find_buf, sizeof find_buf);
                }
                tui_color(TUI_LGRAY, TUI_BLACK);
                if (!find_buf[0]) break;

                replace_buf[0] = '\0';
                prompt_bar("Replace with: ");
                fb_prompt_line("", replace_buf, sizeof replace_buf);
                tui_color(TUI_LGRAY, TUI_BLACK);

                spec.needle = find_buf;
                spec.replacement = replace_buf;
                spec.case_sensitive = 1;
                n = replace_all(&spec);

                {
                    char msg[64];
                    snprintf(msg, sizeof msg, "%d replacement%s", n, n == 1 ? "" : "s");
                    prompt_bar(msg);
                    tui_getch();
                    tui_color(TUI_LGRAY, TUI_BLACK);
                }
                clear_mark();
            }
            break;

        case 14:                                  /* Ctrl+N */
            new_document();
            break;
        case 27:                                  /* Esc: cancel, else new doc */
            if (mark_active || mark_exists) {
                clear_mark();
                break;
            }
            new_document();
            break;
            if (modified) {
                int k;
                prompt_bar("Save before quit? (y/n) ");
                k = tui_getch();
                tui_color(TUI_LGRAY, TUI_BLACK);
                if (k == 'y' || k == 'Y') save_current();
            }
            tui_cursor(1, 0);
            tui_clear();
            tui_restore();
            ts_free();
            tui_gotoxy(1, 1);
            return 0;
        case 13:
            clear_mark();
            split_line();
            break;
        case 8:
            clear_mark();
            del_back();
            break;
        default:
            if (ch >= 32 && ch < 127) {
                clear_mark();
                insert_char(ch);
            }
            break;
        }
    }
}
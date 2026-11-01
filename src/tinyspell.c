/* tinyspell.c - lightweight spell checker library
 * compile as part of tiny.exe:  tcc tiny.c tui_console.c tinyspell.c ...
 * compile as part of spell.exe: tcc spell.c tui_console.c tinyspell.c ...
 */

#include <stdio.h>
#include "file_fabgl.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tinyspell.h"
#ifdef FABLE_ESP32
#include <Arduino.h>
#include <esp32-hal-psram.h>
#endif

#define RESTART      8
#define MAX_GUESSES  95
#define EDIT_CAP     5
#define SCRATCH_CAP  4096
#define MAX_WORD     TS_MAX_WORD

/* ---------- little-endian helpers ---------- */
static unsigned long rd32(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* ---------- dictionary state ---------- */
static unsigned char *dict = NULL;
static unsigned long  dict_len = 0;
static unsigned long  word_count, restart_count, restart_off, data_off;

int ts_init(const char *path)
{
    FILE *f;
    long  n;

    ts_free();
    f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0x20) { fclose(f); return 0; }
    #ifdef FABLE_ESP32
        dict = (unsigned char *)ps_malloc((size_t)n);
    #else
        dict = (unsigned char *)malloc((size_t)n);
    #endif
    if (!dict) { fclose(f); return 0; }
    if (fread(dict, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(dict); dict = NULL; return 0; }
    fclose(f);
    if (memcmp(dict, "SMD3", 4) != 0) { free(dict); dict = NULL; return 0; }
    dict_len      = (unsigned long)n;
    word_count    = rd32(dict + 0x04);
    restart_count = rd32(dict + 0x08);
    restart_off   = rd32(dict + 0x0C);
    data_off      = rd32(dict + 0x10);
    if (restart_off >= dict_len || data_off >= dict_len) {
        free(dict); dict = NULL; return 0;
    }
    return 1;
}

void ts_free(void)
{
    if (dict) { free(dict); dict = NULL; }
    dict_len = word_count = restart_count = restart_off = data_off = 0;
}

/* ---------- decode helpers ---------- */
static void decode_restart(unsigned long r, char *out) {
    unsigned long o, base;
    unsigned char slen;
    if (r >= restart_count) { out[0] = 0; return; }
    base = restart_off + 4*r;
    if (base + 4 > dict_len) { out[0] = 0; return; }
    o = rd32(dict + base);
    if (data_off + o + 2 > dict_len) { out[0] = 0; return; }
    slen = dict[data_off + o + 1];
    if (slen >= MAX_WORD) slen = MAX_WORD - 1;
    if (data_off + o + 2 + slen > dict_len) { out[0] = 0; return; }
    memcpy(out, dict + data_off + o + 2, slen);
    out[slen] = 0;
}

static void decode_next(unsigned long *po, const char *prev, char *out) {
    unsigned long o = *po;
    unsigned char shared, slen;
    if (data_off + o + 2 > dict_len) { out[0] = 0; return; }
    shared = dict[data_off + o];
    slen   = dict[data_off + o + 1];
    if (shared >= MAX_WORD) shared = MAX_WORD - 1;
    if (slen >= MAX_WORD - shared) slen = MAX_WORD - shared - 1;
    if (data_off + o + 2 + slen > dict_len) { out[0] = 0; return; }
    memcpy(out, prev, shared);
    memcpy(out + shared, dict + data_off + o + 2, slen);
    out[shared + slen] = 0;
    *po = o + 2 + slen;
}

/* ---------- lookup ---------- */
int ts_check(const char *key)
{
    long lo = 0, hi = (long)restart_count - 1, mid;
    char buf[MAX_WORD], prev[MAX_WORD];
    unsigned long o;
    int i;

    if (!dict || restart_count == 0 || !key || !key[0]) return 0;

    while (lo < hi) {
        mid = (lo + hi + 1) / 2;
        decode_restart((unsigned long)mid, buf);
        if (strcmp(buf, key) <= 0) lo = mid; else hi = mid - 1;
    }
    o = rd32(dict + restart_off + 4*lo);
    prev[0] = 0;
    for (i = 0; i < RESTART; i++) {
        decode_next(&o, prev, buf);
        if (buf[0] == 0) break;
        if (strcmp(buf, key) == 0) return 1;
        if (strcmp(buf, key) > 0)  return 0;
        strcpy(prev, buf);
    }
    return 0;
}

/* ---------- Damerau-Levenshtein ---------- */
static int edit_distance(const char *a, const char *b, int cap) {
    int prev[MAX_WORD+1], cur[MAX_WORD+1], prev2[MAX_WORD+1];
    int la = (int)strlen(a), lb = (int)strlen(b);
    int i, j, cost, row_min, result;
    int *p, *c, *p2, *t;

    if (la > MAX_WORD || lb > MAX_WORD) return cap + 1;
    if (abs(la - lb) > cap) return cap + 1;

    for (j = 0; j <= lb; j++) prev[j] = j;
    p2 = prev2; p = prev; c = cur;

    for (i = 1; i <= la; i++) {
        c[0] = i; row_min = i;
        for (j = 1; j <= lb; j++) {
            cost = (a[i-1] == b[j-1]) ? 0 : 1;
            c[j] = p[j] + 1;
            if (c[j-1] + 1 < c[j]) c[j] = c[j-1] + 1;
            if (p[j-1] + cost < c[j]) c[j] = p[j-1] + cost;
            if (i > 1 && j > 1 && a[i-1] == b[j-2] && a[i-2] == b[j-1]) {
                if (p2[j-2] + 1 < c[j]) c[j] = p2[j-2] + 1;
            }
            if (c[j] < row_min) row_min = c[j];
        }
        if (row_min > cap) return cap + 1;
        t = p2; p2 = p; p = c; c = t;
    }
    result = p[lb];
    return result;
}

/* ---------- suggestions ---------- */
static int common_prefix(const char *a, const char *b) {
    int n = 0;
    while (a[n] && b[n] && a[n] == b[n]) n++;
    return n;
}

typedef struct { int dist; char word[MAX_WORD]; } Cand;

static const char *sort_key;

static int cmp_cand(const void *x, const void *y) {
    const Cand *a = (const Cand *)x, *b = (const Cand *)y;
    int pa, pb;
    if (a->dist != b->dist) return a->dist - b->dist;
    pa = common_prefix(sort_key, a->word);
    pb = common_prefix(sort_key, b->word);
    if (pa != pb) return pb - pa;
    return strcmp(a->word, b->word);
}

int ts_suggest(const char *key, char out[][TS_MAX_WORD], int max)
{
    unsigned long r, o;
    char buf[MAX_WORD], prev[MAX_WORD];
    int i, n = 0, k;
    int klen;
    int lo, hi;
    Cand *c;

    if (!dict || !key || !key[0] || max <= 0) return 0;
    klen = (int)strlen(key);
    lo = klen - 2; hi = klen + 2;
    if (lo < 1) lo = 1;

    c = (Cand *)malloc(sizeof(Cand) * SCRATCH_CAP);
    if (!c) return 0;

    for (r = 0; r < restart_count && n < SCRATCH_CAP; r++) {
        if (restart_off + 4*r + 4 > dict_len) break;
        o = rd32(dict + restart_off + 4*r);
        prev[0] = 0;
        for (i = 0; i < RESTART; i++) {
            if (data_off + o + 2 > dict_len) break;
            decode_next(&o, prev, buf);
            if (buf[0] == 0) break;
            strcpy(prev, buf);
            if (buf[0] != key[0]) continue;
            if ((int)strlen(buf) < lo || (int)strlen(buf) > hi) continue;
            k = edit_distance(key, buf, EDIT_CAP);
            if (k <= EDIT_CAP && n < SCRATCH_CAP) {
                c[n].dist = k;
                strncpy(c[n].word, buf, MAX_WORD - 1);
                c[n].word[MAX_WORD - 1] = 0;
                n++;
            }
        }
    }
    sort_key = key;
    qsort(c, n, sizeof(Cand), cmp_cand);
    for (i = 0; i < n && i < max; i++) strcpy(out[i], c[i].word);
    free(c);
    return (n < max) ? n : max;
}
/* ============================================================
 * Layer 2: interactive UI
 * ============================================================ */

#include "tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define PAGE_SIZE   5
#define MAX_GUESSES 95
#define SCRW        80
#define SCRH        24

/* ---------- case ---------- */
static void match_case(const char *tmpl, const char *word, char *out) {
    int i, upper = 1, lower = 1;
    for (i = 0; tmpl[i]; i++) {
        if (isupper((unsigned char)tmpl[i])) lower = 0;
        if (islower((unsigned char)tmpl[i])) upper = 0;
    }
    if (upper) { for (i = 0; word[i]; i++) out[i] = toupper((unsigned char)word[i]); out[i] = 0; }
    else if (lower) { for (i = 0; word[i]; i++) out[i] = tolower((unsigned char)word[i]); out[i] = 0; }
    else if (isupper((unsigned char)tmpl[0])) {
        out[0] = toupper((unsigned char)word[0]);
        for (i = 1; word[i]; i++) out[i] = tolower((unsigned char)word[i]);
        out[i] = 0;
    } else { for (i = 0; word[i]; i++) out[i] = tolower((unsigned char)word[i]); out[i] = 0; }
}

/* ---------- text helpers ---------- */
static int next_word(const char *text, int len, int *pos, int *start) {
    int i = *pos;
    while (i < len && !isalpha((unsigned char)text[i])) i++;
    *start = i;
    while (i < len && (isalpha((unsigned char)text[i]) || text[i] == '\'')) i++;
    *pos = i;
    return i - *start;
}

static void sentence_bounds(const char *text, int len, int start, int end,
                            int *sbeg, int *send, int *ws, int *we) {
    int i;
    *sbeg = 0;
    for (i = start - 1; i >= 0; i--) {
        if (text[i] == '.' || text[i] == '!' || text[i] == '?' || text[i] == '\n') { *sbeg = i + 1; break; }
    }
    *send = len;
    for (i = end; i < len; i++) {
        if (text[i] == '.' || text[i] == '!' || text[i] == '?' || text[i] == '\n') { *send = i + 1; break; }
    }
    while (*sbeg < *send && isspace((unsigned char)text[*sbeg])) (*sbeg)++;
    *ws = start - *sbeg;
    *we = end - *sbeg;
}

/* ---------- input at fixed position ---------- */
static int read_line_at(int x, int y, char *buf, int n)
{
    int i = 0, c;
    tui_gotoxy(x, y);
    for (;;) {
        c = tui_getch();
        if (c == -1) break;
        if (c == 27) { buf[0] = 0; return -1; }
        if (c == '\n' || c == '\r') break;
        if (c == 8 || c == 127) {
            if (i > 0) {
                i--;
                tui_putc('\b'); tui_putc(' '); tui_putc('\b');
            }
            continue;
        }
        if (i < n - 1) { buf[i++] = (char)c; tui_putc(c); }
    }
    buf[i] = 0;
    return i;
}

/* ---------- sentence display ---------- */
static void print_sentence(const char *text, int sbeg, int wstart, int wlen, int send) {
    int k;
    for (k = sbeg; k < wstart && k - sbeg < SCRW; k++) tui_putc(text[k]);
    if (k - sbeg >= SCRW) return;
    tui_color(TUI_LRED, TUI_BLACK);
    tui_putc('[');
    for (; k < wstart + wlen && k - sbeg < SCRW - 1; k++) tui_putc(text[k]);
    tui_putc(']');
    tui_color(TUI_LGRAY, TUI_BLACK);
    for (; k < send && k - sbeg < SCRW; k++) tui_putc(text[k]);
}

/* ---------- menu ---------- */
static int prompt_choice(const char *word,
                         const char *text, int sbeg, int wstart, int wlen, int send,
                         int use_colour,
                         char gs[][TS_MAX_WORD], int ng) {
    int page = 0;
    for (;;) {
        int start = page * PAGE_SIZE, end = start + PAGE_SIZE;
        int i;
        char line[16];

        if (end > ng) end = ng;

        tui_clear();

        tui_gotoxy(1, 1);
        if (use_colour) print_sentence(text, sbeg, wstart, wlen, send);
        else {
            int k;
            for (k = 0; text[k] && k < SCRW; k++) tui_putc(text[k]);
        }

        for (i = start; i < end; i++) {
            char m[TS_MAX_WORD];
            char row[SCRW + 1];
            match_case(word, gs[i], m);
            snprintf(row, sizeof row, "   %d. %s", i - start + 1, m);
            tui_gotoxy(1, 3 + (i - start));
            tui_puts(row);
        }

        {
            int row = 3 + (end - start);
            if (end < ng) {
                char more[64];
                snprintf(more, sizeof more, "   6. more guesses (%d left)", ng - end);
                tui_gotoxy(1, row); tui_puts(more); row++;
            }
            tui_gotoxy(1, row);     tui_puts("   7. ignore");
            tui_gotoxy(1, row + 1); tui_puts("   8. replace");
            tui_gotoxy(1, row + 2); tui_puts("   ESC to quit");
        }

        tui_gotoxy(1, SCRH);
        tui_color(TUI_BLACK, TUI_LGRAY);
        tui_puts("choose: ");
        tui_color(TUI_LGRAY, TUI_BLACK);

        {
        int r = read_line_at(9, SCRH, line, sizeof line);
        if (r < 0) return -1;      /* Esc = quit */
        if (r == 0) return 0;      /* empty Enter = ignore */
        }

        {
            int c = (unsigned char)line[0];
          /*  if (c == 'q' || c == 'Q') return -1;*/
            if (c == 27) return 0;
            if (c >= '1' && c <= '5') {
                int n = c - '1';
                if (start + n < end) return start + n + 1;
            }
            if (c == '6') { if (end < ng) page++; continue; }
            if (c == '7') return -2;
            if (c == '8') return -3;
        }
    }
}

/* ---------- replacement prompt ---------- */
static int ask_replacement(const char *original, char out[TS_MAX_WORD]) {
    char line[TS_MAX_WORD];
    char key[TS_MAX_WORD];
    for (;;) {
        int i;
        tui_clear();
        tui_gotoxy(1, 1); tui_puts("replace: ");
        tui_color(TUI_LRED, TUI_BLACK); tui_puts(original);
        tui_color(TUI_LGRAY, TUI_BLACK);
        tui_gotoxy(1, 2); tui_puts("(q = quit, empty = cancel, letters only)");
        tui_gotoxy(1, SCRH);
        tui_color(TUI_BLACK, TUI_LGRAY); tui_puts("new word: ");
        tui_color(TUI_LGRAY, TUI_BLACK);

        {
            int r = read_line_at(11, SCRH, line, TS_MAX_WORD);
            if (r < 0) return -1;      /* Esc = quit */
            if (r == 0) return 0;      /* empty = cancel */
        }
        /* remove: if (line[0] == 'q' || line[0] == 'Q') return -1; */

        {
            int ok = 1;
            for (i = 0; line[i]; i++)
                if (!isalpha((unsigned char)line[i])) { ok = 0; break; }
            if (!ok) {
                tui_gotoxy(1, 3);
                tui_color(TUI_LRED, TUI_BLACK);
                tui_puts("letters only - press any key");
                tui_color(TUI_LGRAY, TUI_BLACK);
                tui_getch();
                continue;
            }
        }

        for (i = 0; line[i]; i++) key[i] = toupper((unsigned char)line[i]);
        key[i] = 0;

        if (ts_check(key)) { strcpy(out, line); return 1; }

        {
            static char gs[MAX_GUESSES][TS_MAX_WORD];
            char hdr[TS_MAX_WORD + 32];
            int ng, r;
            snprintf(hdr, sizeof hdr, "[%s] not found. replace [%s] with:", line, original);
            ng = ts_suggest(key, gs, MAX_GUESSES);
            r = prompt_choice(line, hdr, 0, 0, 0, (int)strlen(hdr), 0, gs, ng);
            if (r == -1) return -1;
            if (r == -2 || r == 0) continue;
            if (r > 0) { match_case(line, gs[r-1], out); return 1; }
        }
    }
}

/* ---------- the one Tiny calls ---------- */
int ts_fix_interactive(char **text, int *len)
{
    int pos = 0, fixes = 0, quit = 0;
    char *t = *text;
    int total = *len;

    if (!t || total <= 0) return 0;

    while (pos < total) {
        int wstart, wlen, sbeg, send, ws, we;
        char word[TS_MAX_WORD], key[TS_MAX_WORD];
        int i;

        wlen = next_word(t, total, &pos, &wstart);
        if (wlen == 0) break;
        if (wlen >= TS_MAX_WORD) continue;
        memcpy(word, t + wstart, wlen); word[wlen] = 0;
        for (i = 0; i < wlen; i++) key[i] = toupper((unsigned char)word[i]);
        key[wlen] = 0;
        if (ts_check(key)) continue;

        sentence_bounds(t, total, wstart, wstart + wlen, &sbeg, &send, &ws, &we);

        {
            static char gs[MAX_GUESSES][TS_MAX_WORD];
            int ng = ts_suggest(key, gs, MAX_GUESSES);
            int r = prompt_choice(word, t, sbeg, wstart, wlen, send, 1, gs, ng);

            if (r == -1) { quit = 1; break; }
            if (r == -2 || r == 0) continue;

            if (r == -3) {
                char rep[TS_MAX_WORD];
                int kk = ask_replacement(word, rep);
                if (kk == -1) { quit = 1; break; }
                if (kk == 0) continue;
                {
                    int rl = (int)strlen(rep);
                    char *nt = (char *)malloc((size_t)total + rl + 1);
                    if (!nt) break;
                    memcpy(nt, t, wstart);
                    memcpy(nt + wstart, rep, rl);
                    memcpy(nt + wstart + rl, t + wstart + wlen, total - wstart - wlen + 1);
                    free(t); t = nt;
                    total = total - wlen + rl;
                    pos = wstart + rl;
                    fixes++;
                }
                continue;
            }

            {
                char rep[TS_MAX_WORD];
                int rl;
                match_case(word, gs[r-1], rep);
                rl = (int)strlen(rep);
                {
                    char *nt = (char *)malloc((size_t)total + rl + 1);
                    if (!nt) break;
                    memcpy(nt, t, wstart);
                    memcpy(nt + wstart, rep, rl);
                    memcpy(nt + wstart + rl, t + wstart + wlen, total - wstart - wlen + 1);
                    free(t); t = nt;
                    total = total - wlen + rl;
                    pos = wstart + rl;
                    fixes++;
                }
            }
        }
    }

    *text = t;
    *len = total;
    (void)quit;
    return fixes;
}
/* block.c - Ctrl+B mark and the clipboard (copy, cut, paste). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tui.h"
#include "editor.h"

Mark ed_mark;

int ed_mark_active = 0;

int ed_mark_exists = 0;

char *ed_clip = NULL;

long ed_clip_len = 0;

static long clip_cap = 0;

void ed_clear_mark(void)
{
    ed_mark_active = 0;
    ed_mark_exists = 0;
}

static void mark_range(int *l1, int *l2, int *c1, int *c2)
{
    if (ed_mark.sl < ed_mark.el) {
        *l1 = ed_mark.sl; *l2 = ed_mark.el;
        *c1 = ed_mark.sc; *c2 = ed_mark.ec;
    } else if (ed_mark.sl > ed_mark.el) {
        *l1 = ed_mark.el; *l2 = ed_mark.sl;
        *c1 = ed_mark.ec; *c2 = ed_mark.sc;
    } else {
        *l1 = *l2 = ed_mark.sl;
        if (ed_mark.sc < ed_mark.ec) { *c1 = ed_mark.sc; *c2 = ed_mark.ec; }
        else                   { *c1 = ed_mark.ec; *c2 = ed_mark.sc; }
    }
}

int ed_in_mark(int line, int col)
{
    int l1, l2, c1, c2;
    if (!ed_mark_exists) return 0;
    mark_range(&l1, &l2, &c1, &c2);
    if (line < l1 || line > l2) return 0;
    if (l1 == l2) return col >= c1 && col < c2;
    if (line == l1) return col >= c1;
    if (line == l2) return col < c2;
    return 1;
}

void ed_clip_clear(void) { ed_clip_len = 0; }

static int clip_append(const char *s, long n)
{
    if (ed_clip_len + n > clip_cap) {
        long new_cap = clip_cap ? clip_cap : 256;
        char *new_buf;
        while (new_cap < ed_clip_len + n) new_cap *= 2;
        new_buf = (char *)realloc(ed_clip, (size_t)new_cap);
        if (!new_buf) return 0;
        ed_clip = new_buf;
        clip_cap = new_cap;
    }
    memcpy(ed_clip + ed_clip_len, s, (size_t)n);
    ed_clip_len += n;
    return 1;
}

void ed_copy_mark_to_clip(void)
{
    int l1, l2, c1, c2, l;
    ed_clip_clear();
    if (!ed_mark_exists) return;
    mark_range(&l1, &l2, &c1, &c2);
    for (l = l1; l <= l2; l++) {
        int start = (l == l1) ? c1 : 0;
        int end   = (l == l2) ? c2 : (int)strlen(ed_lines[l]);
        int len   = end - start;
        if (len < 0) len = 0;
        if (len > 0) clip_append(&ed_lines[l][start], (long)len);
        if (l < l2)  clip_append("\n", 1);
    }
}

void ed_delete_mark(void)
{
    int l1, l2, c1, c2, i, len;

    if (!ed_mark_exists) return;
    mark_range(&l1, &l2, &c1, &c2);

    if (l1 == l2) {
        len = (int)strlen(ed_lines[l1]);
        if (c2 > len) c2 = len;
        if (c1 > c2) c1 = c2;
        memmove(&ed_lines[l1][c1],
                &ed_lines[l1][c2],
                (size_t)(len - c2 + 1));
        ed_cur_line = l1;
        ed_cur_col  = c1;
    } else {
        int first_len = (int)strlen(ed_lines[l1]);
        int last_len  = (int)strlen(ed_lines[l2]);
        int keep_first, keep_last, after;

        if (c1 > first_len) c1 = first_len;
        if (c2 > last_len)  c2 = last_len;
        keep_first = c1;
        keep_last  = last_len - c2;

        if (keep_first + keep_last + 1 <= MAXLINE) {
            memmove(&ed_lines[l1][keep_first],
                    &ed_lines[l2][c2],
                    (size_t)(keep_last + 1));
        } else {
            ed_lines[l1][keep_first] = '\0';
        }

        after = ed_nlines - l2 - 1;
        for (i = 0; i < after; i++) {
            size_t ln = strlen(ed_lines[l2 + 1 + i]) + 1;
            memcpy(ed_lines[l1 + 1 + i], ed_lines[l2 + 1 + i], ln);
        }
        ed_nlines -= (l2 - l1);

        ed_cur_line = l1;
        ed_cur_col  = keep_first;
    }
    ed_modified = 1;
    ed_spell_marks_off();
}

void ed_paste_clip(void)
{
    long i;
    for (i = 0; i < ed_clip_len; i++) {
        if (ed_clip[i] == '\n') ed_split_line();
        else                  ed_insert_char((unsigned char)ed_clip[i]);
    }
    ed_modified = 1;
    ed_spell_marks_off();
}

/* edit.c - cursor movement and text edits (insert, split, join, delete). */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "tui.h"
#include "editor.h"

static void para_up(void)
{
    int ln = ed_cur_line;
    /* skip blank lines above */
    while (ln > 0 && ed_lines[ln][0] == '\0') ln--;
    /* if we're on a non-blank line, we're in a paragraph — go to its top */
    while (ln > 0 && ed_lines[ln - 1][0] != '\0') ln--;
    /* if we were already at the top, jump to the previous paragraph */
    if (ln == ed_cur_line) {
        while (ln > 0 && ed_lines[ln - 1][0] == '\0') ln--;
        while (ln > 0 && ed_lines[ln - 1][0] != '\0') ln--;
    }
    ed_cur_line = ln;
    ed_cur_col  = 0;
}

static void para_down(void)
{
    int ln = ed_cur_line;
    /* skip blank lines below */
    while (ln < ed_nlines - 1 && ed_lines[ln][0] == '\0') ln++;
    /* if we're on a non-blank line, we're in a paragraph — go to its bottom */
    while (ln < ed_nlines - 1 && ed_lines[ln + 1][0] != '\0') ln++;
    /* if we were already at the bottom, jump to the next paragraph */
    if (ln == ed_cur_line) {
        while (ln < ed_nlines - 1 && ed_lines[ln + 1][0] == '\0') ln++;
        while (ln < ed_nlines - 1 && ed_lines[ln + 1][0] != '\0') ln++;
    }
    ed_cur_line = ln;
    ed_cur_col  = 0;
}

void ed_clamp_col(void)
{
    int len = (int)strlen(ed_lines[ed_cur_line]);
    if (ed_cur_col > len) ed_cur_col = len;
    if (ed_cur_col < 0) ed_cur_col = 0;
}

static void word_left(void)
{
    if (ed_cur_col == 0) {
        if (ed_cur_line > 0) {
            ed_cur_line--;
            ed_cur_col = (int)strlen(ed_lines[ed_cur_line]);
        }
        return;
    }
    while (ed_cur_col > 0 && !IS_WORD(ed_lines[ed_cur_line][ed_cur_col - 1])) ed_cur_col--;
    while (ed_cur_col > 0 &&  IS_WORD(ed_lines[ed_cur_line][ed_cur_col - 1])) ed_cur_col--;
}

static void word_right(void)
{
    int len = (int)strlen(ed_lines[ed_cur_line]);
    if (ed_cur_col >= len) {
        if (ed_cur_line < ed_nlines - 1) { ed_cur_line++; ed_cur_col = 0; }
        return;
    }
    while (ed_cur_col < len &&  IS_WORD(ed_lines[ed_cur_line][ed_cur_col])) ed_cur_col++;
    while (ed_cur_col < len && !IS_WORD(ed_lines[ed_cur_line][ed_cur_col])) ed_cur_col++;
}

void ed_insert_char(int c)
{
    int len = (int)strlen(ed_lines[ed_cur_line]);
    if (ed_cur_col > len) ed_cur_col = len;
    if (len >= MAXLINE - 1) return;
    memmove(&ed_lines[ed_cur_line][ed_cur_col + 1],
            &ed_lines[ed_cur_line][ed_cur_col],
            (size_t)(len - ed_cur_col + 1));
    ed_lines[ed_cur_line][ed_cur_col] = (char)c;
    ed_cur_col++;
    ed_modified = 1;
    ed_spell_marks_off();
}

void ed_split_line(void)
{
    char tailbuf[MAXLINE];
    int len, tail;
    if (ed_nlines >= MAXLINES) return;
    len = (int)strlen(ed_lines[ed_cur_line]);
    if (ed_cur_col > len) ed_cur_col = len;
    tail = len - ed_cur_col;

    memcpy(tailbuf, &ed_lines[ed_cur_line][ed_cur_col], (size_t)(tail + 1));
    ed_lines[ed_cur_line][ed_cur_col] = '\0';

    memmove(&ed_lines[ed_cur_line + 2], &ed_lines[ed_cur_line + 1],
            (size_t)((ed_nlines - ed_cur_line - 1) * MAXLINE));

    memcpy(ed_lines[ed_cur_line + 1], tailbuf, (size_t)(tail + 1));

    ed_nlines++;
    ed_cur_line++;
    ed_cur_col = 0;
    ed_modified = 1;
    ed_spell_marks_off();
}

static void join(int a, int b)
{
    int alen = (int)strlen(ed_lines[a]);
    int blen = (int)strlen(ed_lines[b]);
    int i;
    if (alen + blen >= MAXLINE - 1) return;
    memcpy(&ed_lines[a][alen], ed_lines[b], (size_t)(blen + 1));
    for (i = b; i < ed_nlines - 1; i++) {
        size_t l = strlen(ed_lines[i + 1]) + 1;
        memcpy(ed_lines[i], ed_lines[i + 1], l);
    }
    ed_nlines--;
    ed_cur_line = a;
    ed_cur_col  = alen;
    ed_modified = 1;
    ed_spell_marks_off();
}

void ed_del_back(void)
{
    int len = (int)strlen(ed_lines[ed_cur_line]);
    if (ed_cur_col > 0) {
        memmove(&ed_lines[ed_cur_line][ed_cur_col - 1],
                &ed_lines[ed_cur_line][ed_cur_col],
                (size_t)(len - ed_cur_col + 1));
        ed_cur_col--;
        ed_modified = 1;
    } else if (ed_cur_line > 0) {
        join(ed_cur_line - 1, ed_cur_line);
    }
    ed_spell_marks_off();
}

void ed_del_forward(void)
{
    int len = (int)strlen(ed_lines[ed_cur_line]);
    if (ed_cur_col < len) {
        memmove(&ed_lines[ed_cur_line][ed_cur_col],
                &ed_lines[ed_cur_line][ed_cur_col + 1],
                (size_t)(len - ed_cur_col));
        ed_modified = 1;
    } else if (ed_cur_line < ed_nlines - 1) {
        join(ed_cur_line, ed_cur_line + 1);
    }
    ed_spell_marks_off();
}

static int shift_down(void)
{
    return tui_shift_down();
}

void ed_move_arrow(int code)
{
    int shift = shift_down();
    switch (code) {
    case 72:                                       /* Up */
        if (shift) para_up();
        else if (ed_cur_line > 0) { ed_cur_line--; ed_clamp_col(); }
        break;
    case 80:                                       /* Down */
        if (shift) para_down();
        else if (ed_cur_line < ed_nlines - 1) { ed_cur_line++; ed_clamp_col(); }
        break;
    case 75:
        if (shift) word_left();
        else if (ed_cur_col > 0) ed_cur_col--;
        else if (ed_cur_line > 0) {
            ed_cur_line--;
            ed_cur_col = (int)strlen(ed_lines[ed_cur_line]);
        }
        break;
    case 77:
        if (shift) word_right();
        else if (ed_cur_col < (int)strlen(ed_lines[ed_cur_line])) ed_cur_col++;
        else if (ed_cur_line < ed_nlines - 1) { ed_cur_line++; ed_cur_col = 0; }
        break;
    case 71: ed_cur_col = 0; break;
    case 79: ed_cur_col = (int)strlen(ed_lines[ed_cur_line]); break;
    case 73:
        ed_cur_line -= SCRH - 1;
        if (ed_cur_line < 0) ed_cur_line = 0;
        ed_clamp_col();
        break;
    case 81:
        ed_cur_line += SCRH - 1;
        if (ed_cur_line >= ed_nlines) ed_cur_line = ed_nlines - 1;
        ed_clamp_col();
        break;
    }
}

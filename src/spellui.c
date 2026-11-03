/* spellui.c - spell marks (F5) and interactive spellcheck (F4). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tui.h"
#include "tinyspell.h"
#include "editor.h"

int ed_spell_marks_on = 0;

int ed_spell_ok = 0;

unsigned char (*ed_spell_bad)[MAXLINE / 8];

static void spell_set(int line, int col) {
    ed_spell_bad[line][col / 8] |= (unsigned char)(1 << (col % 8));
}

int ed_spell_is_bad(int line, int col) {
    return (ed_spell_bad[line][col / 8] >> (col % 8)) & 1;
}

static void spell_clear_line(int line) {
    if (line >= 0 && line < MAXLINES)
        memset(ed_spell_bad[line], 0, MAXLINE / 8);
}

static void spell_mark_line(int line)
{
    char *p;
    int col = 0;

    spell_clear_line(line);
    if (!ed_spell_ok || !ed_spell_marks_on) return;
    if (line < 0 || line >= ed_nlines) return;

    p = ed_lines[line];
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
    if (ed_spell_marks_on && ed_spell_ok) spell_mark_line(line);
}

void ed_spell_mark_all(void)
{
    int i;
    memset(ed_spell_bad, 0, (size_t)MAXLINES * (MAXLINE / 8));
    for (i = 0; i < ed_nlines; i++) spell_mark_line(i);
}

void ed_spell_marks_off(void)
{
    ed_spell_marks_on = 0;
    /*memset(spell_bad, 0, sizeof(spell_bad)); */
}

static int spell_buffer(void)
{
    int i, n = 0;
    for (i = 0; i < ed_nlines; i++) {
        char *p = ed_lines[i];
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

void ed_spell_check(void)
{
    char *text;
    int total = 0, len, fixes, i, off;

    ed_clear_mark();

    if (!ed_spell_ok) {
        ed_status_msg("Dictionary not loaded.");
        tui_getch();
        return;
    }

    for (i = 0; i < ed_nlines; i++) total += (int)strlen(ed_lines[i]) + 1;
    text = (char *)malloc((size_t)total + 1);
    if (!text) { ed_status_msg("Out of memory."); tui_getch(); return; }

    off = 0;
    for (i = 0; i < ed_nlines; i++) {
        int l = (int)strlen(ed_lines[i]);
        memcpy(text + off, ed_lines[i], (size_t)l);
        off += l;
        text[off++] = '\n';
    }
    text[off] = '\0';
    len = off;

    tui_clear();
    fixes = ts_fix_interactive(&text, &len);

    /* rebuild lines[] from text */
    ed_nlines = 0;
    {
        int start = 0, k;
        for (k = 0; k <= len; k++) {
            if (k == len || text[k] == '\n') {
                int l = k - start;
                if (l > MAXLINE - 1) l = MAXLINE - 1;
                memcpy(ed_lines[ed_nlines], text + start, (size_t)l);
                ed_lines[ed_nlines][l] = '\0';
                ed_nlines++;
                if (ed_nlines >= MAXLINES) break;
                start = k + 1;
            }
        }
        if (ed_nlines == 0) { ed_lines[0][0] = '\0'; ed_nlines = 1; }
    }

    free(text);

    if (fixes > 0) ed_modified = 1;

    {
        char msg[64];
        snprintf(msg, sizeof msg, "%d fix%s", fixes, fixes == 1 ? "" : "es");
        ed_status_msg(msg);
    }

    if (ed_cur_line >= ed_nlines) ed_cur_line = ed_nlines - 1;
    if (ed_cur_line < 0) ed_cur_line = 0;
    ed_clamp_col();
    ed_top_line = 0;
    ed_left_col = 0;
    ed_spell_marks_off(); 
}

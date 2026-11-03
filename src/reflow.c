/* reflow.c - paragraph reflow (F2). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tui.h"
#include "editor.h"

/* length of the next output line from p (remain chars left) */
static int wrap_len(const char *p, int remain, int width)
{
    int brk;
    if (remain <= width) return remain;
    brk = width;
    while (brk > 0 && p[brk] != ' ') brk--;
    return brk > 0 ? brk : width;        /* no space: hard-break at width */
}

void ed_reflow(int width)
{
    int  pstart, pend, i, j;
    char *para;
    long total;
    int  plen, pos, outcount, old_count, diff;
    int  changed = 0;

    if (width < 10)  width = 10;
    if (width > 120) width = 120;

    if (ed_lines[ed_cur_line][0] == '\0') return;

    /* paragraph bounds around the cursor */
    pstart = ed_cur_line;
    pend   = ed_cur_line;
    while (pstart > 0 && ed_lines[pstart - 1][0] != '\0') pstart--;
    while (pend < ed_nlines - 1 && ed_lines[pend + 1][0] != '\0') pend++;

    /* indented paragraph: treat as preformatted, don't touch */
    if (ed_lines[pstart][0] == ' ' || ed_lines[pstart][0] == '\t') return;

    /* joined text is never longer than the sum of the lines + joiners */
    total = 1;
    for (i = pstart; i <= pend; i++) total += (long)strlen(ed_lines[i]) + 1;
    para = (char *)malloc((size_t)total);
    if (!para) return;

    /* join lines; leading whitespace dropped, runs of spaces/tabs -> one space */
    plen = 0;
    for (i = pstart; i <= pend; i++) {
        const char *s = ed_lines[i];
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
    if (diff > 0 && ed_nlines + diff > MAXLINES) { free(para); return; }
    if (diff != 0) {
        int tail = ed_nlines - pend - 1;
        if (diff > 0) {
            for (j = 0; j < tail; j++)
                memcpy(ed_lines[ed_nlines + diff - 1 - j], ed_lines[ed_nlines - 1 - j], MAXLINE);
        } else {
            for (j = 0; j < tail; j++)
                memcpy(ed_lines[pend + 1 + diff + j], ed_lines[pend + 1 + j], MAXLINE);
        }
        ed_nlines += diff;
        changed = 1;
    }

    /* write wrapped lines */
    pos = 0;
    for (i = pstart; pos < plen; i++) {
        int len = wrap_len(para + pos, plen - pos, width);
        if (strlen(ed_lines[i]) != (size_t)len ||
            memcmp(ed_lines[i], para + pos, (size_t)len) != 0)
            changed = 1;
        memcpy(ed_lines[i], para + pos, (size_t)len);
        ed_lines[i][len] = '\0';
        pos += len;
        if (pos < plen && para[pos] == ' ') pos++;
    }

    free(para);
    if (changed) ed_modified = 1;

    ed_cur_line = pstart;          /* cursor to start of the reflowed paragraph */
    ed_cur_col  = 0;
    ed_spell_marks_off();
}

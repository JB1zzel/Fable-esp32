/* find.c - find (Ctrl+F) and find-and-replace-all (Ctrl+R). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tui.h"
#include "editor.h"

int ed_find_next(const char *needle)
{
    int nlen = (int)strlen(needle);
    int start_line = ed_cur_line;
    int start_col  = ed_cur_col + 1;
    int i;

    if (nlen == 0) return 0;

    for (i = 0; i <= ed_nlines; i++) {              /* <= so we come back to the start line */
        int ln = (start_line + i) % ed_nlines;
        const char *s = ed_lines[ln];
        int slen = (int)strlen(s);
        int from = (i == 0) ? start_col : 0;
        int to   = (i == ed_nlines) ? start_col + nlen - 1 : slen;   /* last pass: stop where we began */
        int k;

        if (from > slen) from = slen;
        if (to   > slen) to   = slen;
        for (k = from; k + nlen <= to; k++) {
            if (memcmp(&s[k], needle, (size_t)nlen) == 0) {
                ed_cur_line = ln;
                ed_cur_col  = k;
                return 1;
            }
        }
    }
    return 0;
}

int ed_replace_all(const FindSpec *spec)
{
    int nlen, rlen, count = 0;
    int l;
    int replaced_any = 0;

    if (!spec || !spec->needle || !spec->needle[0]) return 0;
    if (strchr(spec->needle, '\n')) return 0;

    nlen = (int)strlen(spec->needle);
    rlen = (int)strlen(spec->replacement);

    for (l = 0; l < ed_nlines; l++) {
        char out[MAXLINE];
        int outlen = 0;
        int pos = 0;
        int linelen = (int)strlen(ed_lines[l]);
        int line_changed = 0;

        while (pos < linelen) {
            int match = -1;
            int k;

            for (k = pos; k + nlen <= linelen; k++) {
                int ok;
                if (spec->case_sensitive) {
                    ok = memcmp(&ed_lines[l][k], spec->needle, (size_t)nlen) == 0;
                } else {
                    int j;
                    ok = 1;
                    for (j = 0; j < nlen; j++) {
                        if (tolower((unsigned char)ed_lines[l][k + j]) !=
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
                    memcpy(out + outlen, &ed_lines[l][pos], (size_t)rest);
                    outlen += rest;
                }
                break;
            }

            {
                int pre = match - pos;
                if (outlen + pre > MAXLINE - 1) pre = MAXLINE - 1 - outlen;
                if (pre > 0) {
                    memcpy(out + outlen, &ed_lines[l][pos], (size_t)pre);
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
            memcpy(ed_lines[l], out, (size_t)(outlen + 1));
            replaced_any = 1;
        }
    }

    if (replaced_any) {
        ed_modified = 1;
        if (ed_cur_line >= ed_nlines) ed_cur_line = ed_nlines - 1;
        if (ed_cur_line < 0) ed_cur_line = 0;
        {
            int len = (int)strlen(ed_lines[ed_cur_line]);
            if (ed_cur_col > len) ed_cur_col = len;
        }
        ed_spell_marks_off();
    }

    return count;
}

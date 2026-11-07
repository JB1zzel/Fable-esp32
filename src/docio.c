/* docio.c - loading, saving, Save As prompt and Ctrl+N new document. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tui.h"
#include "editor.h"

#include "fab_file.h"

static int load_truncated = 0;

static char trunc_name[260] = "";

/* returns 1 if the file was loaded, 0 if it could not be opened
 * (the current buffer is left untouched in that case) */
int ed_load_file(const char *fn)
{
    FILE *f;
    int c;
    if (!fab_exists(fn)) return 0;         /* avoids a VFS error log */
    f = fopen(fn, "rb");
    if (!f) return 0;
    load_truncated = 0;
    ed_nlines = 0;
    for (;;) {
        int len = 0;
        c = fgetc(f);
        while (c != EOF && c != '\n' && c != '\r') {
            if (len < MAXLINE - 1) ed_lines[ed_nlines][len++] = (char)c;
            else load_truncated = 1;
            c = fgetc(f);
        }
        ed_lines[ed_nlines][len] = '\0';
        ed_nlines++;
        if (c == EOF) break;
        if (c == '\r') {
            int c2 = fgetc(f);
            if (c2 != '\n' && c2 != EOF) ungetc(c2, f);
        }
        if (ed_nlines >= MAXLINES) {
            if (fgetc(f) != EOF) load_truncated = 1;   /* more data left */
            break;
        }
    }
    fclose(f);
    if (ed_nlines == 0) { ed_lines[0][0] = '\0'; ed_nlines = 1; }
    ed_modified = 0;
    ed_top_line = 0;
    ed_left_col = 0;
    ed_cur_line = 0;
    ed_cur_col = 0;
    ed_clear_mark();
    ed_spell_marks_off();
    if (load_truncated) {
        strncpy(trunc_name, fn, sizeof(trunc_name) - 1);
        trunc_name[sizeof(trunc_name) - 1] = '\0';
        ed_status_msg("File too big (max 256 lines / 1023 cols): saving it is disabled");
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

    if (load_truncated && strcmp(ed_filename, trunc_name) == 0) {
        ed_status_msg("Not saved: file was truncated when loaded (256 lines / 1023 cols)");
        tui_getch();
        return 0;
    }

    snprintf(tmp_path, sizeof tmp_path, "%s.tmp", ed_filename);
    f = fopen(tmp_path, "wb");
    if (!f) return 0;
    for (i = 0; i < ed_nlines; i++) {
        if (fputs(ed_lines[i], f) == EOF) ok = 0;
        if (i < ed_nlines - 1 && fputs("\r\n", f) == EOF) ok = 0;
    }
    if (fclose(f) == EOF) ok = 0;
    if (!ok) { fab_remove(tmp_path); return 0; }

    if (fab_exists(ed_filename)) fab_remove(ed_filename);
    if (fab_rename(tmp_path, ed_filename) != 0) return 0;  /* data kept in .tmp */

    ed_modified = 0;
    return 1;
}

int ed_save_current(void)
{
    if (!ed_filename[0]) {
        char path[260];
        ed_prompt_bar("Save as: ");
        path[0] = '\0';
        fb_prompt_line("", path, sizeof path);
        tui_color(TUI_LGRAY, TUI_BLACK);
        if (!path[0]) return 0;
        strncpy(ed_filename, path, sizeof(ed_filename) - 1);
        ed_filename[sizeof(ed_filename) - 1] = '\0';
    }
    return save_file();
}

int ed_new_document(void)
{
    char path[260];
    FILE *probe;

    ed_clear_mark();

    if (ed_modified) {
        int k;
        ed_prompt_bar("Save changes? (y/n/c) ");
        k = tui_getch();
        tui_color(TUI_LGRAY, TUI_BLACK);
        if (k == 'c' || k == 'C' || k == 27) return ED_DOC_STAY;
        if (k == 'y' || k == 'Y') {
            if (!ed_save_current()) return ED_DOC_STAY;
        }
    }

   
    if (!fb_show_files(path, sizeof path)) return ED_DOC_CANCELLED;

    ed_nlines = 1;
    ed_lines[0][0] = '\0';
    ed_modified = 0;
    ed_top_line = 0;
    ed_left_col = 0;
    ed_cur_line = 0;
    ed_cur_col  = 0;

    strncpy(ed_filename, path, sizeof(ed_filename) - 1);
    ed_filename[sizeof(ed_filename) - 1] = '\0';

    probe = fopen(ed_filename, "rb");
    if (probe) {
        fclose(probe);
        ed_load_file(ed_filename);
        ed_cur_line = 0;
        ed_cur_col  = 0;
    } else {
        FILE *out = fopen(ed_filename, "wb");
        if (out) fclose(out);
    }
    return ED_DOC_OPENED;
}

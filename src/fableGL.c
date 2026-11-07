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
 *   F6              diary calendar (one file per day in /diary)
 *   F7              games menu (snake, minesweeper, solitaire)
 *   F9              Wi-Fi setup (ESP32 only)
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
#include "tui.h"
#include "tinyspell.h"
#include "editor.h"

static void resolve_dict_path(void)
{
    char dir[260];
    tui_exe_dir(dir, sizeof dir);
    if (dir[0])
        snprintf(ed_dict_path, sizeof ed_dict_path, "%sdict.smd", dir);
}

int fable_main(int argc, char **argv)
{
    int ch;

    ed_lines     = (char (*)[MAXLINE])fable_big_calloc(MAXLINES, MAXLINE);
    ed_spell_bad = (unsigned char (*)[MAXLINE / 8])fable_big_calloc(MAXLINES, MAXLINE / 8);
    if (!ed_lines || !ed_spell_bad) return 1;

    diary_init();                    /* make sure the diary folder exists */

    tui_init();
    tui_clear();

    if (argc > 1) {
        strncpy(ed_filename, argv[1], sizeof(ed_filename) - 1);
        ed_filename[sizeof(ed_filename) - 1] = '\0';
    }

    tui_clear();

    ed_lines[0][0] = '\0';
    ed_nlines = 1;
    if (ed_filename[0]) ed_load_file(ed_filename);
    resolve_dict_path();

    if (ts_init(ed_dict_path)) {
        ed_spell_ok = 1;
    } else {
        char path[260];
        path[0] = '\0';
        ed_prompt_bar("Dictionary not found. Path [skip]: ");
        if (fb_prompt_line("", path, sizeof path) >= 0 && path[0]) {
            strncpy(ed_dict_path, path, sizeof(ed_dict_path) - 1);
            ed_dict_path[sizeof(ed_dict_path) - 1] = '\0';
            if (ts_init(ed_dict_path)) ed_spell_ok = 1;
        }
        tui_color(TUI_LGRAY, TUI_BLACK);
        tui_clear();
    }

    for (;;) {

        if (!tui_key_pending()) ed_render();
        ch = tui_getch();

        if (ch == 0 || ch == 224) {
            int code = tui_getch();
            if (code == 59) {          /* F1 */
                ed_clear_mark();
                fb_help_screen();
            } else if (code == 60) {                     /* F2: reflow */
                char buf[16];
                int width = 80;
                buf[0] = '\0';
                ed_prompt_bar("Width [80]: ");
                if (fb_prompt_line("", buf, sizeof buf) < 0) {
                    tui_color(TUI_LGRAY, TUI_BLACK);
                    continue;                            /* Esc cancels */
                }
                tui_color(TUI_LGRAY, TUI_BLACK);
                if (buf[0]) width = atoi(buf);
                if (width < 20)  width = 20;
                if (width > 120) width = 120;
                ed_clear_mark();
                ed_reflow(width);
            } else if (code == 61) {              /* F3 */
                ed_clear_mark();
                ed_save_current();
            } else if (code == 65) {              /* F7: games menu */
                ed_clear_mark();
                while (fb_game_menu())            /* 1 = pick another game */
                    tui_clear();
            } else if (code == 64) {              /* F6: calendar */
                char path[300];
                ed_clear_mark();
                if (diary_pick(path, sizeof path)) {
                    if (!ed_load_file(path)) {
                        /* new diary day: start with an empty buffer so we
                         * never save the previous document into this file */
                        ed_nlines = 1;
                        ed_lines[0][0] = '\0';
                        ed_modified = 0;
                        ed_top_line = ed_left_col = 0;
                        ed_cur_line = ed_cur_col = 0;
                        ed_clear_mark();
                        ed_spell_marks_off();
                    }
                    strncpy(ed_filename, path, sizeof(ed_filename) - 1);
                    ed_filename[sizeof(ed_filename) - 1] = '\0';
                }
#ifdef FABLE_ESP32
            } else if (code == 67) {              /* F9: Wi-Fi setup */
                ed_clear_mark();
                fb_wifi_setup_page();
#endif
            } else if (code == 62) {              /* F4 */
                ed_spell_check();
            } else if (code == 63) {              /* F5: toggle spell marks */
                ed_clear_mark();
                if (ed_spell_marks_on) {
                    ed_spell_marks_on = 0;
                    ed_status_msg("Spell marks off.");
                } else {
                    if (!ed_spell_ok) {
                        if (!ts_init(ed_dict_path)) {
                            ed_status_msg("Dictionary not found.");
                            tui_getch();
                            continue;
                        }
                        ed_spell_ok = 1;
                    }
                    ed_spell_marks_on = 1;
                    ed_spell_mark_all();
                    ed_status_msg("Spell marks on.");
                }
            } else if (code == 83) {              /* Delete */
                ed_clear_mark();
                ed_del_forward();
            } else {
                ed_move_arrow(code);
            }
            continue;
        }
        switch (ch) {      
        case 19:                                  /* Ctrl+S: save */
            ed_clear_mark();
            ed_save_current();
            break;
        case 15:                                  /* Ctrl+O: open */
            {
                char path[260];
                path[0] = '\0';
                ed_prompt_bar("Open: ");
                if (fb_prompt_line("", path, sizeof path) >= 0 && path[0]) {
                    if (ed_load_file(path)) {
                        strncpy(ed_filename, path, sizeof(ed_filename) - 1);
                        ed_filename[sizeof(ed_filename) - 1] = '\0';
                    } else {
                        ed_status_msg("File not found");
                        tui_getch();
                    }
                }
                tui_color(TUI_LGRAY, TUI_BLACK);
            }
            break;
        case 2:                                   /* Ctrl+B */
            if (ed_mark_active) {
                ed_mark.el = ed_cur_line;
                ed_mark.ec = ed_cur_col;
                ed_mark_active = 0;
                ed_mark_exists = 1;
                ed_copy_mark_to_clip();
            } else {
                ed_mark.sl = ed_cur_line;
                ed_mark.sc = ed_cur_col;
                ed_mark.el = ed_cur_line;
                ed_mark.ec = ed_cur_col;
                ed_mark_active = 1;
                ed_mark_exists = 0;
                ed_clip_clear();
            }
            break;
        case 24:                                  /* Ctrl+X: cut */
            if (ed_mark_exists) {
                ed_copy_mark_to_clip();
                ed_delete_mark();
                ed_clear_mark();
            }
            break;
        case 16:                                  /* Ctrl+P: paste */
            if (ed_clip_len > 0) ed_paste_clip();
            ed_clear_mark();
            break;
        case 6:                                   /* Ctrl+F: find */
            {
                char needle[128];
                needle[0] = '\0';
                ed_prompt_bar("Find: ");
                fb_prompt_line("", needle, sizeof needle);
                tui_color(TUI_LGRAY, TUI_BLACK);
                if (needle[0] && !ed_find_next(needle)) {
                    ed_prompt_bar("Not found - press a key");
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
                if (ed_clip_len > 0 && ed_clip_len <= 15) {
                    long i;
                    int ok = 1;
                    for (i = 0; i < ed_clip_len; i++) {
                        unsigned char c = (unsigned char)ed_clip[i];
                        if (c == '\n' || c == '\r' || c < 32 || c > 126) {
                            ok = 0;
                            break;
                        }
                    }
                    if (ok) {
                        memcpy(find_buf, ed_clip, (size_t)ed_clip_len);
                        find_buf[ed_clip_len] = '\0';
                    }
                }

                ed_prompt_bar("Find: ");
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
                ed_prompt_bar("Replace with: ");
                fb_prompt_line("", replace_buf, sizeof replace_buf);
                tui_color(TUI_LGRAY, TUI_BLACK);

                spec.needle = find_buf;
                spec.replacement = replace_buf;
                spec.case_sensitive = 1;
                n = ed_replace_all(&spec);

                {
                    char msg[64];
                    snprintf(msg, sizeof msg, "%d replacement%s", n, n == 1 ? "" : "s");
                    ed_prompt_bar(msg);
                    tui_getch();
                    tui_color(TUI_LGRAY, TUI_BLACK);
                }
                ed_clear_mark();
            }
            break;

        case 14:                                  /* Ctrl+N */
            ed_new_document();
            break;
        case 27:                                  /* Esc: cancel, else file browser */
            if (ed_mark_active || ed_mark_exists) {
                ed_clear_mark();
                break;
            }
            {
                int r = ed_new_document();
#ifdef FABLE_WINDOWS
                /* Windows has no other way out: Esc in the file browser quits */
                if (r == ED_DOC_CANCELLED) {
                    if (ed_modified) {            /* only if "n" was answered above */
                        int k;
                        ed_prompt_bar("Discard unsaved changes and quit? (y/n) ");
                        k = tui_getch();
                        tui_color(TUI_LGRAY, TUI_BLACK);
                        if (k != 'y' && k != 'Y') break;
                    }
                    tui_cursor(1, 0);
                    tui_clear();
                    tui_restore();
                    ts_free();
                    tui_gotoxy(1, 1);
                    return 0;
                }
#else
                (void)r;
#endif
            }
            break;
        case 13:
            ed_clear_mark();
            ed_split_line();
            break;
        case 8:
            ed_clear_mark();
            ed_del_back();
            break;
        default:
            if (ch >= 32 && ch < 127) {
                ed_clear_mark();
                ed_insert_char(ch);
            }
            break;
        }
    }
}

/* editor.h - shared editor state and the functions that work on it.
 *
 * The state lives in editor.c. Names with an ed_ prefix are shared between
 * the editor source files; they are prefixed so they cannot clash with
 * symbols in the ESP32/Arduino libraries.
 */
#ifndef EDITOR_H
#define EDITOR_H

#include <ctype.h>
#include "fable.h"

#define IS_WORD(c) (isalnum((unsigned char)(c)) || (c) == '_')

typedef struct { int sl, sc, el, ec; } Mark;

typedef struct {
    const char *needle;
    const char *replacement;
    int         case_sensitive;
} FindSpec;

/* ---- state ---- */
extern char (*ed_lines)[MAXLINE];
extern int ed_nlines;
extern int ed_cur_line;
extern int ed_cur_col;
extern int ed_top_line;
extern int ed_left_col;
extern int ed_modified;
extern char ed_filename[260];
extern char ed_dict_path[260];
extern int ed_spell_marks_on;
extern int ed_spell_ok;
extern unsigned char (*ed_spell_bad)[MAXLINE / 8];
extern Mark ed_mark;
extern int ed_mark_active;
extern int ed_mark_exists;
extern char *ed_clip;
extern long ed_clip_len;

/* ---- functions ---- */
/* edit.c */
void ed_clamp_col(void);
void ed_insert_char(int c);
void ed_split_line(void);
void ed_del_back(void);
void ed_del_forward(void);
void ed_move_arrow(int code);
/* block.c */
void ed_clear_mark(void);
int ed_in_mark(int line, int col);
void ed_clip_clear(void);
void ed_copy_mark_to_clip(void);
void ed_delete_mark(void);
void ed_paste_clip(void);
/* find.c */
int ed_find_next(const char *needle);
int ed_replace_all(const FindSpec *spec);
/* reflow.c */
void ed_reflow(int width);
/* docio.c */
int ed_load_file(const char *fn);
int ed_save_current(void);
void ed_new_document(void);
/* screen.c */
void ed_status_msg(const char *msg);
void ed_prompt_bar(const char *text);
void ed_render(void);
/* spellui.c */
int ed_spell_is_bad(int line, int col);
void ed_spell_mark_all(void);
void ed_spell_marks_off(void);
void ed_spell_check(void);

#endif

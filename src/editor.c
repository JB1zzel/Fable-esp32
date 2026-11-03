/* editor.c - the one document being edited: line buffer, cursor, file name.
 *
 * Everything else (edit.c, block.c, find.c, ...) works on this state via editor.h. */

#include <stdio.h>
#include "editor.h"

char (*ed_lines)[MAXLINE] = NULL;

int ed_nlines;

int ed_cur_line;

int ed_cur_col;

int ed_top_line = 0;

int ed_left_col = 0;

int ed_modified;

char ed_filename[260];

char ed_dict_path[260] = "/dict.smd";

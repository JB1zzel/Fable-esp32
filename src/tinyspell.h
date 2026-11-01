#ifndef TINYSPELL_H
#define TINYSPELL_H

#define TS_MAX_WORD 128

/* Layer 1: non-UI */
int  ts_init(const char *dict_path);
int  ts_check(const char *word);                          /* UPPERCASE word */
int  ts_suggest(const char *word, char out[][TS_MAX_WORD], int max);
void ts_free(void);

/* Layer 2: interactive UI (runs the suggestion menu, mutates text) */
int  ts_fix_interactive(char **text, int *len);           /* returns fix count */

#endif
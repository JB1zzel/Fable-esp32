/* file_fabgl.h - fopen/fread/fwrite shim over SD for ESP32
 *
 * Redirects the standard C stdio calls used by fableGL.c and tinyspell.c
 * onto the ESP32 SD card driver. Include this header after <stdio.h> in
 * every .c file that touches the filesystem.
 */

#ifndef FILE_FABGL_H
#define FILE_FABGL_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Real shim entry points ---- */

FILE *fab_fopen(const char *path, const char *mode);
int   fab_fgetc(FILE *fp);
int   fab_fputc(int c, FILE *fp);
size_t fab_fread(void *buf, size_t sz, size_t n, FILE *fp);
size_t fab_fwrite(const void *buf, size_t sz, size_t n, FILE *fp);
char *fab_fgets(char *s, int n, FILE *fp);
int   fab_fputs(const char *s, FILE *fp);
int   fab_fclose(FILE *fp);
int   fab_remove(const char *path);
int   fab_ungetc(int c, FILE *fp);
int   fab_fseek(FILE *fp, long offset, int whence);
long  fab_ftell(FILE *fp);

/* Directory listing routed through the same SD object as fab_fopen. */
int   fab_list_dir(const char *path, char names[][32], int max);

/* Rename (replaces nothing: remove the destination first). 0 = ok. */
int   fab_rename(const char *from, const char *to);

#ifdef __cplusplus
}
#endif

/* ---- Redirect the standard names to the shim ----
 * NOTE: `remove` is intentionally NOT aliased here, because file_fabgl.cpp
 * itself calls SD.remove() and the macro would clobber it. */

#define fopen   fab_fopen
#define fgetc   fab_fgetc
#define fputc   fab_fputc
#define fread   fab_fread
#define fwrite  fab_fwrite
#define fgets   fab_fgets
#define fputs   fab_fputs
#define fclose  fab_fclose
#define ungetc  fab_ungetc
#define fseek   fab_fseek
#define ftell   fab_ftell

#endif
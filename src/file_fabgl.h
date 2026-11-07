/* file_fabgl.h - fopen/fread/fwrite shim over SD for ESP32
 *
 * Redirects the standard C stdio calls used by fableGL.c and tinyspell.c
 * onto the ESP32 SD card driver. Include this header after <stdio.h> in
 * every .c file that touches the filesystem.
 */

#ifndef FILE_FABGL_H
#define FILE_FABGL_H

#include <stdio.h>
#include <stdint.h>

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

/* 1 if the path exists (no error is logged when it does not). */
int   fab_exists(const char *path);

/* Creates a folder. 0 = created or already there. */
int   fab_mkdir(const char *path);

/* ---- structured directory listing for the Filer ---- */

typedef struct {
    char     name[128];     /* entries with longer names are not listed */
    uint32_t size;       /* file size in bytes (0 for dirs) */
    uint32_t mtime;      /* unix epoch seconds, 0 if unknown */
    int      is_dir;
} FabDirEntry;

/* Card size / used bytes. Returns 0 on failure. */
uint64_t fab_card_total(void);
uint64_t fab_card_used(void);
/* Fills out[] with up to max entries of the given directory.
 * Entries are sorted: directories first (alphabetical), then files
 * by mtime descending. ".." is NOT included - caller synthesizes it.
 * If more is not NULL it is set to 1 when the folder had further entries
 * that did not fit in max (the list is then incomplete), else 0.
 * Returns count (>= 0). */
int   fab_dir_entries(const char *path, FabDirEntry *out, int max, int *more);

#ifdef __cplusplus
}
#endif

/* ---- Redirect the standard names to the shim ----
 * NOTE: `remove` is intentionally NOT aliased here, because file_fabgl.cpp
 * itself calls SD.remove() and the macro would clobber it. */

#ifndef FABLE_WINDOWS
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
#endif
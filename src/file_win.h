/* file_win.h - Windows equivalent of file_fabgl.h.
 *
 * Paths use the same "/"-style as on the ESP32 SD card. "/" is the folder
 * Fable was started in; "/sub/x.txt" is sub\x.txt below it; "/../x.txt" is
 * x.txt in the parent folder. Anything else (e.g. "C:\docs\a.txt") is used
 * as given.
 *
 * fopen() is redirected to fab_fopen() so that every file the editor opens
 * (documents, the dictionary, diary files) follows the same rules as
 * fab_exists(), fab_remove(), fab_rename() and the directory listing.
 * Without this, Windows would treat "/dict.smd" as C:\dict.smd.
 */

#ifndef FILE_WIN_H
#define FILE_WIN_H

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     name[128];     /* entries with longer names are not listed */
    uint32_t size;
    uint32_t mtime;
    int      is_dir;
} FabDirEntry;

FILE *fab_fopen(const char *path, const char *mode);
int fab_exists(const char *path);
int fab_remove(const char *path);
int fab_rename(const char *from, const char *to);
int fab_mkdir(const char *path);        /* 0 = created or already there */

/* Fills out[] (folders first, then files newest first). If more is not
 * NULL it is set to 1 when the folder had more entries than max. */
int fab_dir_entries(const char *path, FabDirEntry *out, int max, int *more);
uint64_t fab_card_total(void);
uint64_t fab_card_used(void);

#ifdef __cplusplus
}
#endif

#define fopen fab_fopen

#endif

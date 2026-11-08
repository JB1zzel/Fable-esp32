/* file_posix.h - Linux/POSIX equivalent of file_win.h.
 *
 * The editor uses "/"-style paths as on the ESP32 SD card, so "/" is the
 * folder Fable was started in, not the root of the disk:
 *
 *   "/"            the start folder
 *   "/notes.txt"   notes.txt in the start folder
 *   "/../x.txt"    x.txt in the parent folder
 *   "//home/me/x"  an absolute path (the extra slash marks it)
 *   "~/x.txt"      x.txt in your home folder
 *   "x.txt", "../x.txt", "sub/x.txt"   as usual, from the start folder
 *
 * fopen() is redirected to fab_fopen() so that every file the editor opens
 * (documents, the dictionary, diary files) follows these rules, the same as
 * fab_exists(), fab_remove(), fab_rename() and the directory listing.
 */

#ifndef FILE_POSIX_H
#define FILE_POSIX_H

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

/* Fills out[] (folders first, then files newest first). Names starting with
 * "." are not listed. If more is not NULL it is set to 1 when the folder had
 * more entries than max. */
int fab_dir_entries(const char *path, FabDirEntry *out, int max, int *more);
uint64_t fab_card_total(void);
uint64_t fab_card_used(void);

#ifdef __cplusplus
}
#endif

#define fopen fab_fopen

#endif

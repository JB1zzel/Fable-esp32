/* file_posix.c - Linux/POSIX implementation of the fab_* file helpers.
 * See file_posix.h for how paths are interpreted. */

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE 1
#endif

#include "platform.h"

#ifdef FABLE_POSIX

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include "file_posix.h"

#undef fopen            /* file_posix.h redirects fopen to fab_fopen below */

#define PATH_BUF 1024

/* Editor path -> real path. "" means the start folder itself. */
static void make_path(const char *in, char *out, size_t n)
{
    if (in[0] == '/' && in[1] == '/') {             /* "//abs/path" */
        snprintf(out, n, "%s", in + 1);
    } else if (in[0] == '/') {                      /* "/x" is below the start folder */
        snprintf(out, n, "%s", in + 1);
    } else if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *home = getenv("HOME");
        snprintf(out, n, "%s%s", home ? home : "", in + 1);
    } else {
        snprintf(out, n, "%s", in);
    }
}

FILE *fab_fopen(const char *path, const char *mode)
{
    char p[PATH_BUF];
    make_path(path, p, sizeof p);
    if (p[0] == '\0') return NULL;                  /* the folder itself is not a file */
    return fopen(p, mode);
}

int fab_exists(const char *path)
{
    char p[PATH_BUF];
    struct stat st;
    make_path(path, p, sizeof p);
    if (p[0] == '\0') return 1;                     /* the start folder */
    return stat(p, &st) == 0 ? 1 : 0;
}

int fab_remove(const char *path)
{
    char p[PATH_BUF];
    make_path(path, p, sizeof p);
    if (p[0] == '\0') return -1;
    return unlink(p) == 0 ? 0 : -1;
}

int fab_rename(const char *from, const char *to)
{
    char a[PATH_BUF], b[PATH_BUF];
    make_path(from, a, sizeof a);
    make_path(to, b, sizeof b);
    if (a[0] == '\0' || b[0] == '\0') return -1;
    return rename(a, b) == 0 ? 0 : -1;
}

int fab_mkdir(const char *path)
{
    char p[PATH_BUF];
    make_path(path, p, sizeof p);
    if (p[0] == '\0') return 0;
    if (mkdir(p, 0755) == 0) return 0;
    return errno == EEXIST ? 0 : -1;
}

static int name_cmp(const char *a, const char *b)
{
    return strcasecmp(a, b);
}

static int cmp_entry(const void *pa, const void *pb)
{
    const FabDirEntry *a = (const FabDirEntry *)pa;
    const FabDirEntry *b = (const FabDirEntry *)pb;

    if (a->is_dir && !b->is_dir) return -1;
    if (!a->is_dir && b->is_dir) return  1;
    if (a->is_dir) return name_cmp(a->name, b->name);

    if (a->mtime > b->mtime) return -1;
    if (a->mtime < b->mtime) return  1;
    return name_cmp(a->name, b->name);
}

int fab_dir_entries(const char *path, FabDirEntry *out, int max, int *more)
{
    char dirp[PATH_BUF], full[PATH_BUF + 160];
    DIR *d;
    struct dirent *e;
    int n = 0;

    if (more) *more = 0;
    if (!out || max <= 0) return 0;

    make_path(path, dirp, sizeof dirp);
    d = opendir(dirp[0] ? dirp : ".");
    if (!d) return 0;

    while ((e = readdir(d)) != NULL) {
        struct stat st;
        const char *nm = e->d_name;

        if (nm[0] == '.') continue;                 /* ".", ".." and hidden files */
        /* names too long for out[].name are skipped: a cut-off name would
         * point at a file that does not exist */
        if (strlen(nm) >= sizeof(out[0].name)) continue;
        if (n >= max) {                             /* out[] is full */
            if (more) *more = 1;
            break;
        }

        snprintf(full, sizeof full, "%s%s%s", dirp,
                 (dirp[0] && dirp[strlen(dirp) - 1] != '/') ? "/" : "", nm);
        memset(&out[n], 0, sizeof out[n]);
        snprintf(out[n].name, sizeof out[n].name, "%s", nm);
        if (stat(full, &st) == 0) {
            out[n].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            out[n].size   = out[n].is_dir ? 0
                          : (st.st_size > 0xFFFFFFFFLL ? 0xFFFFFFFFu : (uint32_t)st.st_size);
            out[n].mtime  = st.st_mtime > 0 ? (uint32_t)st.st_mtime : 0;
        }
        n++;
    }
    closedir(d);

    if (n > 1) qsort(out, (size_t)n, sizeof(FabDirEntry), cmp_entry);
    return n;
}

uint64_t fab_card_total(void)
{
    struct statvfs v;
    if (statvfs(".", &v) != 0) return 0;
    return (uint64_t)v.f_blocks * (uint64_t)v.f_frsize;
}

uint64_t fab_card_used(void)
{
    struct statvfs v;
    if (statvfs(".", &v) != 0) return 0;
    return ((uint64_t)v.f_blocks - (uint64_t)v.f_bfree) * (uint64_t)v.f_frsize;
}

#else
typedef int file_posix_is_posix_only;   /* keeps this file from being empty */
#endif

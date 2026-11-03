/* file_fabgl.cpp - fopen/fread/fwrite shim over SD for ESP32 */

#include <Arduino.h>
#include <SD.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "file_fabgl.h"

typedef struct {
    File f;
    int  used;
} FabFile;

static FabFile files[8];

#define FAB_PATH_MAX 260

/* SD paths must start with '/'. Writes the absolute form into out. */
static void make_full(const char *path, char *out, size_t n)
{
    if (path[0] != '/') snprintf(out, n, "/%s", path);
    else                snprintf(out, n, "%s", path);
}

extern "C" {

FILE *fab_fopen(const char *path, const char *mode)
{
    int i;
    char fullpath[FAB_PATH_MAX];

    make_full(path, fullpath, sizeof fullpath);

    for (i = 0; i < 8; i++) {
        if (!files[i].used) {
            files[i].f = SD.open(fullpath, strchr(mode, 'w') ? FILE_WRITE : FILE_READ);
            if (!files[i].f) return NULL;
            files[i].used = 1;
            return (FILE*)&files[i];
        }
    }
    return NULL;
}

int fab_fgetc(FILE *fp)
{
    FabFile *f = (FabFile*)fp;
    if (!f || !f->f) return EOF;
    return f->f.read();
}

int fab_fputc(int c, FILE *fp)
{
    FabFile *f = (FabFile*)fp;
    if (!f || !f->f) return EOF;
    return f->f.write((uint8_t)c) ? c : EOF;
}

size_t fab_fread(void *buf, size_t sz, size_t n, FILE *fp)
{
    FabFile *f = (FabFile*)fp;
    if (!f || !f->f) return 0;
    return f->f.read((uint8_t*)buf, sz * n) / sz;
}

size_t fab_fwrite(const void *buf, size_t sz, size_t n, FILE *fp)
{
    FabFile *f = (FabFile*)fp;
    if (!f || !f->f) return 0;
    return f->f.write((const uint8_t*)buf, sz * n) / sz;
}

char *fab_fgets(char *s, int n, FILE *fp)
{
    FabFile *f = (FabFile*)fp;
    int i = 0;
    if (!f || !f->f) return NULL;
    while (i < n - 1 && f->f.available()) {
        int c = f->f.read();
        if (c < 0) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return i > 0 ? s : NULL;
}

int fab_fputs(const char *s, FILE *fp)
{
    FabFile *f = (FabFile*)fp;
    size_t n;
    if (!f || !f->f) return EOF;
    n = strlen(s);
    return f->f.write((const uint8_t*)s, n) == n ? 0 : EOF;
}

int fab_fclose(FILE *fp)
{
    FabFile *f = (FabFile*)fp;
    if (!f || !f->used) return EOF;
    f->f.close();
    f->used = 0;
    return 0;
}

int fab_remove(const char *path)
{
    char fullpath[FAB_PATH_MAX];
    make_full(path, fullpath, sizeof fullpath);
    return SD.remove(fullpath) ? 0 : -1;
}

int fab_exists(const char *path)
{
    char fullpath[FAB_PATH_MAX];
    make_full(path, fullpath, sizeof fullpath);
    return SD.exists(fullpath) ? 1 : 0;
}

int fab_rename(const char *from, const char *to)
{
    char a[FAB_PATH_MAX], b[FAB_PATH_MAX];
    make_full(from, a, sizeof a);
    make_full(to,   b, sizeof b);
    return SD.rename(a, b) ? 0 : -1;
}

uint64_t fab_card_total(void)
{
    return (uint64_t)SD.cardSize();
}

uint64_t fab_card_used(void)
{
    return (uint64_t)SD.usedBytes();
}

/* One-character push-back by stepping the file position back one byte.
 * Valid because callers only unget the character they just read. */
int fab_ungetc(int c, FILE *fp)
{
    FabFile *f = (FabFile*)fp;
    size_t pos;
    if (!f || !f->f || c == EOF) return EOF;
    pos = f->f.position();
    if (pos == 0) return EOF;
    return f->f.seek(pos - 1) ? c : EOF;
}

int fab_fseek(FILE *fp, long offset, int whence)
{
    FabFile *f = (FabFile*)fp;
    if (!f || !f->f) return -1;
    if (whence == SEEK_SET)      f->f.seek(offset);
    else if (whence == SEEK_END) f->f.seek(f->f.size() + offset);
    else if (whence == SEEK_CUR) f->f.seek(f->f.position() + offset);
    return 0;
}

long fab_ftell(FILE *fp)
{
    FabFile *f = (FabFile*)fp;
    if (!f || !f->f) return -1;
    return f->f.position();
}

int fab_list_dir(const char *path, char names[][32], int max)
{
    char fullpath[FAB_PATH_MAX];
    int n = 0;
    File dir;

    make_full(path, fullpath, sizeof fullpath);

    dir = SD.open(fullpath);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return 0;
    }

    File e = dir.openNextFile();
    while (e && n < max) {
        if (!e.isDirectory()) {
            const char *nm = e.name();
            const char *slash = strrchr(nm, '/');
            if (slash) nm = slash + 1;
            strncpy(names[n], nm, 31);
            names[n][31] = '\0';
            n++;
        }
        e.close();
        e = dir.openNextFile();
    }
    dir.close();
    return n;
}

/* Case-insensitive strcmp helper for sorting names. */
static int name_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = (unsigned char)*a++;
        int cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* qsort comparator: directories first (alphabetical), then files by
 * mtime descending, ties broken alphabetically. */
static int cmp_dir_entry(const void *pa, const void *pb)
{
    const FabDirEntry *a = (const FabDirEntry *)pa;
    const FabDirEntry *b = (const FabDirEntry *)pb;

    if (a->is_dir && !b->is_dir) return -1;
    if (!a->is_dir && b->is_dir) return  1;
    if (a->is_dir)               return name_cmp(a->name, b->name);

    if (a->mtime > b->mtime) return -1;
    if (a->mtime < b->mtime) return  1;
    return name_cmp(a->name, b->name);
}

int fab_dir_entries(const char *path, FabDirEntry *out, int max, int *more)
{
    char fullpath[FAB_PATH_MAX];
    int n = 0;
    File dir;

    if (more) *more = 0;
    if (!out || max <= 0) return 0;
    make_full(path, fullpath, sizeof fullpath);

    /* SD.open() fails on directory paths with a trailing slash — strip it */
    {
        size_t len = strlen(fullpath);
        if (len > 1 && fullpath[len - 1] == '/') fullpath[len - 1] = '\0';
    }

    dir = SD.open(fullpath);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return 0;
    }

    File e = dir.openNextFile();
    while (e && n < max) {
        const char *nm = e.name();
        const char *slash = strrchr(nm, '/');
        if (slash) nm = slash + 1;

        /* names that do not fit in out[].name are skipped: a cut-off name
         * would point at a file that does not exist */
        if (strcmp(nm, ".") != 0 && strcmp(nm, "..") != 0 &&
            strlen(nm) < sizeof(out[n].name)) {
            strncpy(out[n].name, nm, sizeof(out[n].name) - 1);
            out[n].name[sizeof(out[n].name) - 1] = '\0';
            out[n].is_dir = e.isDirectory() ? 1 : 0;
            out[n].size   = out[n].is_dir ? 0 : (uint32_t)e.size();
            out[n].mtime  = 0;
            if (!out[n].is_dir) {
                time_t t = e.getLastWrite();
                out[n].mtime = (uint32_t)t;
            }
            n++;
        }
        e.close();
        e = dir.openNextFile();
    }
    if (e) {                       /* stopped because out[] is full */
        if (more) *more = 1;
        e.close();
    }
    dir.close();

    if (n > 1) qsort(out, (size_t)n, sizeof(FabDirEntry), cmp_dir_entry);
    return n;
}

}  /* extern "C" */
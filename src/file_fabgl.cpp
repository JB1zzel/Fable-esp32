/* file_fabgl.cpp - fopen/fread/fwrite shim over SD for ESP32 */

#include <Arduino.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    File f;
    int  used;
} FabFile;

static FabFile files[8];

extern "C" {

FILE *fab_fopen(const char *path, const char *mode)
{
    int i;
    char fullpath[64];

    if (path[0] != '/')
        snprintf(fullpath, sizeof fullpath, "/%s", path);
    else
        snprintf(fullpath, sizeof fullpath, "%s", path);

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
    if (!f || !f->f) return EOF;
    return f->f.print(s) ? 0 : EOF;
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
    return SD.remove(path) ? 0 : -1;
}

int fab_ungetc(int c, FILE *fp) { (void)c; (void)fp; return EOF; }

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
    char fullpath[64];
    int n = 0;
    File dir;

    if (path[0] != '/')
        snprintf(fullpath, sizeof fullpath, "/%s", path);
    else
        snprintf(fullpath, sizeof fullpath, "%s", path);

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

}  /* extern "C" */
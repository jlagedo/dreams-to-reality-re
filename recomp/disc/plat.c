/*
 * Disc library - the few places that differ by system: opening files with
 * 64-bit offsets, listing directories, and (on Windows) UTF-8 paths.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif

#include "internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* UTF-8 to UTF-16; a string that is not valid UTF-8 is read as ANSI. */
static wchar_t* widen(const char* s) {
    UINT cp = CP_UTF8;
    int n = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (!n) {
        cp = CP_ACP;
        n = MultiByteToWideChar(cp, 0, s, -1, NULL, 0);
    }
    if (!n) return NULL;
    wchar_t* w = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
    if (w) MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, s, -1, w, n);
    return w;
}

static char* narrow(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char* s = n ? (char*)malloc((size_t)n) : NULL;
    if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

FILE* plat_fopen(const char* path) {
    wchar_t* w = widen(path);
    FILE* f = w ? _wfopen(w, L"rb") : NULL;
    free(w);
    return f;
}

int plat_seek(FILE* f, uint64_t offset) {
    return offset > (uint64_t)INT64_MAX ? -1 : _fseeki64(f, (__int64)offset, SEEK_SET);
}

static int attributes(const char* path, WIN32_FILE_ATTRIBUTE_DATA* d) {
    wchar_t* w = widen(path);
    int ok = w && GetFileAttributesExW(w, GetFileExInfoStandard, d);
    free(w);
    return ok;
}

int plat_file_size(const char* path, uint64_t* size) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!attributes(path, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return -1;
    *size = (uint64_t)d.nFileSizeHigh << 32 | d.nFileSizeLow;
    return 0;
}

int plat_is_dir(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    return attributes(path, &d) && (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

int plat_list_dir(const char* dir, PlatListFn cb, void* ctx) {
    char* pattern = path_join(dir, "*");
    wchar_t* w = pattern ? widen(pattern) : NULL;
    free(pattern);
    if (!w) return -1;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(w, &fd);
    free(w);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    int rc = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        char* name = narrow(fd.cFileName);
        if (!name) { rc = -1; break; }
        rc = cb(ctx, name, (uint64_t)fd.nFileSizeHigh << 32 | fd.nFileSizeLow,
                (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        free(name);
    } while (!rc && FindNextFileW(h, &fd));
    FindClose(h);
    return rc;
}

#else   /* POSIX */

#include <dirent.h>
#include <sys/stat.h>

FILE* plat_fopen(const char* path) { return fopen(path, "rb"); }

int plat_seek(FILE* f, uint64_t offset) {
    return offset > (uint64_t)INT64_MAX ? -1 : fseeko(f, (off_t)offset, SEEK_SET);
}

int plat_file_size(const char* path, uint64_t* size) {
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode)) return -1;
    *size = (uint64_t)st.st_size;
    return 0;
}

int plat_is_dir(const char* path) {
    struct stat st;
    return !stat(path, &st) && S_ISDIR(st.st_mode);
}

int plat_list_dir(const char* dir, PlatListFn cb, void* ctx) {
    DIR* d = opendir(dir);
    if (!d) return -1;
    int rc = 0;
    for (struct dirent* e; !rc && (e = readdir(d)) != NULL;) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char* full = path_join(dir, e->d_name);
        struct stat st;
        if (!full || stat(full, &st)) { free(full); continue; }   /* a dangling link: not an entry */
        free(full);
        rc = cb(ctx, e->d_name, S_ISDIR(st.st_mode) ? 0 : (uint64_t)st.st_size, S_ISDIR(st.st_mode));
    }
    closedir(d);
    return rc;
}

#endif

/* ---- util ---- */

char* str_dup(const char* s) {
    size_t n = strlen(s) + 1;
    char* d = (char*)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

static int is_sep(char c) { return c == '/' || c == '\\'; }

char* path_join(const char* dir, const char* name) {
    size_t nd = strlen(dir), nn = strlen(name);
    char* p = (char*)malloc(nd + nn + 2);
    if (!p) return NULL;
    memcpy(p, dir, nd);
    if (nd && !is_sep(dir[nd - 1])) p[nd++] = '/';
    for (size_t i = 0; i <= nn; i++) p[nd + i] = name[i] == '\\' ? '/' : name[i];
    return p;
}

char* path_dirname(const char* path) {
    size_t n = strlen(path);
    while (n > 1 && is_sep(path[n - 1])) n--;     /* a trailing separator is not a name */
    while (n && !is_sep(path[n - 1])) n--;
    while (n > 1 && is_sep(path[n - 1])) n--;
    if (!n) return str_dup(".");
    char* d = (char*)malloc(n + 1);
    if (d) { memcpy(d, path, n); d[n] = 0; }
    return d;
}

static int up(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

int ci_compare(const char* a, const char* b) {
    for (;; a++, b++) {
        int d = up((unsigned char)*a) - up((unsigned char)*b);
        if (d || !*a) return d;
    }
}

int err_set(char* err, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, MSG_LEN, fmt, ap);
    va_end(ap);
    return -1;
}

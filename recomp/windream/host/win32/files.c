/*
 * WINDREAM recompilation - file system bridges.
 *
 * The guest believes it runs as C:\WINDREAM.EXE. A guest path loses its drive
 * letter and is resolved relative to the guest's current directory, then
 * looked up first in the write sandbox and then in each read root (the
 * directory holding WINDREAM.EXE, plus WD_READ_ROOTS). Anything opened for
 * writing goes to the sandbox (WD_WRITE_ROOT, default ./sandbox): the extracted
 * disc directories are reference data and are never modified.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#define RECOMP_GENERATED_CODE
#include "imports.h"

#define MAX_ROOTS 4
static char g_roots[MAX_ROOTS][MAX_PATH];
static int g_nroots;
static char g_sandbox[MAX_PATH];
static char g_cwd[MAX_PATH] = "";   /* guest cwd below C:\, no leading/trailing '\' */
static int g_log = 200;             /* log the first N opens */

void files_init(const char* exe) {
    char dir[MAX_PATH];
    GetFullPathNameA(exe, MAX_PATH, dir, NULL);
    char* s = strrchr(dir, '\\');
    if (s) *s = 0;
    strcpy(g_roots[g_nroots++], dir);
    const char* extra = getenv("WD_READ_ROOTS");
    if (extra) {
        char tmp[4 * MAX_PATH]; strncpy(tmp, extra, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
        for (char* t = strtok(tmp, ";"); t && g_nroots < MAX_ROOTS; t = strtok(NULL, ";"))
            strcpy(g_roots[g_nroots++], t);
    }
    const char* w = getenv("WD_WRITE_ROOT");
    GetFullPathNameA(w ? w : "sandbox", MAX_PATH, g_sandbox, NULL);
    CreateDirectoryA(g_sandbox, NULL);
    for (int i = 0; i < g_nroots; i++) fprintf(stderr, "[files] read root %d: %s\n", i, g_roots[i]);
    fprintf(stderr, "[files] write sandbox: %s\n", g_sandbox);
}

/* Guest path -> path relative to the guest's C:\ ("DATA\FOO.DAT"). */
static void guest_rel(uint32_t va, char* rel) {
    char in[MAX_PATH], tmp[2 * MAX_PATH];
    guest_str(va, in, sizeof in);
    for (char* p = in; *p; p++) if (*p == '/') *p = '\\';
    const char* p = in;
    if (p[0] && p[1] == ':') p += 2;
    if (*p == '\\') strcpy(tmp, p + 1);
    else if (g_cwd[0]) sprintf(tmp, "%s\\%s", g_cwd, p);
    else strcpy(tmp, p);
    /* normalise . and .. */
    char* seg[64]; char* ctx = NULL; int n = 0;
    for (char* t = strtok_s(tmp, "\\", &ctx); t; t = strtok_s(NULL, "\\", &ctx)) {
        if (!strcmp(t, ".")) continue;
        if (!strcmp(t, "..")) { if (n) n--; continue; }
        if (n < 64) seg[n++] = t;
    }
    rel[0] = 0;
    for (int i = 0; i < n; i++) { if (i) strcat(rel, "\\"); strcat(rel, seg[i]); }
}

static int exists(const char* path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

static void make_parents(char* path) {
    for (char* p = path + 3; *p; p++)
        if (*p == '\\') { *p = 0; CreateDirectoryA(path, NULL); *p = '\\'; }
}

/* Resolve for reading: sandbox copy wins, then the read roots in order. */
static void resolve_read(const char* rel, char* out) {
    sprintf(out, "%s\\%s", g_sandbox, rel);
    if (exists(out)) return;
    for (int i = 0; i < g_nroots; i++) {
        sprintf(out, "%s\\%s", g_roots[i], rel);
        if (exists(out)) return;
    }
    sprintf(out, "%s\\%s", g_roots[0], rel);
}
/* Resolve for writing: always the sandbox; seed it from a read root when the
 * file already exists there and is being opened, not replaced. */
static void resolve_write(const char* rel, char* out, int keep) {
    sprintf(out, "%s\\%s", g_sandbox, rel);
    make_parents(out);
    if (keep && !exists(out)) {
        char src[MAX_PATH];
        resolve_read(rel, src);
        if (exists(src)) CopyFileA(src, out, TRUE);
    }
}

void imp_CreateFileA(void) {  /* (name, access, share, sa, disposition, flags, template) */
    char rel[MAX_PATH], host[MAX_PATH];
    guest_rel(ARG(0), rel);
    uint32_t access = ARG(1), disp = ARG(4);
    int write = (access & GENERIC_WRITE) || disp == CREATE_ALWAYS || disp == CREATE_NEW || disp == TRUNCATE_EXISTING;
    if (write) resolve_write(rel, host, disp == OPEN_EXISTING || disp == OPEN_ALWAYS);
    else resolve_read(rel, host);
    HANDLE h = CreateFileA(host, access, ARG(2), NULL, disp, ARG(5) & 0xFFFF, NULL);
    uint32_t gh = h == INVALID_HANDLE_VALUE ? 0xFFFFFFFFu : handle_new(h, HK_FILE);
    const char* leaf = strrchr(rel, '\\');
    leaf = leaf ? leaf + 1 : rel;
    int save = 0;
    if (strlen(leaf) > 4 && !_strnicmp(leaf, "game", 4) && leaf[4] >= '0' && leaf[4] <= '9') {
        const char* suffix = leaf + 4;
        while (*suffix >= '0' && *suffix <= '9') ++suffix;
        save = !_stricmp(suffix, ".dat");
    }
    // Save/reload acceptance must remain observable after asset loads exhaust
    // the general 200-open log budget. Emit exactly one successful save event.
    if (save && gh != 0xFFFFFFFFu)
        fprintf(stderr, "[save] open %s \"%s\" -> %s\n", write ? "W" : "R", rel, host);
    else if (g_log > 0) { g_log--; fprintf(stderr, "[files] open %s \"%s\" -> %s%s\n", write ? "W" : "R", rel, host, gh == 0xFFFFFFFFu ? " (FAILED)" : ""); }
    RET(gh); STDRET(7);
}
void imp_ReadFile(void) {  /* (h, buf, n, *read, overlapped) */
    DWORD got = 0;
    HANDLE h = (HANDLE)handle_get(ARG(0), 0);
    /* Stage the host output: EOF/short reads only touch the bytes returned. */
    uint32_t count = ARG(2);
    void* data = h ? malloc(count ? count : 1) : NULL;
    BOOL ok = FALSE;
    if (h && !data) SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    if (data) {
        ok = ReadFile(h, data, count, &got, NULL);
        DWORD error = GetLastError();
        if (got) memcpy(wd_host_range(ARG(1), got, 1), data, got);
        free(data);
        SetLastError(error);
    }
    if (ARG(3)) WD_HOST_WRITE32(ARG(3)) = got;
    RET(ok); STDRET(5);
}
void imp_WriteFile(void) {
    DWORD put = 0;
    HANDLE h = (HANDLE)handle_get(ARG(0), 0);
    BOOL ok = h && WriteFile(h, wd_host_range(ARG(1), ARG(2), 0), ARG(2), &put, NULL);
    if (ARG(3)) WD_HOST_WRITE32(ARG(3)) = put;
    RET(ok); STDRET(5);
}
void imp_SetFilePointer(void) {  /* (h, dist, *distHigh, method) */
    HANDLE h = (HANDLE)handle_get(ARG(0), 0);
    LONG hi = ARG(2) ? (LONG)WD_HOST_READ32(ARG(2)) : 0;
    DWORD r = h ? SetFilePointer(h, (LONG)ARG(1), ARG(2) ? &hi : NULL, ARG(3)) : INVALID_SET_FILE_POINTER;
    if (ARG(2)) WD_HOST_WRITE32(ARG(2)) = (uint32_t)hi;
    RET(r); STDRET(4);
}
void imp_FlushFileBuffers(void) { HANDLE h = (HANDLE)handle_get(ARG(0), HK_FILE); RET(h ? FlushFileBuffers(h) : 1); STDRET(1); }
void imp_GetFileType(void) { HANDLE h = (HANDLE)handle_get(ARG(0), 0); RET(h ? GetFileType(h) : 0); STDRET(1); }

void imp_GetFileAttributesA(void) {
    char rel[MAX_PATH], host[MAX_PATH];
    guest_rel(ARG(0), rel);
    if (!rel[0]) { RET(FILE_ATTRIBUTE_DIRECTORY); STDRET(1); return; }
    resolve_read(rel, host);
    RET(GetFileAttributesA(host)); STDRET(1);
}
void imp_DeleteFileA(void) {
    char rel[MAX_PATH], host[MAX_PATH];
    guest_rel(ARG(0), rel);
    sprintf(host, "%s\\%s", g_sandbox, rel);
    RET(DeleteFileA(host)); STDRET(1);
}
void imp_MoveFileA(void) {
    char r0[MAX_PATH], r1[MAX_PATH], h0[MAX_PATH], h1[MAX_PATH];
    guest_rel(ARG(0), r0); guest_rel(ARG(1), r1);
    resolve_write(r0, h0, 1); resolve_write(r1, h1, 0);
    RET(MoveFileA(h0, h1)); STDRET(2);
}

/* WIN32_FIND_DATAA holds no pointers: 320 bytes, same layout on every host. */
void imp_FindFirstFileA(void) {
    char rel[MAX_PATH], host[MAX_PATH], dir[MAX_PATH];
    guest_rel(ARG(0), rel);
    strcpy(dir, rel);
    char* s = strrchr(dir, '\\');
    if (s) *s = 0; else dir[0] = 0;
    sprintf(host, "%s\\%s", g_sandbox, rel);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(host, &fd);
    for (int i = 0; h == INVALID_HANDLE_VALUE && i < g_nroots; i++) {
        sprintf(host, "%s\\%s", g_roots[i], rel);
        h = FindFirstFileA(host, &fd);
    }
    if (g_log > 0) { g_log--; fprintf(stderr, "[files] find \"%s\"%s\n", rel, h == INVALID_HANDLE_VALUE ? " (none)" : ""); }
    if (h == INVALID_HANDLE_VALUE) { RET(0xFFFFFFFFu); STDRET(2); return; }
    memcpy(wd_host_range(ARG(1), sizeof fd, 1), &fd, sizeof fd);
    RET(handle_new(h, HK_FIND)); STDRET(2);
}
void imp_FindNextFileA(void) {
    WIN32_FIND_DATAA fd;
    HANDLE h = (HANDLE)handle_get(ARG(0), HK_FIND);
    BOOL ok = h && FindNextFileA(h, &fd);
    if (ok) memcpy(wd_host_range(ARG(1), sizeof fd, 1), &fd, sizeof fd);
    RET(ok); STDRET(2);
}
void imp_FindClose(void) { handle_close(ARG(0)); RET(1); STDRET(1); }

void imp_GetCurrentDirectoryA(void) {  /* (len, buf) */
    char s[MAX_PATH + 3];
    sprintf(s, "C:\\%s", g_cwd);
    guest_strcpy_out(ARG(1), ARG(0), s);
    RET(strlen(s)); STDRET(2);
}
void imp_SetCurrentDirectoryA(void) {
    char rel[MAX_PATH];
    guest_rel(ARG(0), rel);
    strcpy(g_cwd, rel);
    fprintf(stderr, "[files] chdir C:\\%s\n", g_cwd);
    RET(1); STDRET(1);
}
void imp_GetFullPathNameA(void) {  /* (name, len, buf, *filePart) */
    char rel[MAX_PATH], s[MAX_PATH + 3];
    guest_rel(ARG(0), rel);
    sprintf(s, "C:\\%s", rel);
    uint32_t n = (uint32_t)strlen(s);
    if (n + 1 > ARG(1)) { RET(n + 1); STDRET(4); return; }
    guest_strcpy_out(ARG(2), ARG(1), s);
    if (ARG(3)) { char* f = strrchr(s, '\\'); WD_HOST_WRITE32(ARG(3)) = ARG(2) + (uint32_t)(f ? f + 1 - s : 0); }
    RET(n); STDRET(4);
}

void imp_DosDateTimeToFileTime(void) {
    FILETIME value;
    BOOL ok = DosDateTimeToFileTime((WORD)ARG(0), (WORD)ARG(1), &value);
    if (ok) memcpy(wd_host_range(ARG(2), sizeof value, 1), &value, sizeof value);
    RET(ok); STDRET(3);
}
void imp_FileTimeToDosDateTime(void) {
    WORD date, time;
    BOOL ok = FileTimeToDosDateTime((const FILETIME*)wd_host_range(ARG(0), sizeof(FILETIME), 0), &date, &time);
    if (ok) { WD_HOST_WRITE16(ARG(1)) = date; WD_HOST_WRITE16(ARG(2)) = time; }
    RET(ok); STDRET(3);
}
void imp_FileTimeToLocalFileTime(void) {
    FILETIME value;
    BOOL ok = FileTimeToLocalFileTime((const FILETIME*)wd_host_range(ARG(0), sizeof(FILETIME), 0), &value);
    if (ok) memcpy(wd_host_range(ARG(1), sizeof value, 1), &value, sizeof value);
    RET(ok); STDRET(2);
}
void imp_LocalFileTimeToFileTime(void) {
    FILETIME value;
    BOOL ok = LocalFileTimeToFileTime((const FILETIME*)wd_host_range(ARG(0), sizeof(FILETIME), 0), &value);
    if (ok) memcpy(wd_host_range(ARG(1), sizeof value, 1), &value, sizeof value);
    RET(ok); STDRET(2);
}

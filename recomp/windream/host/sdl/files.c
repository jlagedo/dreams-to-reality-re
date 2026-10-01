/*
 * WINDREAM recompilation - file system bridges (SDL3).
 *
 * The guest believes it runs as C:\WINDREAM.EXE. A guest path loses its drive
 * letter and is resolved relative to the guest's current directory, then
 * looked up first in the write sandbox and then in each read root (the
 * directory holding WINDREAM.EXE, plus WD_READ_ROOTS). Anything opened for
 * writing goes to the sandbox (WD_WRITE_ROOT, default ./sandbox): the extracted
 * disc directories are reference data and are never modified.
 *
 * Guest names are case-insensitive, as on the disc and in the game's own
 * strings ("DATA\FONT\HI640.SPR", "data\objet\particle.spr"). Where the host
 * file system is case-sensitive, each path segment that is not found as
 * written is looked up in its directory without regard to case
 * (WD_FILES_CASE_WALK=1 forces that lookup everywhere, for testing on a
 * case-insensitive host). Names pass through byte for byte: the game's are
 * ASCII, and nothing here converts code page 1252 to the host's encoding.
 * Host paths are UTF-8, as SDL takes them; on Windows the exe path arrives in
 * the process's ANSI code page, so a read root with non-ASCII characters
 * needs that code page to be UTF-8.
 *
 * What differs from Win32, because SDL does not expose it: share modes are
 * SDL's, not the guest's (on Windows SDL opens a file for writing exclusively
 * and for reading with read sharing; an open refused for that reason is
 * logged and fails with ERROR_SHARING_VIOLATION), every file reports
 * FILE_ATTRIBUTE_ARCHIVE and no 8.3 alternate name, and a wildcard never
 * matches through a short name.
 *
 * Known gaps, as in the Win32 bridges these replaced: the sandbox and the read
 * roots are not merged. FindFirstFileA answers from the first of them that has
 * a match, and a directory that exists in the sandbox always has one for "*.*"
 * (its "." entry), hiding the read roots' files; CREATE_NEW, CREATE_ALWAYS,
 * DeleteFileA and MoveFileA look for an existing file in the sandbox only;
 * SetCurrentDirectoryA accepts a directory that does not exist.
 */
#define RECOMP_GENERATED_CODE
#include "host.h"

#define MAX_ROOTS 4
#define HOST_PATH 1024               /* host paths; guest paths are at most W32_MAX_PATH */
static char g_roots[MAX_ROOTS][HOST_PATH];
static int g_folds[MAX_ROOTS + 1];   /* the root's file system ignores case (sandbox: last) */
static int g_nroots;
static char g_sandbox[HOST_PATH];
static char g_cwd[W32_MAX_PATH] = "";   /* guest cwd below C:\, no leading/trailing '\' */
static int g_log = 200;                 /* log the first N opens */
static int g_walk;                      /* WD_FILES_CASE_WALK */

/* An SDL stream is not safe to use from two threads at once, a Win32 handle
 * is: each file's operations take its lock. */
typedef struct File {
    WdObject object;
    SDL_Mutex* lock;
    SDL_IOStream* io;
    uint32_t access;
    struct File* next;               /* g_open */
    char path[HOST_PATH];
} File;
typedef struct { char name[W32_MAX_PATH]; SDL_PathInfo info; } FindEntry;
typedef struct { WdObject object; SDL_SpinLock lock; FindEntry* entries; int count, next; } Find;
static File* g_open;                 /* every open file, to explain a refused second open */
static SDL_SpinLock g_open_lock;

void files_release(int kind, void* host) {
    if (!SDL_AtomicDecRef(&((WdObject*)host)->refs)) return;
    if (kind == HK_FILE) {
        File* f = (File*)host;
        SDL_LockSpinlock(&g_open_lock);
        for (File** link = &g_open; *link; link = &(*link)->next)
            if (*link == f) { *link = f->next; break; }
        SDL_UnlockSpinlock(&g_open_lock);
        SDL_CloseIO(f->io);
        SDL_DestroyMutex(f->lock);
        SDL_free(f);
    } else {
        Find* f = (Find*)host;
        SDL_free(f->entries);
        SDL_free(f);
    }
}
static int file_is_open(const char* path) {
    int open = 0;
    SDL_LockSpinlock(&g_open_lock);
    for (File* f = g_open; f && !open; f = f->next) open = !SDL_strcasecmp(f->path, path);
    SDL_UnlockSpinlock(&g_open_lock);
    return open;
}

/* ---- host paths ---- */
static void slashes(char* path) { for (; *path; path++) if (*path == '\\') *path = '/'; }

static void absolute(const char* path, char* out) {
    int rooted = path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':');
    char* cwd = rooted ? NULL : SDL_GetCurrentDirectory();
    SDL_snprintf(out, HOST_PATH, "%s%s", cwd ? cwd : "", path);
    SDL_free(cwd);
    slashes(out);
    size_t n = strlen(out);
    while (n > 1 && out[n - 1] == '/' && out[n - 2] != ':') out[--n] = 0;
}

static SDL_PathType path_type(const char* path, SDL_PathInfo* info) {
    SDL_PathInfo local;
    if (!info) info = &local;
    return SDL_GetPathInfo(path, info) ? info->type : SDL_PATHTYPE_NONE;
}

/* Does this directory's file system ignore case? Probe it under its own name
 * with the case of every letter flipped. */
static int folds_case(const char* directory) {
    char flipped[HOST_PATH];
    int letters = 0;
    SDL_strlcpy(flipped, directory, sizeof flipped);
    for (char* p = flipped; *p; p++) {
        if (*p >= 'a' && *p <= 'z') { *p -= 32; letters++; }
        else if (*p >= 'A' && *p <= 'Z') { *p += 32; letters++; }
    }
    return letters && path_type(flipped, NULL) == SDL_PATHTYPE_DIRECTORY;
}

void files_init(const char* exe) {
    char dir[HOST_PATH];
    absolute(exe, dir);
    char* s = strrchr(dir, '/');
    if (s) *s = 0;
    SDL_strlcpy(g_roots[g_nroots++], dir, HOST_PATH);
    const char* extra = SDL_getenv("WD_READ_ROOTS");
    if (extra) {
        char tmp[4 * HOST_PATH]; char* ctx = NULL;
        SDL_strlcpy(tmp, extra, sizeof tmp);
        for (char* t = SDL_strtok_r(tmp, ";", &ctx); t && g_nroots < MAX_ROOTS; t = SDL_strtok_r(NULL, ";", &ctx))
            absolute(t, g_roots[g_nroots++]);
    }
    const char* w = SDL_getenv("WD_WRITE_ROOT");
    absolute(w && *w ? w : "sandbox", g_sandbox);
    SDL_CreateDirectory(g_sandbox);
    const char* walk = SDL_getenv("WD_FILES_CASE_WALK");
    g_walk = walk && *walk && *walk != '0';
    for (int i = 0; i < g_nroots; i++) {
        g_folds[i] = folds_case(g_roots[i]);
        fprintf(stderr, "[files] read root %d: %s\n", i, g_roots[i]);
    }
    g_folds[MAX_ROOTS] = folds_case(g_sandbox);
    fprintf(stderr, "[files] write sandbox: %s\n", g_sandbox);
}
static int root_folds(const char* root) {
    if (g_walk) return 0;
    if (root == g_sandbox) return g_folds[MAX_ROOTS];
    for (int i = 0; i < g_nroots; i++) if (root == g_roots[i]) return g_folds[i];
    return 0;
}

/* Guest path -> path relative to the guest's C:\ ("DATA\FOO.DAT"), in a
 * buffer of W32_MAX_PATH. 0 when the current directory and the path together
 * are longer than that. */
static int guest_rel(uint32_t va, char* rel) {
    char in[W32_MAX_PATH], tmp[2 * W32_MAX_PATH];
    guest_str(va, in, sizeof in);
    for (char* p = in; *p; p++) if (*p == '/') *p = '\\';
    const char* p = in;
    if (p[0] && p[1] == ':') p += 2;
    if (*p == '\\') SDL_strlcpy(tmp, p + 1, sizeof tmp);
    else if (g_cwd[0]) SDL_snprintf(tmp, sizeof tmp, "%s\\%s", g_cwd, p);
    else SDL_strlcpy(tmp, p, sizeof tmp);
    /* normalise . and .. */
    char* seg[64]; char* ctx = NULL; int n = 0;
    for (char* t = SDL_strtok_r(tmp, "\\", &ctx); t; t = SDL_strtok_r(NULL, "\\", &ctx)) {
        if (!strcmp(t, ".")) continue;
        if (!strcmp(t, "..")) { if (n) n--; continue; }
        if (n < 64) seg[n++] = t;
    }
    size_t used = 0;
    rel[0] = 0;
    for (int i = 0; i < n; i++) {
        size_t len = strlen(seg[i]);
        if (used + (i ? 1 : 0) + len >= W32_MAX_PATH) { rel[0] = 0; return 0; }
        if (i) rel[used++] = '\\';
        memcpy(rel + used, seg[i], len + 1);
        used += len;
    }
    return 1;
}
/* The bridge's path argument, or fail the call as Win32 does for a path that
 * is too long. */
#define GUEST_REL(va, rel, failure, slots) \
    if (!guest_rel((va), (rel))) { g_last_error = W32_ERROR_FILENAME_EXCED_RANGE; RET(failure); STDRET(slots); return; }

typedef struct { const char* want; char* found; } SegmentSearch;
static SDL_EnumerationResult SDLCALL segment_seen(void* userdata, const char* dirname, const char* fname) {
    SegmentSearch* search = (SegmentSearch*)userdata;
    (void)dirname;
    if (SDL_strcasecmp(fname, search->want)) return SDL_ENUM_CONTINUE;
    SDL_strlcpy(search->found, fname, W32_MAX_PATH);
    return SDL_ENUM_SUCCESS;
}

/* root + rel as a host path. Returns what is there (SDL_PATHTYPE_NONE: nothing,
 * and *error says whether the name or its directory is missing); out is then
 * the path a new file would get, existing directories in their own case. */
static SDL_PathType locate(const char* root, const char* rel, char* out, SDL_PathInfo* info, uint32_t* error) {
    char copy[W32_MAX_PATH]; char* ctx = NULL;
    int fold = root_folds(root);
    SDL_PathType type = SDL_PATHTYPE_DIRECTORY;
    uint32_t err = 0;
    SDL_PathInfo local;
    if (!info) info = &local;
    SDL_strlcpy(out, root, HOST_PATH);
    SDL_strlcpy(copy, rel, sizeof copy);
    if (!rel[0]) type = path_type(out, info);
    for (char* seg = SDL_strtok_r(copy, "\\", &ctx); seg; seg = SDL_strtok_r(NULL, "\\", &ctx)) {
        size_t at = strlen(out);
        char actual[W32_MAX_PATH];
        const char* name = seg;
        if (type == SDL_PATHTYPE_DIRECTORY) {
            SDL_snprintf(out + at, HOST_PATH - at, "/%s", seg);
            type = g_walk ? SDL_PATHTYPE_NONE : path_type(out, info);
            if (type == SDL_PATHTYPE_NONE && !fold) {
                SegmentSearch search = { seg, actual };
                actual[0] = 0;
                out[at] = 0;
                SDL_EnumerateDirectory(out, segment_seen, &search);
                if (actual[0]) name = actual;
            }
            SDL_snprintf(out + at, HOST_PATH - at, "/%s", name);
            if (name == actual) type = path_type(out, info);
            if (type == SDL_PATHTYPE_NONE) err = W32_ERROR_FILE_NOT_FOUND;
        } else {
            /* below something that is missing or is not a directory */
            SDL_snprintf(out + at, HOST_PATH - at, "/%s", seg);
            type = SDL_PATHTYPE_NONE;
            err = W32_ERROR_PATH_NOT_FOUND;
        }
    }
    if (error) *error = err;
    return type;
}

/* Resolve for reading: sandbox copy wins, then the read roots in order. When
 * nothing is found, out and *error are those of the first read root. */
static SDL_PathType resolve_read(const char* rel, char* out, SDL_PathInfo* info, uint32_t* error) {
    SDL_PathType type = locate(g_sandbox, rel, out, info, error);
    for (int i = 0; type == SDL_PATHTYPE_NONE && i < g_nroots; i++)
        type = locate(g_roots[i], rel, out, info, error);
    if (type == SDL_PATHTYPE_NONE) locate(g_roots[0], rel, out, info, error);
    return type;
}
/* Resolve for writing: always the sandbox, parents created; seed it from a
 * read root when the file already exists there and is being opened, not
 * replaced. */
static SDL_PathType resolve_write(const char* rel, char* out, int keep, SDL_PathInfo* info, uint32_t* error) {
    SDL_PathType type = locate(g_sandbox, rel, out, info, error);
    if (type != SDL_PATHTYPE_NONE) return type;
    char* leaf = strrchr(out, '/');
    if (leaf) { *leaf = 0; SDL_CreateDirectory(out); *leaf = '/'; }
    if (keep) {
        char src[HOST_PATH];
        if (resolve_read(rel, src, NULL, NULL) == SDL_PATHTYPE_FILE) SDL_CopyFile(src, out);
    }
    type = path_type(out, info);
    if (error) *error = type == SDL_PATHTYPE_NONE ? W32_ERROR_FILE_NOT_FOUND : 0;
    return type;
}

/* ---- files ---- */
void imp_CreateFileA(void) {  /* (name, access, share, sa, disposition, flags, template) */
    char rel[W32_MAX_PATH], host[HOST_PATH];
    GUEST_REL(ARG(0), rel, W32_INVALID_HANDLE_VALUE, 7)
    uint32_t access = ARG(1), disp = ARG(4), error = 0;
    int write = (access & W32_GENERIC_WRITE) || disp == W32_CREATE_ALWAYS || disp == W32_CREATE_NEW ||
                disp == W32_TRUNCATE_EXISTING || disp == W32_OPEN_ALWAYS;
    SDL_PathType type = write
        ? resolve_write(rel, host, disp == W32_OPEN_EXISTING || disp == W32_OPEN_ALWAYS, NULL, &error)
        : resolve_read(rel, host, NULL, &error);
    int exists = type != SDL_PATHTYPE_NONE;
    const char* mode = NULL;
    if (type == SDL_PATHTYPE_DIRECTORY) error = W32_ERROR_ACCESS_DENIED;
    else switch (disp) {
    case W32_CREATE_NEW:
        if (exists) error = W32_ERROR_FILE_EXISTS; else { mode = "w+b"; error = 0; }
        break;
    case W32_CREATE_ALWAYS:
        mode = "w+b"; error = exists ? W32_ERROR_ALREADY_EXISTS : 0;
        break;
    case W32_OPEN_EXISTING:
        if (exists) { mode = write ? "r+b" : "rb"; error = 0; }
        break;
    case W32_OPEN_ALWAYS:
        mode = exists ? "r+b" : "w+b"; error = exists ? W32_ERROR_ALREADY_EXISTS : 0;
        break;
    case W32_TRUNCATE_EXISTING:
        if (exists) { mode = "w+b"; error = 0; }
        break;
    default:
        error = W32_ERROR_INVALID_PARAMETER;
    }
    uint32_t gh = W32_INVALID_HANDLE_VALUE;
    if (mode) {
        SDL_IOStream* io = SDL_IOFromFile(host, mode);
        if (io) {
            File* f = (File*)SDL_calloc(1, sizeof *f);
            SDL_SetAtomicInt(&f->object.refs, 1);
            f->lock = SDL_CreateMutex();
            f->io = io; f->access = access;
            SDL_strlcpy(f->path, host, sizeof f->path);
            SDL_LockSpinlock(&g_open_lock);
            f->next = g_open; g_open = f;
            SDL_UnlockSpinlock(&g_open_lock);
            gh = handle_new(f, HK_FILE);
            if (!gh) { files_release(HK_FILE, f); gh = W32_INVALID_HANDLE_VALUE; error = W32_ERROR_TOO_MANY_OPEN_FILES; }
        } else if (file_is_open(host)) {
            /* Not limited by the open-log budget: this is the evidence that
             * the guest relies on a share mode SDL does not give it. */
            fprintf(stderr, "[files] open \"%s\" refused: the guest already has it open (share modes are SDL's)\n", rel);
            error = W32_ERROR_SHARING_VIOLATION;
        } else {
            error = W32_ERROR_ACCESS_DENIED;
        }
    }
    g_last_error = error;
    const char* leaf = strrchr(rel, '\\');
    leaf = leaf ? leaf + 1 : rel;
    int save = 0;
    if (strlen(leaf) > 4 && !SDL_strncasecmp(leaf, "game", 4) && leaf[4] >= '0' && leaf[4] <= '9') {
        const char* suffix = leaf + 4;
        while (*suffix >= '0' && *suffix <= '9') ++suffix;
        save = !SDL_strcasecmp(suffix, ".dat");
    }
    // Save/reload acceptance must remain observable after asset loads exhaust
    // the general 200-open log budget. Emit exactly one successful save event.
    if (save && gh != W32_INVALID_HANDLE_VALUE)
        fprintf(stderr, "[save] open %s \"%s\" -> %s\n", write ? "W" : "R", rel, host);
    else if (g_log > 0) { g_log--; fprintf(stderr, "[files] open %s \"%s\" -> %s%s\n", write ? "W" : "R", rel, host, gh == W32_INVALID_HANDLE_VALUE ? " (FAILED)" : ""); }
    RET(gh); STDRET(7);
}
void imp_ReadFile(void) {  /* (h, buf, n, *read, overlapped) */
    uint32_t got = 0, count = ARG(2);
    int ok = 0, kind = handle_kind(ARG(0));
    File* f = (File*)handle_acquire(ARG(0), HK_FILE);
    if (f && !(f->access & W32_GENERIC_READ)) {
        g_last_error = W32_ERROR_ACCESS_DENIED;
    } else if (f) {
        /* Stage the host output: EOF/short reads only touch the bytes returned. */
        void* data = malloc(count ? count : 1);
        if (!data) {
            g_last_error = W32_ERROR_NOT_ENOUGH_MEMORY;
        } else {
            SDL_LockMutex(f->lock);
            got = (uint32_t)SDL_ReadIO(f->io, data, count);
            ok = got || !count || SDL_GetIOStatus(f->io) != SDL_IO_STATUS_ERROR;   /* the end of the file is not an error */
            SDL_UnlockMutex(f->lock);
            if (!ok) g_last_error = W32_ERROR_ACCESS_DENIED;
            if (got) memcpy(wd_host_range(ARG(1), got, 1), data, got);
            free(data);
        }
    } else if (kind == HK_STD) {
        ok = 1;                                    /* the game has no console: end of input */
    } else {
        g_last_error = W32_ERROR_INVALID_HANDLE;
    }
    if (f) files_release(HK_FILE, f);
    if (ARG(3)) WD_HOST_WRITE32(ARG(3)) = got;
    RET(ok); STDRET(5);
}
void imp_WriteFile(void) {
    uint32_t put = 0, count = ARG(2);
    int ok = 0, kind = handle_kind(ARG(0));
    File* f = (File*)handle_acquire(ARG(0), HK_FILE);
    if (f && !(f->access & W32_GENERIC_WRITE)) {
        g_last_error = W32_ERROR_ACCESS_DENIED;
    } else if (f) {
        SDL_LockMutex(f->lock);
        put = (uint32_t)SDL_WriteIO(f->io, wd_host_range(ARG(1), count, 0), count);
        SDL_UnlockMutex(f->lock);
        ok = put == count;
        if (!ok) g_last_error = W32_ERROR_ACCESS_DENIED;
    } else if (kind == HK_STD) {
        /* GetStdHandle's objects are 1 input, 2 output, 3 error. */
        FILE* stream = (uintptr_t)handle_get(ARG(0), HK_STD) == 2 ? stdout : stderr;
        put = (uint32_t)fwrite(wd_host_range(ARG(1), count, 0), 1, count, stream);
        ok = 1;
    } else {
        g_last_error = W32_ERROR_INVALID_HANDLE;
    }
    if (f) files_release(HK_FILE, f);
    if (ARG(3)) WD_HOST_WRITE32(ARG(3)) = put;
    RET(ok); STDRET(5);
}
void imp_SetFilePointer(void) {  /* (h, dist, *distHigh, method) */
    File* f = (File*)handle_acquire(ARG(0), HK_FILE);
    uint32_t r = W32_INVALID_SET_FILE_POINTER, method = ARG(3);
    if (!f) {
        g_last_error = W32_ERROR_INVALID_HANDLE;
    } else if (method > 2) {
        g_last_error = W32_ERROR_INVALID_PARAMETER;
    } else {
        int64_t distance = ARG(2) ? (int64_t)(((uint64_t)WD_HOST_READ32(ARG(2)) << 32) | ARG(1)) : (int32_t)ARG(1);
        static const SDL_IOWhence whence[3] = { SDL_IO_SEEK_SET, SDL_IO_SEEK_CUR, SDL_IO_SEEK_END };
        SDL_LockMutex(f->lock);
        int64_t position = method == 0 && distance < 0 ? -1 : SDL_SeekIO(f->io, distance, whence[method]);
        SDL_UnlockMutex(f->lock);
        if (position < 0) {
            g_last_error = W32_ERROR_NEGATIVE_SEEK;
        } else {
            r = (uint32_t)position;
            if (ARG(2)) WD_HOST_WRITE32(ARG(2)) = (uint32_t)(position >> 32);
        }
    }
    if (f) files_release(HK_FILE, f);
    RET(r); STDRET(4);
}
void imp_FlushFileBuffers(void) {
    File* f = (File*)handle_acquire(ARG(0), HK_FILE);
    int ok = 1;                                    /* also for what is not a file, as before the SDL port */
    if (f && !(f->access & W32_GENERIC_WRITE)) { ok = 0; g_last_error = W32_ERROR_ACCESS_DENIED; }
    else if (f) { SDL_LockMutex(f->lock); ok = SDL_FlushIO(f->io); SDL_UnlockMutex(f->lock); }
    if (f) files_release(HK_FILE, f);
    RET(ok); STDRET(1);
}
void imp_GetFileType(void) {
    int kind = handle_kind(ARG(0));
    RET(kind == HK_FILE ? W32_FILE_TYPE_DISK : kind == HK_STD ? W32_FILE_TYPE_CHAR : W32_FILE_TYPE_UNKNOWN);
    STDRET(1);
}

static uint32_t attributes_of(SDL_PathType type) {
    return type == SDL_PATHTYPE_DIRECTORY ? W32_FILE_ATTRIBUTE_DIRECTORY : W32_FILE_ATTRIBUTE_ARCHIVE;
}
void imp_GetFileAttributesA(void) {
    char rel[W32_MAX_PATH], host[HOST_PATH];
    uint32_t error = 0;
    GUEST_REL(ARG(0), rel, W32_INVALID_FILE_ATTRIBUTES, 1)
    if (!rel[0]) { RET(W32_FILE_ATTRIBUTE_DIRECTORY); STDRET(1); return; }
    SDL_PathType type = resolve_read(rel, host, NULL, &error);
    if (type == SDL_PATHTYPE_NONE) g_last_error = error;
    RET(type == SDL_PATHTYPE_NONE ? W32_INVALID_FILE_ATTRIBUTES : attributes_of(type)); STDRET(1);
}
void imp_DeleteFileA(void) {
    char rel[W32_MAX_PATH], host[HOST_PATH];
    uint32_t error = 0;
    GUEST_REL(ARG(0), rel, 0, 1)
    SDL_PathType type = locate(g_sandbox, rel, host, NULL, &error);
    int ok = 0;
    if (type == SDL_PATHTYPE_NONE) g_last_error = error;
    else if (type != SDL_PATHTYPE_FILE || !SDL_RemovePath(host)) g_last_error = W32_ERROR_ACCESS_DENIED;
    else ok = 1;
    RET(ok); STDRET(1);
}
void imp_MoveFileA(void) {
    char r0[W32_MAX_PATH], r1[W32_MAX_PATH], h0[HOST_PATH], h1[HOST_PATH];
    uint32_t error = 0;
    GUEST_REL(ARG(0), r0, 0, 2)
    GUEST_REL(ARG(1), r1, 0, 2)
    SDL_PathType from = resolve_write(r0, h0, 1, NULL, &error);
    SDL_PathType to = resolve_write(r1, h1, 0, NULL, NULL);
    int ok = 0;
    if (from == SDL_PATHTYPE_NONE) g_last_error = error ? error : W32_ERROR_FILE_NOT_FOUND;
    else if (to != SDL_PATHTYPE_NONE) g_last_error = W32_ERROR_ALREADY_EXISTS;
    else if (!SDL_RenamePath(h0, h1)) g_last_error = W32_ERROR_ACCESS_DENIED;
    else ok = 1;
    RET(ok); STDRET(2);
}

/* ---- FindFirstFile ----
 * The pattern is matched the way Win32 does it: kernel32 rewrites it into the
 * NT matcher's alphabet (below) and the file system compares upper-cased
 * names. "*.*" is every name; '?' and a dot before a wildcard also match at
 * the end of a name, so "*." is the names without an extension. The "." and
 * ".." entries match as the empty name. verify/kernel_bridge_smoke.py compares
 * generated patterns with FindFirstFileW on the same directory. */
enum { DOS_STAR = '<', DOS_QM = '>', DOS_DOT = '"' };

static void dos_expression(const char* pattern, char* out) {
    size_t n = strlen(pattern);
    int wild = strpbrk(pattern, "*?") != NULL;
    while (n > 1 && pattern[n - 1] == '.' && pattern[n - 2] == '.') n--;
    if (n == 3 && !strncmp(pattern, "*.*", 3)) { strcpy(out, "*"); return; }
    for (size_t i = 0; i < n; i++) {
        char c = (char)SDL_toupper((unsigned char)pattern[i]);
        if (i && c == '.' && pattern[i - 1] == '*') out[i - 1] = DOS_STAR;
        if (c == '?') { c = DOS_QM; if (i && pattern[i - 1] == '.') out[i - 1] = DOS_DOT; }
        else if (c == '*' && i && pattern[i - 1] == '.') out[i - 1] = DOS_DOT;
        out[i] = c;
    }
    /* A trailing dot is not part of a name. */
    if (n && out[n - 1] == '.') { if (wild) out[n - 1] = DOS_DOT; else n--; }
    out[n] = 0;
}
static int dos_match(const char* p, const char* n) {
    for (;; p++, n++) {
        switch (*p) {
        case 0:
            return !*n;
        case '*':
            for (;; n++) { if (dos_match(p + 1, n)) return 1; if (!*n) return 0; }
        case DOS_STAR: {                           /* any characters, but not past the last dot */
            const char* dot = strrchr(n, '.');
            const char* limit = dot ? dot : n + strlen(n);
            for (;; n++) { if (dos_match(p + 1, n)) return 1; if (n == limit) return 0; }
        }
        case DOS_QM:
            if (!*n || *n == '.') {                /* nothing left of this part: skip the run */
                while (*p == DOS_QM) p++;
                return dos_match(p, n);
            }
            break;
        case DOS_DOT:
            if (!*n) return dos_match(p + 1, n);
            if (*n != '.') return 0;
            break;
        default:
            if ((char)SDL_toupper((unsigned char)*n) != *p) return 0;
        }
    }
}

typedef struct { Find* find; const char* expression; int capacity; } FindSearch;
static void find_add(FindSearch* search, const char* name, const char* path) {
    Find* find = search->find;
    if (strlen(name) >= W32_MAX_PATH) return;
    if (find->count == search->capacity) {
        search->capacity = search->capacity ? search->capacity * 2 : 16;
        find->entries = (FindEntry*)SDL_realloc(find->entries, (size_t)search->capacity * sizeof(FindEntry));
    }
    FindEntry* entry = &find->entries[find->count];
    if (path_type(path, &entry->info) == SDL_PATHTYPE_NONE) return;
    strcpy(entry->name, name);
    find->count++;
}
static SDL_EnumerationResult SDLCALL find_seen(void* userdata, const char* dirname, const char* fname) {
    FindSearch* search = (FindSearch*)userdata;
    char path[HOST_PATH];
    if (dos_match(search->expression, fname)) {
        size_t n = strlen(dirname);
        SDL_snprintf(path, sizeof path, "%s%s%s", dirname, n && (dirname[n - 1] == '/' || dirname[n - 1] == '\\') ? "" : "/", fname);
        find_add(search, fname, path);
    }
    return SDL_ENUM_CONTINUE;
}
static int find_order(const void* a, const void* b) {   /* by upper-cased name, as NTFS lists */
    const unsigned char* x = (const unsigned char*)((const FindEntry*)a)->name;
    const unsigned char* y = (const unsigned char*)((const FindEntry*)b)->name;
    for (;; x++, y++) {
        int d = SDL_toupper(*x) - SDL_toupper(*y);
        if (d || !*x) return d;
    }
}
/* The entries of one root's directory that match, or NULL with the Win32 error. */
static Find* find_in(const char* root, const char* dir, const char* expression, uint32_t* error) {
    char host[HOST_PATH];
    if (locate(root, dir, host, NULL, NULL) != SDL_PATHTYPE_DIRECTORY) { *error = W32_ERROR_PATH_NOT_FOUND; return NULL; }
    Find* find = (Find*)SDL_calloc(1, sizeof *find);
    SDL_SetAtomicInt(&find->object.refs, 1);
    FindSearch search = { find, expression, 0 };
    SDL_EnumerateDirectory(host, find_seen, &search);
    SDL_qsort(find->entries, (size_t)find->count, sizeof(FindEntry), find_order);
    if (dos_match(expression, "")) {               /* "." and "..", ahead of the names */
        int names = find->count;
        find_add(&search, ".", host);
        find_add(&search, "..", host);
        int dots = find->count - names;
        if (dots) {
            FindEntry* moved = (FindEntry*)SDL_malloc((size_t)find->count * sizeof(FindEntry));
            memcpy(moved, find->entries + names, (size_t)dots * sizeof(FindEntry));
            memcpy(moved + dots, find->entries, (size_t)names * sizeof(FindEntry));
            SDL_free(find->entries);
            find->entries = moved;
        }
    }
    if (find->count) return find;
    files_release(HK_FIND, find);
    *error = W32_ERROR_FILE_NOT_FOUND;
    return NULL;
}
/* WIN32_FIND_DATAA holds no pointers: 320 bytes, same layout on every host. */
static void find_write(uint32_t va, const FindEntry* entry) {
    uint8_t* data = (uint8_t*)wd_host_range(va, W32_FIND_DATA_SIZE, 1);
    uint32_t words[11] = { attributes_of(entry->info.type) };
    int directory = entry->info.type == SDL_PATHTYPE_DIRECTORY;
    SDL_TimeToWindows(entry->info.create_time, &words[1], &words[2]);
    SDL_TimeToWindows(entry->info.access_time, &words[3], &words[4]);
    SDL_TimeToWindows(entry->info.modify_time, &words[5], &words[6]);
    words[7] = directory ? 0 : (uint32_t)(entry->info.size >> 32);
    words[8] = directory ? 0 : (uint32_t)entry->info.size;
    memset(data, 0, W32_FIND_DATA_SIZE);
    memcpy(data, words, sizeof words);
    strcpy((char*)data + W32_FIND_DATA_NAME, entry->name);
}
void imp_FindFirstFileA(void) {
    char rel[W32_MAX_PATH], dir[W32_MAX_PATH], expression[W32_MAX_PATH];
    GUEST_REL(ARG(0), rel, W32_INVALID_HANDLE_VALUE, 2)
    strcpy(dir, rel);
    char* s = strrchr(dir, '\\');
    const char* leaf = s ? s + 1 : rel;
    dos_expression(leaf, expression);
    if (s) *s = 0; else dir[0] = 0;
    /* The sandbox, then each read root: the first that has a match answers,
     * and after none the error is the last root's. */
    uint32_t error = 0;
    Find* find = find_in(g_sandbox, dir, expression, &error);
    for (int i = 0; !find && i < g_nroots; i++) find = find_in(g_roots[i], dir, expression, &error);
    if (g_log > 0) { g_log--; fprintf(stderr, "[files] find \"%s\"%s\n", rel, find ? "" : " (none)"); }
    if (!find) { g_last_error = error; RET(W32_INVALID_HANDLE_VALUE); STDRET(2); return; }
    uint32_t gh = handle_new(find, HK_FIND);
    if (!gh) { files_release(HK_FIND, find); g_last_error = W32_ERROR_TOO_MANY_OPEN_FILES; RET(W32_INVALID_HANDLE_VALUE); STDRET(2); return; }
    find_write(ARG(1), &find->entries[find->next++]);
    RET(gh); STDRET(2);
}
void imp_FindNextFileA(void) {
    Find* find = (Find*)handle_acquire(ARG(0), HK_FIND);
    int index = -1;
    if (find) {
        SDL_LockSpinlock(&find->lock);
        if (find->next < find->count) index = find->next++;
        SDL_UnlockSpinlock(&find->lock);
    }
    if (index >= 0) find_write(ARG(1), &find->entries[index]);
    else g_last_error = find ? W32_ERROR_NO_MORE_FILES : W32_ERROR_INVALID_HANDLE;
    if (find) files_release(HK_FIND, find);
    RET(index >= 0); STDRET(2);
}
void imp_FindClose(void) { handle_close(ARG(0)); RET(1); STDRET(1); }

/* ---- the guest's directories: names only, nothing on the host changes ---- */
void imp_GetCurrentDirectoryA(void) {  /* (len, buf) */
    char s[W32_MAX_PATH + 3];
    sprintf(s, "C:\\%s", g_cwd);
    guest_strcpy_out(ARG(1), ARG(0), s);
    RET(strlen(s)); STDRET(2);
}
void imp_SetCurrentDirectoryA(void) {
    char rel[W32_MAX_PATH];
    GUEST_REL(ARG(0), rel, 0, 1)
    strcpy(g_cwd, rel);
    fprintf(stderr, "[files] chdir C:\\%s\n", g_cwd);
    RET(1); STDRET(1);
}
void imp_GetFullPathNameA(void) {  /* (name, len, buf, *filePart) */
    char rel[W32_MAX_PATH], s[W32_MAX_PATH + 3];
    GUEST_REL(ARG(0), rel, 0, 4)
    sprintf(s, "C:\\%s", rel);
    uint32_t n = (uint32_t)strlen(s);
    if (n + 1 > ARG(1)) { RET(n + 1); STDRET(4); return; }
    guest_strcpy_out(ARG(2), ARG(1), s);
    if (ARG(3)) { char* f = strrchr(s, '\\'); WD_HOST_WRITE32(ARG(3)) = ARG(2) + (uint32_t)(f ? f + 1 - s : 0); }
    RET(n); STDRET(4);
}

/* ---- file times ----
 * FILETIME: 100 ns ticks since 1601-01-01. DOS date and time: years from 1980
 * in 7 bits, two-second resolution. */
#define TICKS_PER_SECOND 10000000ull
#define DAYS_1601_TO_1970 134774

static int64_t days_from_civil(int y, unsigned m, unsigned d) {   /* days since 1970-01-01 */
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}
static void civil_from_days(int64_t z, int* y, unsigned* m, unsigned* d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(yoe + era * 400) + (*m <= 2);
}
static uint64_t filetime_read(uint32_t va) {
    return WD_HOST_READ32(va) | ((uint64_t)WD_HOST_READ32(va + 4) << 32);
}
static void filetime_write(uint32_t va, uint64_t value) {
    WD_HOST_WRITE32(va) = (uint32_t)value; WD_HOST_WRITE32(va + 4) = (uint32_t)(value >> 32);
}
void imp_DosDateTimeToFileTime(void) {  /* (date, time, *filetime) */
    uint32_t date = ARG(0) & 0xFFFF, time = ARG(1) & 0xFFFF;
    int year = 1980 + (int)(date >> 9);
    unsigned month = (date >> 5) & 15, day = date & 31;
    unsigned hour = time >> 11, minute = (time >> 5) & 63, second = (time & 31) * 2;
    int ok = month >= 1 && month <= 12 && day >= 1 && (int)day <= SDL_GetDaysInMonth(year, (int)month) &&
             hour < 24 && minute < 60 && second < 60;
    if (ok) {
        uint64_t seconds = (uint64_t)(days_from_civil(year, month, day) + DAYS_1601_TO_1970) * 86400u +
                           hour * 3600u + minute * 60u + second;
        filetime_write(ARG(2), seconds * TICKS_PER_SECOND);
    } else {
        g_last_error = W32_ERROR_INVALID_PARAMETER;
    }
    RET(ok); STDRET(3);
}
void imp_FileTimeToDosDateTime(void) {  /* (*filetime, *date, *time) */
    /* Rounded up to the next even second, as Win32 does. */
    uint64_t seconds = (filetime_read(ARG(0)) + 2 * TICKS_PER_SECOND - 1) / (2 * TICKS_PER_SECOND) * 2;
    int year; unsigned month, day;
    civil_from_days((int64_t)(seconds / 86400u) - DAYS_1601_TO_1970, &year, &month, &day);
    int ok = year >= 1980 && year <= 2107;
    if (ok) {
        uint32_t rest = (uint32_t)(seconds % 86400u);
        WD_HOST_WRITE16(ARG(1)) = (uint16_t)(((year - 1980) << 9) | (month << 5) | day);
        WD_HOST_WRITE16(ARG(2)) = (uint16_t)(((rest / 3600u) << 11) | ((rest / 60u % 60u) << 5) | (rest % 60u / 2));
    } else {
        g_last_error = W32_ERROR_INVALID_PARAMETER;
    }
    RET(ok); STDRET(3);
}
/* Win32 applies the zone's current offset from UTC to every date. */
static int64_t local_offset_ticks(void) {
    SDL_Time now;
    SDL_DateTime local;
    if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &local, true)) return 0;
    return (int64_t)local.utc_offset * (int64_t)TICKS_PER_SECOND;
}
void imp_FileTimeToLocalFileTime(void) {
    filetime_write(ARG(1), filetime_read(ARG(0)) + (uint64_t)local_offset_ticks());
    RET(1); STDRET(2);
}
void imp_LocalFileTimeToFileTime(void) {
    filetime_write(ARG(1), filetime_read(ARG(0)) - (uint64_t)local_offset_ticks());
    RET(1); STDRET(2);
}

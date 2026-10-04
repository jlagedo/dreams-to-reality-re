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
 *
 * Disc mode (WD_DISC1 and WD_DISC2 set: each a .cue, .iso or extracted
 * directory, read through recomp/disc): the read roots are the two discs
 * instead of directories, and WD_READ_ROOTS and the EXE's directory are not
 * used. Host-only structure; the guest keeps its retail view of a CD root and
 * an install root C:\CRYO\DREAMS (docs/research/install-and-discs.md):
 *   - a read looks in the sandbox, then on the active disc;
 *   - below CRYO\DREAMS\ it then drops that prefix and looks on the active
 *     disc and on the other one (DATA\HD.ID, DATA\3DC\DIALOG.DRD which only
 *     disc 1 has, data\anim\*.HNM, data\replay.bin); but never for a file of
 *     CRYO\DREAMS\DATA\GAME\, the saves: an install starts with that
 *     directory empty, and disc 2 carries a leftover GAME.DAT, GAME0.DAT and
 *     GAME0.ICO there that would show up as the player's own;
 *   - CRYO\DREAMS\DATA\FULL.ID is never found, not even in the sandbox: disc 1
 *     has a DATA\FULL.ID, and seeing it at the install root would switch the
 *     game to its copy-to-hard-disk mode;
 *   - writes, creates, deletes and moves go to the sandbox, as always.
 * Disc 1 is active at start. The game asks about discs only by opening
 * DATA\1CD.ID or DATA\2CD.ID at the CD root (CD_FindDrive, CD_GetDiscNumber,
 * CD_PromptSwap), and each disc holds only its own marker, so the open
 * succeeds for the active disc alone; the sandbox is not consulted for them.
 * Two CreateFileA calls in a row for the other disc's marker are the swap
 * prompt polling: the second one makes that disc the active one and succeeds
 * ("[disc] active disc 1 -> 2"). A file already open keeps reading the disc it
 * was opened on. FindFirstFileA answers from the sandbox, the active disc and,
 * below CRYO\DREAMS\, the prefix-less directory of the active and the other
 * disc (not for the saves directory): the first with a match, unmerged as
 * above. Disc files have no dates (FILETIME 1970-01-01).
 *
 * Tree mode (WD_TREE: a directory; the browser build always): one tree is
 * both the CD root and the install root, as the developers' hard-disk game
 * had it and as the Develop and Play edits launch modes serve the developer
 * folder (docs/specs/008-editor-restoration/spec.md, phase M). Host-only
 * structure; the guest keeps its retail view:
 *   - the tree is the only read root and the write root (saves land in its
 *     DATA\GAME, edits in place); WD_READ_ROOTS and WD_DATA_DIR's sandbox
 *     do not apply, and the guest EXE is the tree's GDIDREAM.EXE;
 *   - CRYO\DREAMS\x is x;
 *   - DATA\FULL.ID is never found: its copy-to-hard-disk mode would purge
 *     DATA\3DC and DATA\ANIM of the one tree before copying them back;
 *   - DATA\1CD.ID, DATA\2CD.ID and DATA\HD.ID are created if missing.
 * With WD_DISC1 and WD_DISC2 as well (Play edits), the discs are opened for
 * their audio tracks only: files still come from the tree, and the last disc
 * marker the game opened (CD_GetDiscNumber opens 1CD.ID, CD_PromptSwap
 * 2CD.ID) picks the disc whose tracks winmm.c plays.
 *
 *   WD_TREE             the tree (tree mode)
 *   WD_DISC1, WD_DISC2  the discs (disc mode); both or neither
 *   WD_DISC_ACTIVE=2    start with disc 2 in the drive (default 1; either disc
 *                       boots the game, and a new game then asks for disc 1)
 *   WD_DATA_DIR         the user data directory: the sandbox under its
 *                       product name; takes the place of WD_WRITE_ROOT
 *   WD_FILES_LOG=N      log the first N opens (default 200)
 * These and WD_READ_ROOTS, WD_WRITE_ROOT are read with SDL_getenv, which on
 * Windows is the Unicode environment as UTF-8 (C getenv gives ANSI code page
 * bytes): a path with non-ASCII characters works whatever the code page, and
 * a setter inside the process must use SDL_setenv_unsafe, before the first
 * file call.
 */
#define RECOMP_GENERATED_CODE
#include "host.h"
#include "disc.h"

#define MAX_ROOTS 4
#define HOST_PATH 1024               /* host paths; guest paths are at most W32_MAX_PATH */
static char g_roots[MAX_ROOTS][HOST_PATH];
static int g_folds[MAX_ROOTS + 1];   /* the root's file system ignores case (sandbox: last) */
static int g_nroots;
static char g_sandbox[HOST_PATH];
static char g_cwd[W32_MAX_PATH] = "";   /* guest cwd below C:\, no leading/trailing '\' */
static int g_log = 200;                 /* log the first N opens (WD_FILES_LOG) */
static int g_walk;                      /* WD_FILES_CASE_WALK */

/* Disc mode. A Disc is read-only once open; only the active number and the
 * marker count change, and guest threads open files concurrently. */
static Disc* g_disc[3];                 /* [1], [2]: WD_DISC1, WD_DISC2; NULL in legacy mode */
static SDL_AtomicInt g_active;          /* 1 or 2 */
static SDL_SpinLock g_marker_lock;
static int g_marker_miss;               /* the marker (1, 2) whose open just failed, no other open since */
static int g_log_disc = 200;            /* marker opens and failed install-root opens, logged past g_log */
static void (*g_on_switch)(void);       /* winmm.c: the CD audio device follows the active disc */
static int g_tree;                      /* tree mode: one tree is CD root, install root and write root */
typedef struct { int disc; const char* path; DiscEntry entry; } DiscHit;   /* disc 0: not on a disc; path: in the caller's rel */

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

/* ---- tree mode ---- */
/* WD_TREE, or NULL. Read before files_init (the guest EXE comes from it). */
const char* files_tree(void) {
    const char* tree = SDL_getenv("WD_TREE");
    return tree && *tree ? tree : NULL;
}
/* Files come from the discs (disc mode proper; in tree mode the discs, when
 * open, are for their audio only). */
static int disc_files(void) { return g_disc[1] && !g_tree; }

/* ---- disc mode ---- */
/* Open the discs. 1: disc mode; 0: WD_DISC1 is unset, the directory read
 * roots apply; -1: a disc is missing or wrong (the message is printed). */
int files_open_discs(void) {
    const char* path[3] = { NULL, SDL_getenv("WD_DISC1"), SDL_getenv("WD_DISC2") };
    if (g_disc[1]) return 1;
    if (!path[1] || !*path[1]) return 0;
    for (int n = 1; n <= 2; n++) {
        char err[512] = "";
        Disc* disc = path[n] && *path[n] ? disc_open(path[n], err, sizeof err) : NULL;
        int number = disc ? disc_number(disc) : 0;
        if (!path[n] || !*path[n]) fprintf(stderr, "FATAL: WD_DISC1 is set but WD_DISC2 is not: the game needs both discs\n");
        else if (!disc) fprintf(stderr, "FATAL: WD_DISC%d: cannot open %s: %s\n", n, path[n], err);
        else if (number != n) fprintf(stderr, "FATAL: WD_DISC%d: %s is not disc %d of the game (DATA\\%dCD.ID %s)\n", n, path[n], n, n,
                                      number ? "is missing, it holds the other disc's marker" : "is missing");
        if (number != n) {
            disc_close(disc); disc_close(g_disc[1]);
            g_disc[1] = NULL;
            return -1;
        }
        g_disc[n] = disc;
        fprintf(stderr, "[files] disc %d: %s (%d tracks)\n", n, path[n], disc_track_count(disc));
    }
    const char* first = SDL_getenv("WD_DISC_ACTIVE");
    SDL_SetAtomicInt(&g_active, first && *first == '2' ? 2 : 1);
    return 1;
}
/* A whole file of a disc (1, 2) in a malloc'd block: the guest EXE. */
void* files_disc_read(int disc, const char* path, size_t* size) {
    DiscEntry entry;
    if (disc < 1 || disc > 2 || !g_disc[disc] || !disc_find(g_disc[disc], path, &entry) || entry.is_dir) return NULL;
    DiscFile* file = disc_file_open(g_disc[disc], &entry);
    void* data = file ? malloc(entry.size ? (size_t)entry.size : 1) : NULL;
    if (data && disc_file_read(file, 0, data, (size_t)entry.size) != (int64_t)entry.size) { free(data); data = NULL; }
    disc_file_close(file);
    if (data) *size = (size_t)entry.size;
    return data;
}
/* The active disc and its number, NULL in legacy mode; on_switch (when given)
 * is called after every change of the active disc. */
Disc* files_active_disc(int* number, void (*on_switch)(void)) {
    int active = SDL_GetAtomicInt(&g_active);
    if (on_switch) g_on_switch = on_switch;
    if (number) *number = g_disc[1] ? active : 0;
    return g_disc[1] ? g_disc[active] : NULL;
}

/* 1 or 2 for DATA\1CD.ID and DATA\2CD.ID at the CD root, else 0. */
static int marker_of(const char* rel) {
    return !SDL_strcasecmp(rel, "DATA\\1CD.ID") ? 1 : !SDL_strcasecmp(rel, "DATA\\2CD.ID") ? 2 : 0;
}
/* rel at or below the install root: what follows it ("" for the root). */
static const char* below_install(const char* rel) {
    static const char install[] = "CRYO\\DREAMS";
    size_t n = sizeof install - 1;
    if (SDL_strncasecmp(rel, install, n)) return NULL;
    return rel[n] == '\\' ? rel + n + 1 : rel[n] ? NULL : rel + n;
}
/* sub (a path below the install root) is the saves directory (1) or in it (2). */
static int in_saves(const char* sub) {
    static const char saves[] = "DATA\\GAME";
    size_t n = sizeof saves - 1;
    if (SDL_strncasecmp(sub, saves, n)) return 0;
    return sub[n] == '\\' ? 2 : sub[n] ? 0 : 1;
}
static int full_id(const char* rel) {
    const char* sub = below_install(rel);
    return sub && !SDL_strcasecmp(sub, "DATA\\FULL.ID");
}
/* Every CreateFileA passes here (marker: marker_of a path opened for reading,
 * else 0). The second of two opens in a row of the other disc's marker is the
 * swap prompt's poll: that disc becomes the active one. */
static void disc_note_open(int marker) {
    int from = 0;
    SDL_LockSpinlock(&g_marker_lock);
    int active = SDL_GetAtomicInt(&g_active);
    if (g_tree) { if (marker && marker != active) { from = active; SDL_SetAtomicInt(&g_active, marker); } }
    else if (!marker || marker == active) g_marker_miss = 0;
    else if (g_marker_miss != marker) g_marker_miss = marker;
    else { g_marker_miss = 0; from = active; SDL_SetAtomicInt(&g_active, marker); }
    SDL_UnlockSpinlock(&g_marker_lock);
    if (from) {
        fprintf(stderr, "[disc] active disc %d -> %d\n", from, marker);
        if (g_on_switch) g_on_switch();
    }
}
/* rel on the active disc, or for a path below the install root on either
 * (the saves are the sandbox's alone). */
static int disc_lookup(const char* rel, DiscHit* hit) {
    int active = SDL_GetAtomicInt(&g_active);
    const char* sub = below_install(rel);
    if (sub && in_saves(sub) == 2) sub = NULL;
    hit->disc = 0;
    if (disc_find(g_disc[active], rel, &hit->entry)) { hit->disc = active; hit->path = rel; }
    for (int k = 0; sub && !hit->disc && k < 2; k++) {
        int n = k ? 3 - active : active;
        if (disc_find(g_disc[n], sub, &hit->entry)) { hit->disc = n; hit->path = sub; }
    }
    return hit->disc;
}
static void disc_name(int disc, const char* path, char* out) {   /* "disc1:DATA/1CD.ID", for the log */
    SDL_snprintf(out, HOST_PATH, "disc%d:%s", disc, path);
    slashes(out);
}

/* A disc file as an SDL stream: its own DiscFile, so its own host file position. */
typedef struct { DiscFile* file; Sint64 size, pos; } DiscStream;
static Sint64 SDLCALL disc_io_size(void* userdata) { return ((DiscStream*)userdata)->size; }
static Sint64 SDLCALL disc_io_seek(void* userdata, Sint64 offset, SDL_IOWhence whence) {
    DiscStream* s = (DiscStream*)userdata;
    Sint64 base = whence == SDL_IO_SEEK_SET ? 0 : whence == SDL_IO_SEEK_CUR ? s->pos : s->size;
    if (base + offset < 0) { SDL_SetError("seek before the start of the file"); return -1; }
    return s->pos = base + offset;                 /* past the end is allowed, as for a Win32 file */
}
static size_t SDLCALL disc_io_read(void* userdata, void* ptr, size_t size, SDL_IOStatus* status) {
    DiscStream* s = (DiscStream*)userdata;
    int64_t got = s->pos < s->size ? disc_file_read(s->file, (uint64_t)s->pos, ptr, size) : 0;
    if (got < 0) { *status = SDL_IO_STATUS_ERROR; return 0; }
    if ((size_t)got < size) *status = SDL_IO_STATUS_EOF;
    s->pos += got;
    return (size_t)got;
}
static bool SDLCALL disc_io_close(void* userdata) {
    disc_file_close(((DiscStream*)userdata)->file);
    SDL_free(userdata);
    return true;
}
static SDL_IOStream* disc_io(const DiscHit* hit) {
    SDL_IOStreamInterface iface;
    DiscFile* file = disc_file_open(g_disc[hit->disc], &hit->entry);
    if (!file) return NULL;
    DiscStream* s = (DiscStream*)SDL_calloc(1, sizeof *s);
    s->file = file; s->size = (Sint64)hit->entry.size;
    SDL_INIT_INTERFACE(&iface);
    iface.size = disc_io_size; iface.seek = disc_io_seek; iface.read = disc_io_read; iface.close = disc_io_close;
    SDL_IOStream* io = SDL_OpenIO(&iface, s);
    if (!io) disc_io_close(s);
    return io;
}
/* Copy a disc file to a host path (a file opened for update is seeded in the sandbox). */
static void disc_copy_out(const DiscHit* hit, const char* out) {
    SDL_IOStream* from = disc_io(hit);
    SDL_IOStream* to = from ? SDL_IOFromFile(out, "wb") : NULL;
    size_t size = 65536;
    char* block = (char*)SDL_malloc(size);
    for (size_t n; to && block && (n = SDL_ReadIO(from, block, size)) > 0;)
        if (SDL_WriteIO(to, block, n) != n) break;
    SDL_free(block);
    if (to) SDL_CloseIO(to);
    if (from) SDL_CloseIO(from);
}

void files_init(const char* exe) {
    char dir[HOST_PATH];
#ifdef __EMSCRIPTEN__
    g_tree = 1;
#else
    g_tree = files_tree() != NULL;
#endif
    if (!disc_files()) {
        absolute(exe, dir);
        char* s = strrchr(dir, '/');
        if (s) *s = 0;
        SDL_strlcpy(g_roots[g_nroots++], dir, HOST_PATH);
        const char* extra = g_tree ? NULL : SDL_getenv("WD_READ_ROOTS");
        if (extra) {
            char tmp[4 * HOST_PATH]; char* ctx = NULL;
            SDL_strlcpy(tmp, extra, sizeof tmp);
            for (char* t = SDL_strtok_r(tmp, ";", &ctx); t && g_nroots < MAX_ROOTS; t = SDL_strtok_r(NULL, ";", &ctx))
                absolute(t, g_roots[g_nroots++]);
        }
    }
    const char* w = files_tree();
    if (!w) w = SDL_getenv("WD_DATA_DIR");
    if (!w || !*w) w = SDL_getenv("WD_WRITE_ROOT");
    absolute(w && *w ? w : "sandbox", g_sandbox);
    SDL_CreateDirectory(g_sandbox);
    const char* walk = SDL_getenv("WD_FILES_CASE_WALK");
    g_walk = walk && *walk && *walk != '0';
    const char* budget = SDL_getenv("WD_FILES_LOG");
    if (budget && *budget) g_log = SDL_atoi(budget);
    for (int i = 0; i < g_nroots; i++) {
        g_folds[i] = folds_case(g_roots[i]);
        fprintf(stderr, "[files] read root %d: %s\n", i, g_roots[i]);
    }
    g_folds[MAX_ROOTS] = folds_case(g_sandbox);
    fprintf(stderr, "[files] write sandbox: %s%s\n", g_sandbox, g_tree ? " (tree mode)" : "");
    /* Tree mode. The game only opens these markers (CD_FindDrive,
     * CD_FindCacheDrive, CD_GetDiscNumber, CD_PromptSwap): the one tree stands
     * for both discs and the install, so they exist whether or not it holds them. */
    static const char* const markers[] = { "DATA/1CD.ID", "DATA/2CD.ID", "DATA/HD.ID" };
    for (int i = 0; g_tree && i < 3; i++) {
        char path[HOST_PATH];
        SDL_snprintf(path, sizeof path, "%s/%s", g_sandbox, markers[i]);
        if (path_type(path, NULL) != SDL_PATHTYPE_NONE) continue;
        SDL_snprintf(path, sizeof path, "%s/DATA", g_sandbox);
        SDL_CreateDirectory(path);
        SDL_snprintf(path, sizeof path, "%s/%s", g_sandbox, markers[i]);
        SDL_IOStream* io = SDL_IOFromFile(path, "wb");
        if (io) SDL_CloseIO(io);
        fprintf(stderr, "[files] marker %s created\n", markers[i]);
    }
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
    /* Tree mode: the one tree is both the CD root and the install root, so
     * CRYO\DREAMS\x is x (the browser pack's paths are relative to /dreams). */
    if (g_tree && n >= 2 && !SDL_strcasecmp(seg[0], "CRYO") && !SDL_strcasecmp(seg[1], "DREAMS")) {
        for (int i = 0; i + 2 < n; i++) seg[i] = seg[i + 2];
        n -= 2;
    }
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
static SDL_PathType resolve_read_disc(const char* rel, char* out, SDL_PathInfo* info, uint32_t* error, DiscHit* hit);
static SDL_PathType resolve_read(const char* rel, char* out, SDL_PathInfo* info, uint32_t* error, DiscHit* hit) {
    if (hit) hit->disc = 0;
    if (disc_files()) return resolve_read_disc(rel, out, info, error, hit);
    /* Tree mode: DATA\FULL.ID would switch the game to its copy-to-hard-disk
     * mode, which purges DATA\3DC and DATA\ANIM of the one tree that is both
     * CD and install before copying them back (CD_PurgeCache 0x4281d9). */
    if (g_tree && !SDL_strcasecmp(rel, "DATA\\FULL.ID") && !SDL_getenv("WD_WEB_FULL_ID")) {
        SDL_snprintf(out, HOST_PATH, "%s/%s", g_sandbox, "DATA/FULL.ID");
        if (error) *error = W32_ERROR_FILE_NOT_FOUND;
        return SDL_PATHTYPE_NONE;
    }
    SDL_PathType type = locate(g_sandbox, rel, out, info, error);
    for (int i = 0; type == SDL_PATHTYPE_NONE && i < g_nroots; i++)
        type = locate(g_roots[i], rel, out, info, error);
    if (type == SDL_PATHTYPE_NONE) locate(g_roots[0], rel, out, info, error);
    return type;
}
/* The same in disc mode: the sandbox copy wins (but a disc marker is the
 * active disc's alone and FULL.ID at the install root is never there), then
 * the discs. *hit says which disc has it; out is then a name for the log, not
 * a host path. When nothing is found, out and *error are the active disc's. */
static SDL_PathType resolve_read_disc(const char* rel, char* out, SDL_PathInfo* info, uint32_t* error, DiscHit* hit) {
    SDL_PathInfo local_info;
    DiscHit local_hit;
    uint32_t err = 0;
    int hidden = full_id(rel);
    if (!info) info = &local_info;
    if (!hit) hit = &local_hit;
    hit->disc = 0;
    SDL_PathType type = hidden || marker_of(rel) ? SDL_PATHTYPE_NONE : locate(g_sandbox, rel, out, info, &err);
    if (type == SDL_PATHTYPE_NONE && !hidden && disc_lookup(rel, hit)) {
        SDL_zerop(info);
        info->type = type = hit->entry.is_dir ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_FILE;
        info->size = hit->entry.size;
        disc_name(hit->disc, hit->path, out);
        err = 0;
    } else if (type == SDL_PATHTYPE_NONE) {
        /* the name is missing if its directory is on a disc or in the sandbox, else the path is */
        char parent[W32_MAX_PATH];
        DiscHit dir;
        SDL_strlcpy(parent, rel, sizeof parent);
        char* leaf = strrchr(parent, '\\');
        if (leaf) *leaf = 0; else parent[0] = 0;
        if (err != W32_ERROR_FILE_NOT_FOUND)
            err = disc_lookup(parent, &dir) && dir.entry.is_dir ? W32_ERROR_FILE_NOT_FOUND : W32_ERROR_PATH_NOT_FOUND;
        disc_name(SDL_GetAtomicInt(&g_active), rel, out);
    }
    if (error) *error = err;
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
        DiscHit hit;
        if (resolve_read(rel, src, NULL, NULL, &hit) == SDL_PATHTYPE_FILE) {
            if (hit.disc) disc_copy_out(&hit, out); else SDL_CopyFile(src, out);
        }
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
    DiscHit hit = { 0 };
    if (g_disc[1]) disc_note_open(write ? 0 : marker_of(rel));   /* in tree mode: the audio disc */
    SDL_PathType type = write
        ? resolve_write(rel, host, disp == W32_OPEN_EXISTING || disp == W32_OPEN_ALWAYS, NULL, &error)
        : resolve_read(rel, host, NULL, &error, &hit);
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
        SDL_IOStream* io = hit.disc ? disc_io(&hit) : SDL_IOFromFile(host, mode);
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
        } else if (!hit.disc && file_is_open(host)) {
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
    /* In disc mode the marker opens (the disc questions) and what the install
     * root lacks stay visible after that budget too, within their own. */
    int failed = gh == W32_INVALID_HANDLE_VALUE;
    int disc_event = disc_files() && !write && (marker_of(rel) || (failed && below_install(rel)));
    if (save && !failed)
        fprintf(stderr, "[save] open %s \"%s\" -> %s\n", write ? "W" : "R", rel, host);
    else if (g_log > 0 || (disc_event && g_log_disc > 0)) {
        if (g_log > 0) g_log--; else g_log_disc--;
        fprintf(stderr, "[files] open %s \"%s\" -> %s%s\n", write ? "W" : "R", rel, host, failed ? " (FAILED)" : "");
    }
    wd_devtools_file_open(rel, host, write, !failed);
    if (!write) wd_web_boot_note(rel, !failed);
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
    SDL_PathType type = resolve_read(rel, host, NULL, &error, NULL);
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
/* The next entry to fill, or NULL for a name the guest's buffer cannot hold. */
static FindEntry* find_slot(FindSearch* search, const char* name) {
    Find* find = search->find;
    if (strlen(name) >= W32_MAX_PATH) return NULL;
    if (find->count == search->capacity) {
        search->capacity = search->capacity ? search->capacity * 2 : 16;
        find->entries = (FindEntry*)SDL_realloc(find->entries, (size_t)search->capacity * sizeof(FindEntry));
    }
    return &find->entries[find->count];
}
static void find_add(FindSearch* search, const char* name, const char* path) {
    FindEntry* entry = find_slot(search, name);
    if (!entry || path_type(path, &entry->info) == SDL_PATHTYPE_NONE) return;
    strcpy(entry->name, name);
    search->find->count++;
}
static void find_add_disc(FindSearch* search, const char* name, const DiscEntry* from) {
    FindEntry* entry = find_slot(search, name);
    if (!entry) return;
    SDL_zero(entry->info);
    entry->info.type = from->is_dir ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_FILE;
    entry->info.size = from->size;
    strcpy(entry->name, name);
    search->find->count++;
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
/* Order what a directory gave and put its "." and ".." ahead of the names (the
 * directory is a host path or a disc entry). NULL with the Win32 error when
 * nothing matched. */
static Find* find_finish(FindSearch* search, const char* host, const DiscEntry* on_disc, uint32_t* error) {
    Find* find = search->find;
    SDL_qsort(find->entries, (size_t)find->count, sizeof(FindEntry), find_order);
    if (dos_match(search->expression, "")) {
        int names = find->count;
        for (int k = 0; k < 2; k++) {
            if (host) find_add(search, k ? ".." : ".", host);
            else find_add_disc(search, k ? ".." : ".", on_disc);
        }
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
/* The entries of one root's directory that match, or NULL with the Win32 error. */
static Find* find_in(const char* root, const char* dir, const char* expression, uint32_t* error) {
    char host[HOST_PATH];
    if (locate(root, dir, host, NULL, NULL) != SDL_PATHTYPE_DIRECTORY) { *error = W32_ERROR_PATH_NOT_FOUND; return NULL; }
    Find* find = (Find*)SDL_calloc(1, sizeof *find);
    SDL_SetAtomicInt(&find->object.refs, 1);
    FindSearch search = { find, expression, 0 };
    SDL_EnumerateDirectory(host, find_seen, &search);
    return find_finish(&search, host, NULL, error);
}
/* The same for a directory of a disc. */
static Find* find_on_disc(int disc, const char* dir, const char* expression, uint32_t* error) {
    DiscEntry in, entry;
    if (!disc_find(g_disc[disc], dir, &in) || !in.is_dir) { *error = W32_ERROR_PATH_NOT_FOUND; return NULL; }
    Find* find = (Find*)SDL_calloc(1, sizeof *find);
    SDL_SetAtomicInt(&find->object.refs, 1);
    FindSearch search = { find, expression, 0 };
    for (int i = 0, n = disc_dir_count(g_disc[disc], &in); i < n; i++)
        if (disc_dir_entry(g_disc[disc], &in, i, &entry) && dos_match(expression, entry.name))
            find_add_disc(&search, entry.name, &entry);
    return find_finish(&search, NULL, &in, error);
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
    if (disc_files()) {
        /* Disc mode: the active disc, then for a directory of the install
         * root the same directory at the root of the active and the other disc. */
        int active = SDL_GetAtomicInt(&g_active);
        const char* sub = below_install(dir);
        if (sub && in_saves(sub)) sub = NULL;
        if (!find) find = find_on_disc(active, dir, expression, &error);
        for (int k = 0; sub && !find && k < 2; k++) find = find_on_disc(k ? 3 - active : active, sub, expression, &error);
    }
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

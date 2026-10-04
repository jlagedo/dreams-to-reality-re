/*
 * disc_list - list a disc image: its number, its tracks, and every file with
 * its size and SHA-256.
 *
 *   disc_list [--no-hash] <path>     path: .cue, .iso or a directory
 *   disc_list --find <path in disc> <disc>           look one path up: FILE/DIR, size, SHA-256
 *   disc_list --cat <path in disc> <offset> <count> <disc>   bytes of one file to stdout
 *   disc_list --selftest             check SHA-256 against the standard vectors
 *   disc_list --copy-merged <disc 1> <disc 2> <dest>   both discs as one tree in dest
 *                                    (disc_copy_merged's rules; resumes; prints COPIED, SKIPPED, BYTES)
 *
 * Listing output, tab-separated:
 *   DISC    <1|2|0|-1>               (-1: both marker files present)
 *   SOURCE  <cue|iso|dir>
 *   TRACKS  <count>
 *   TRACK   <NN> <DATA|AUDIO> <sector size> <offset of INDEX 01> <length> <host path>
 *   FILES   <count>
 *   <size> <sha256 or -> <UPPER/CASE/PATH>      one line per file, sorted
 * Exit status: 0 on success, 1 when the disc cannot be read or the path is not
 * on it (message on stderr), 2 on a usage error.
 */
#include "../disc.h"
#include "../sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
#endif

typedef struct { char* path; DiscEntry entry; } Item;
typedef struct { Item* v; size_t n, cap; } Items;

static int add_item(Items* items, const char* path, const DiscEntry* e) {
    if (items->n == items->cap) {
        size_t cap = items->cap ? items->cap * 2 : 1024;
        Item* grown = (Item*)realloc(items->v, cap * sizeof(Item));
        if (!grown) return -1;
        items->v = grown;
        items->cap = cap;
    }
    items->v[items->n].path = (char*)malloc(strlen(path) + 1);
    if (!items->v[items->n].path) return -1;
    strcpy(items->v[items->n].path, path);
    items->v[items->n++].entry = *e;
    return 0;
}

/* Every file below dir, with its upper-case path. */
static int collect(const Disc* d, const DiscEntry* dir, const char* prefix, Items* items) {
    int count = disc_dir_count(d, dir);
    for (int i = 0; i < count; i++) {
        DiscEntry e;
        if (!disc_dir_entry(d, dir, i, &e)) return -1;
        size_t n = strlen(prefix) + strlen(e.name) + 2;
        char* path = (char*)malloc(n);
        if (!path) return -1;
        snprintf(path, n, "%s%s", prefix, e.name);
        for (char* p = path; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
        int rc;
        if (e.is_dir) {
            strcat(path, "/");
            rc = collect(d, &e, path, items);
        } else {
            rc = add_item(items, path, &e);
        }
        free(path);
        if (rc) return rc;
    }
    return 0;
}

static int item_cmp(const void* a, const void* b) { return strcmp(((const Item*)a)->path, ((const Item*)b)->path); }

static int selftest(void) {
    static const struct { const char* text; const char* hex; } vectors[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    };
    int bad = 0;
    char hex[65];
    for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++) {
        disc_sha256_buffer(vectors[i].text, strlen(vectors[i].text), hex);
        int ok = !strcmp(hex, vectors[i].hex);
        printf("%s sha256 \"%.20s\"\n", ok ? "ok  " : "FAIL", vectors[i].text);
        bad += !ok;
    }
    /* One million 'a', fed in uneven pieces to exercise the block buffering. */
    Sha256 s;
    uint8_t digest[32];
    char piece[1000];
    memset(piece, 'a', sizeof piece);
    sha256_init(&s);
    for (size_t left = 1000000, step = 1; left; step += 37) {
        size_t k = step % sizeof piece + 1;
        if (k > left) k = left;
        sha256_update(&s, piece, k);
        left -= k;
    }
    sha256_final(&s, digest);
    sha256_hex(digest, hex);
    int ok = !strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    printf("%s sha256 1000000 x \"a\"\n", ok ? "ok  " : "FAIL");
    bad += !ok;
    return bad ? 1 : 0;
}

static int run(const char* path, int hash) {
    char err[256];
    Disc* d = disc_open(path, err, sizeof err);
    if (!d) {
        fprintf(stderr, "disc_list: %s\n", err);
        return 1;
    }
    static const char* const source[] = {"?", "cue", "iso", "dir"};
    printf("DISC\t%d\nSOURCE\t%s\nTRACKS\t%d\n", disc_number(d), source[disc_source(d)], disc_track_count(d));
    for (int n = 1; n < 100; n++) {
        DiscTrack t;
        if (!disc_track(d, n, &t)) continue;
        printf("TRACK\t%02d\t%s\t%u\t%llu\t%llu\t%s\n", t.number, t.is_audio ? "AUDIO" : "DATA", t.sector_size,
               (unsigned long long)t.offset, (unsigned long long)t.length, t.path);
    }
    Items items = {0};
    DiscEntry root;
    int rc = 0;
    if (!disc_find(d, "", &root) || collect(d, &root, "", &items)) {
        fprintf(stderr, "disc_list: cannot list the disc (out of memory?)\n");
        rc = 1;
    } else {
        if (items.n) qsort(items.v, items.n, sizeof(Item), item_cmp);
        printf("FILES\t%zu\n", items.n);
        for (size_t i = 0; i < items.n; i++) {
            char hex[65] = "-";
            if (hash && disc_sha256_file(d, &items.v[i].entry, hex)) {
                fprintf(stderr, "disc_list: cannot read %s\n", items.v[i].path);
                rc = 1;
                break;
            }
            printf("%llu\t%s\t%s\n", (unsigned long long)items.v[i].entry.size, hex, items.v[i].path);
        }
    }
    for (size_t i = 0; i < items.n; i++) free(items.v[i].path);
    free(items.v);
    disc_close(d);
    return rc;
}

/* --find: look one path up the way the game's file layer will. */
static int find_one(const char* path, const char* guest) {
    char err[256], hex[65];
    Disc* d = disc_open(path, err, sizeof err);
    DiscEntry e;
    if (!d) { fprintf(stderr, "disc_list: %s\n", err); return 1; }
    int rc = 1;
    if (!disc_find(d, guest, &e)) fprintf(stderr, "disc_list: not found: %s\n", guest);
    else if (e.is_dir) { printf("DIR\t%s\t%d\n", e.name, disc_dir_count(d, &e)); rc = 0; }
    else if (disc_sha256_file(d, &e, hex)) fprintf(stderr, "disc_list: cannot read %s\n", guest);
    else { printf("FILE\t%s\t%llu\t%s\n", e.name, (unsigned long long)e.size, hex); rc = 0; }
    disc_close(d);
    return rc;
}

/* --cat: write count bytes at offset of one file to stdout. */
static int cat_one(const char* path, const char* guest, unsigned long long offset, unsigned long long count) {
    char err[256];
    Disc* d = disc_open(path, err, sizeof err);
    DiscEntry e;
    if (!d) { fprintf(stderr, "disc_list: %s\n", err); return 1; }
    DiscFile* f = disc_find(d, guest, &e) ? disc_file_open(d, &e) : NULL;
    int rc = 1;
    if (!f) fprintf(stderr, "disc_list: cannot open %s\n", guest);
    else {
        size_t n = count > (1u << 28) ? (size_t)(1u << 28) : (size_t)count;
        uint8_t* buf = (uint8_t*)malloc(n ? n : 1);
        int64_t got = buf ? disc_file_read(f, offset, buf, n) : -1;
        if (got < 0) fprintf(stderr, "disc_list: read error in %s\n", guest);
        else { fwrite(buf, 1, (size_t)got, stdout); rc = 0; }
        free(buf);
    }
    disc_file_close(f);
    disc_close(d);
    return rc;
}

/* --copy-merged: the developer folder's copy (disc_copy_merged). Progress on
 * stderr every 64 MB; the totals on stdout. */
typedef struct { unsigned long long next; int files, skipped; unsigned long long bytes; } CopyState;
static int copy_progress(void* ctx, const DiscCopyProgress* p) {
    CopyState* s = (CopyState*)ctx;
    if (p->bytes_done >= s->next) {
        fprintf(stderr, "disc_list: %llu of %llu MB\n", (unsigned long long)(p->bytes_done >> 20),
                (unsigned long long)(p->bytes_total >> 20));
        s->next = p->bytes_done + (64ull << 20);
    }
    s->files = p->files_done;
    s->skipped = p->files_skipped;
    s->bytes = p->bytes_done;
    return 0;
}
static int copy_merged(const char* path1, const char* path2, const char* dest) {
    char err[512];
    Disc* d1 = disc_open(path1, err, sizeof err);
    if (!d1) { fprintf(stderr, "disc_list: %s\n", err); return 1; }
    Disc* d2 = disc_open(path2, err, sizeof err);
    if (!d2) { fprintf(stderr, "disc_list: %s\n", err); disc_close(d1); return 1; }
    CopyState s = {0, 0, 0, 0};
    int rc = disc_copy_merged(d1, d2, dest, copy_progress, &s, err, sizeof err);
    if (rc) fprintf(stderr, "disc_list: %s\n", rc < 0 ? err : "cancelled");
    else printf("COPIED\t%d\nSKIPPED\t%d\nBYTES\t%llu\n", s.files - s.skipped, s.skipped, s.bytes);
    disc_close(d2);
    disc_close(d1);
    return rc ? 1 : 0;
}

static int usage(void) {
    fprintf(stderr,
            "usage: disc_list [--no-hash] <disc.cue | disc.iso | directory>\n"
            "       disc_list --find <path in disc> <disc>\n"
            "       disc_list --cat <path in disc> <offset> <count> <disc>\n"
            "       disc_list --selftest\n"
            "       disc_list --copy-merged <disc 1> <disc 2> <dest>\n");
    return 2;
}

static int tool(int argc, char** argv) {
    int hash = 1;
    const char* path = NULL;
    if (argc == 2 && !strcmp(argv[1], "--selftest")) return selftest();
    if (argc == 5 && !strcmp(argv[1], "--copy-merged")) return copy_merged(argv[2], argv[3], argv[4]);
    if (argc == 4 && !strcmp(argv[1], "--find")) return find_one(argv[3], argv[2]);
    if (argc == 6 && !strcmp(argv[1], "--cat"))
        return cat_one(argv[5], argv[2], strtoull(argv[3], NULL, 10), strtoull(argv[4], NULL, 10));
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-hash")) hash = 0;
        else if (argv[i][0] == '-' && argv[i][1] == '-') return usage();
        else if (!path) path = argv[i];
        else return usage();
    }
    return path ? run(path, hash) : usage();
}

#ifdef _WIN32
/* The library takes UTF-8 paths; the C runtime's argv is in the ANSI code page. */
int main(void) {
    int argc;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    char** argv = wide ? (char**)calloc((size_t)argc + 1, sizeof(char*)) : NULL;
    if (!argv) return 2;
    for (int i = 0; i < argc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, NULL, 0, NULL, NULL);
        argv[i] = (char*)malloc((size_t)n);
        if (!argv[i]) return 2;
        WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, argv[i], n, NULL, NULL);
    }
    _setmode(_fileno(stdout), _O_BINARY);           /* LF line ends, bytes as written */
    return tool(argc, argv);
}
#else
int main(int argc, char** argv) { return tool(argc, argv); }
#endif

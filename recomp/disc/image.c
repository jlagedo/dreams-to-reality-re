/*
 * Disc library - the disc: sources, ISO 9660 directory tree, file lookup and
 * reads.
 *
 * A disc is opened once. For a .cue or .iso the whole ISO 9660 directory tree
 * is read into memory at open time (a few hundred KB of records for a game
 * disc) and checked: every extent must lie inside the data track, every record
 * and name inside its directory block, and the tree must be finite. For a
 * directory the tree is listed from the file system. After that the Disc never
 * changes, which is what makes it safe to share between threads.
 *
 * Sector layouts: MODE1/2352 keeps the 2048 user bytes at offset 16 of each
 * 2352-byte sector, MODE1/2048 and .iso are the user bytes only. LBA 0 is the
 * first sector of the data track (its INDEX 01).
 *
 * Names on the disc are ISO 9660 level 1 style: ";1" is dropped and so is a
 * trailing '.'. Lookups ignore case. Joliet and Rock Ridge are not read, nor
 * interleaved or multi-extent files (refused with an error: the game discs
 * have none).
 */
#include "internal.h"
#include "sha256.h"

#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 32                /* directory nesting; ISO 9660 allows 8 */
#define MAX_ENTRIES 200000
#define MAX_DIR_BYTES (32u << 20)
#define MAX_NAME 255
#define READ_CHUNK 16u              /* raw sectors per fread */
#define HASH_CHUNK (64u * 1024)
#define FIRST_VOLUME_SECTOR 16u
#define MAX_DESCRIPTORS 32u
#define PREGAP_FRAMES 150u          /* the standard 2 seconds before INDEX 01 */

typedef struct Entry {
    char* name;                     /* canonical: ";1" and trailing '.' removed */
    char* real;                     /* name on the host file system when it differs (directory source) */
    uint64_t size;
    uint64_t lba;                   /* first sector of the extent (image sources) */
    int parent;                     /* -1 for the root */
    int first, count;               /* children (a directory): contiguous, sorted */
    int depth;
    int is_dir;
} Entry;

struct Disc {
    DiscSource source;
    char* root;                     /* directory source: the directory */
    char* data_path;                /* image sources: file holding the data track */
    uint64_t data_base;             /* byte offset of sector 0 in that file */
    unsigned data_sector;           /* bytes per sector in the file */
    uint64_t data_sectors;          /* sectors in the track */
    Entry* e;
    int n, cap;
    DiscTrack* tracks;
    int ntracks;
};

struct DiscFile {
    FILE* f;
    uint64_t size;
    uint64_t base;                  /* file offset of sector 0 (or of byte 0 for a flat file) */
    uint64_t pos0;                  /* user-data position of byte 0 of the file, counted from sector 0 */
    unsigned sector;                /* SECTOR_RAW or SECTOR_USER; flat files (directory source) use SECTOR_USER */
    uint8_t* scratch;               /* READ_CHUNK raw sectors, only for SECTOR_RAW */
};

static void free_entries(Disc* d) {
    for (int i = 0; i < d->n; i++) { free(d->e[i].name); free(d->e[i].real); }
    free(d->e);
    d->e = NULL;
    d->n = d->cap = 0;
}

void disc_close(Disc* d) {
    if (!d) return;
    free_entries(d);
    for (int i = 0; i < d->ntracks; i++) free((char*)d->tracks[i].path);
    free(d->tracks);
    free(d->root);
    free(d->data_path);
    free(d);
}

DiscSource disc_source(const Disc* d) { return d->source; }

/* Append an entry (taking its strings); returns its index or -1. */
static int add_entry(Disc* d, Entry* e, char* err) {
    if (d->n == MAX_ENTRIES) return err_set(err, "too many files on the disc (more than %d)", MAX_ENTRIES);
    if (d->n == d->cap) {
        int cap = d->cap ? d->cap * 2 : 256;
        Entry* grown = (Entry*)realloc(d->e, (size_t)cap * sizeof(Entry));
        if (!grown) return err_set(err, "out of memory");
        d->e = grown;
        d->cap = cap;
    }
    d->e[d->n] = *e;
    return d->n++;
}

static int entry_cmp(const void* a, const void* b) {
    return ci_compare(((const Entry*)a)->name, ((const Entry*)b)->name);
}

/* "NAME.EXT;1" -> "NAME.EXT", "NAME." -> "NAME". Returns the new length. */
static size_t canonical_length(const char* name, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (name[i] == ';') { n = i; break; }
    while (n && name[n - 1] == '.') n--;
    return n;
}

/* ---- reading user data ---- */

/* Read n user-data bytes starting at user-data position pos (counted from
 * sector 0) of an image whose sector 0 is at byte base of f. */
static int read_user(FILE* f, uint64_t base, unsigned sector, uint64_t pos, uint8_t* out, size_t n, uint8_t* scratch) {
    if (sector == SECTOR_USER) {
        if (plat_seek(f, base + pos)) return -1;
        return fread(out, 1, n, f) == n ? 0 : -1;
    }
    while (n) {
        uint64_t lba = pos / SECTOR_USER;
        size_t inside = (size_t)(pos % SECTOR_USER);
        size_t want = (inside + n + SECTOR_USER - 1) / SECTOR_USER;
        if (want > READ_CHUNK) want = READ_CHUNK;
        if (plat_seek(f, base + lba * SECTOR_RAW) || fread(scratch, SECTOR_RAW, want, f) != want) return -1;
        for (size_t i = 0; i < want && n; i++) {
            size_t take = SECTOR_USER - inside < n ? SECTOR_USER - inside : n;
            memcpy(out, scratch + i * SECTOR_RAW + RAW_USER_OFFSET + inside, take);
            out += take; n -= take; pos += take;
            inside = 0;
        }
    }
    return 0;
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* ---- ISO 9660 ---- */

typedef struct { Disc* d; FILE* f; uint8_t* scratch; } Iso;

static int truncated(char* err, const Disc* d, const char* what) {
    return err_set(err, "image is truncated: %s lies beyond the end of the data track (%llu sectors) in %s", what,
                   (unsigned long long)d->data_sectors, d->data_path);
}

/* Read directory entry idx and append its records as its children. */
static int expand_iso(Iso* iso, int idx, char* err) {
    Disc* d = iso->d;
    Entry dir = d->e[idx];
    if (dir.depth >= MAX_DEPTH) return err_set(err, "directories nested deeper than %d levels", MAX_DEPTH);
    if (!dir.size || dir.size > MAX_DIR_BYTES) return err_set(err, "bad size of directory \"%s\" (%llu bytes)", dir.name, (unsigned long long)dir.size);
    uint64_t sectors = (dir.size + SECTOR_USER - 1) / SECTOR_USER;
    if (dir.lba + sectors > d->data_sectors) return truncated(err, d, "a directory");
    for (int a = dir.parent; a >= 0; a = d->e[a].parent)
        if (d->e[a].lba == dir.lba) return err_set(err, "directory \"%s\" contains itself", dir.name);

    size_t total = (size_t)(sectors * SECTOR_USER);
    uint8_t* buf = (uint8_t*)malloc(total);
    if (!buf) return err_set(err, "out of memory");
    int rc = -1;
    if (read_user(iso->f, d->data_base, d->data_sector, dir.lba * SECTOR_USER, buf, total, iso->scratch)) {
        err_set(err, "cannot read directory \"%s\" from %s", dir.name, d->data_path);
        goto done;
    }
    int first = d->n;
    for (size_t i = 0; i < dir.size;) {
        size_t len = buf[i];
        if (!len) { i = (i / SECTOR_USER + 1) * SECTOR_USER; continue; }   /* padding to the next sector */
        if (len < 34 || i + len > dir.size || i % SECTOR_USER + len > SECTOR_USER) {
            err_set(err, "bad directory record in \"%s\"", dir.name);
            goto done;
        }
        const uint8_t* rec = buf + i;
        size_t nlen = rec[32];
        if (33 + nlen > len) { err_set(err, "bad name length in a record of \"%s\"", dir.name); goto done; }
        const char* name = (const char*)rec + 33;
        i += len;
        if (nlen == 1 && (name[0] == 0 || name[0] == 1)) continue;      /* "." and ".." */
        if (rec[25] & 0x80) { err_set(err, "multi-extent files are not supported (in \"%s\")", dir.name); goto done; }
        if (rec[26] || rec[27]) { err_set(err, "interleaved files are not supported (in \"%s\")", dir.name); goto done; }
        size_t cl = canonical_length(name, nlen);
        if (!cl || cl > MAX_NAME) { err_set(err, "bad file name in \"%s\"", dir.name); goto done; }
        for (size_t k = 0; k < cl; k++)
            if ((unsigned char)name[k] < 0x20 || name[k] == '/' || name[k] == '\\') {
                err_set(err, "bad character in a file name in \"%s\"", dir.name);
                goto done;
            }
        Entry e = {0};
        e.is_dir = (rec[25] & 2) != 0;
        e.size = le32(rec + 10);
        e.lba = (uint64_t)le32(rec + 2) + rec[1];                       /* + extended attribute blocks */
        e.parent = idx;
        e.depth = dir.depth + 1;
        if (!e.is_dir && e.size && e.lba + (e.size + SECTOR_USER - 1) / SECTOR_USER > d->data_sectors) {
            truncated(err, d, "a file");
            goto done;
        }
        e.name = (char*)malloc(cl + 1);
        if (!e.name) { err_set(err, "out of memory"); goto done; }
        memcpy(e.name, name, cl);
        e.name[cl] = 0;
        if (add_entry(d, &e, err) < 0) { free(e.name); goto done; }
    }
    /* d->e may have moved while appending: address the parent by index. */
    d->e[idx].first = first;
    d->e[idx].count = d->n - first;
    qsort(d->e + first, (size_t)(d->n - first), sizeof(Entry), entry_cmp);
    rc = 0;
done:
    free(buf);
    return rc;
}

static int iso_load(Disc* d, char* err) {
    FILE* f = plat_fopen(d->data_path);
    if (!f) return err_set(err, "cannot open %s", d->data_path);
    Iso iso = {d, f, NULL};
    int rc = -1;
    uint8_t vd[SECTOR_USER];
    iso.scratch = d->data_sector == SECTOR_RAW ? (uint8_t*)malloc((size_t)READ_CHUNK * SECTOR_RAW) : NULL;
    if (d->data_sector == SECTOR_RAW && !iso.scratch) { err_set(err, "out of memory"); goto done; }

    /* The volume descriptors start at sector 16; the primary one has type 1. */
    const uint8_t* pvd = NULL;
    for (unsigned i = 0; i < MAX_DESCRIPTORS && !pvd; i++) {
        unsigned sector = FIRST_VOLUME_SECTOR + i;
        if (sector >= d->data_sectors) {
            err_set(err, "not an ISO 9660 image: no volume descriptor in %s (is the file too short?)", d->data_path);
            goto done;
        }
        if (read_user(f, d->data_base, d->data_sector, (uint64_t)sector * SECTOR_USER, vd, sizeof vd, iso.scratch)) {
            err_set(err, "cannot read volume descriptor from %s", d->data_path);
            goto done;
        }
        if (memcmp(vd + 1, "CD001", 5) || vd[6] != 1) {
            err_set(err, "not an ISO 9660 image: no CD001 volume descriptor at sector %u of %s", sector, d->data_path);
            goto done;
        }
        if (vd[0] == 255) break;
        if (vd[0] == 1) pvd = vd;
    }
    if (!pvd) { err_set(err, "not an ISO 9660 image: no primary volume descriptor in %s", d->data_path); goto done; }
    if ((pvd[128] | pvd[129] << 8) != (int)SECTOR_USER) {
        err_set(err, "unsupported ISO 9660 block size in %s (only 2048)", d->data_path);
        goto done;
    }
    uint32_t volume = le32(pvd + 80);
    if (volume > d->data_sectors) {
        err_set(err, "image is truncated: the volume has %u sectors, the data track only %llu (%s)", volume,
                (unsigned long long)d->data_sectors, d->data_path);
        goto done;
    }
    const uint8_t* root = pvd + 156;
    if (root[0] < 34 || !(root[25] & 2)) { err_set(err, "bad root directory record in %s", d->data_path); goto done; }
    Entry r = {0};
    r.name = str_dup("");
    r.is_dir = 1;
    r.lba = le32(root + 2);
    r.size = le32(root + 10);
    r.parent = -1;
    if (!r.name || add_entry(d, &r, err) < 0) { free(r.name); if (!r.name) err_set(err, "out of memory"); goto done; }
    for (int i = 0; i < d->n; i++)                  /* the array grows as directories are read */
        if (d->e[i].is_dir && expand_iso(&iso, i, err)) goto done;
    rc = 0;
done:
    free(iso.scratch);
    fclose(f);
    return rc;
}

/* ---- directory source ---- */

typedef struct {
    Disc* d;
    int parent;
    char* err;
    int failed;
} Scan;

static int scan_cb(void* ctx, const char* name, uint64_t size, int is_dir) {
    Scan* s = (Scan*)ctx;
    Entry e = {0};
    size_t n = strlen(name), cl = canonical_length(name, n);
    if (!cl || cl > MAX_NAME) return 0;             /* not a name the game could ask for */
    e.name = (char*)malloc(cl + 1);
    if (!e.name) { s->failed = 1; return err_set(s->err, "out of memory"); }
    memcpy(e.name, name, cl);
    e.name[cl] = 0;
    if (cl != n) e.real = str_dup(name);
    e.size = size;
    e.is_dir = is_dir;
    e.parent = s->parent;
    e.depth = s->d->e[s->parent].depth + 1;
    if (add_entry(s->d, &e, s->err) < 0) { free(e.name); free(e.real); s->failed = 1; return -1; }
    return 0;
}

/* The path of an entry below the root of a directory source. */
static char* host_path(const Disc* d, int idx) {
    int chain[MAX_DEPTH + 2], k = 0;
    for (int i = idx; i > 0 && k < MAX_DEPTH + 2; i = d->e[i].parent) chain[k++] = i;
    char* path = str_dup(d->root);
    while (path && k--) {
        const Entry* e = &d->e[chain[k]];
        char* next = path_join(path, e->real ? e->real : e->name);
        free(path);
        path = next;
    }
    return path;
}

static int expand_host(Disc* d, int idx, char* err) {
    if (d->e[idx].depth >= MAX_DEPTH) return err_set(err, "directories nested deeper than %d levels in %s", MAX_DEPTH, d->root);
    char* path = host_path(d, idx);
    if (!path) return err_set(err, "out of memory");
    int first = d->n;
    Scan s = {d, idx, err, 0};
    int rc = plat_list_dir(path, scan_cb, &s);
    if (rc && !s.failed) err_set(err, "cannot list directory %s", path);
    free(path);
    if (rc) return -1;
    d->e[idx].first = first;
    d->e[idx].count = d->n - first;
    qsort(d->e + first, (size_t)(d->n - first), sizeof(Entry), entry_cmp);
    return 0;
}

typedef struct {
    struct { int number; char* path; uint64_t size; } track[MAX_TRACKS + 1];
    int found;
    char* cues[16];
    int ncues;
    const char* dir;
} Siblings;

/* "... (Track 05).bin" -> 5, anything else 0. */
static int track_number(const char* name) {
    static const char key[] = "(track ";
    for (const char* p = name; *p; p++) {
        size_t k = 0;
        while (key[k] && (p[k] | 32) == key[k]) k++;
        if (key[k]) continue;
        const char* q = p + k;
        int n = 0, digits = 0;
        for (; *q >= '0' && *q <= '9' && digits < 3; q++, digits++) n = n * 10 + (*q - '0');
        if (digits && *q == ')' && !ci_compare(q + 1, ".bin") && n >= 1 && n <= MAX_TRACKS) return n;
    }
    return 0;
}

static int sibling_cb(void* ctx, const char* name, uint64_t size, int is_dir) {
    Siblings* s = (Siblings*)ctx;
    size_t n = strlen(name);
    if (is_dir) return 0;
    int number = track_number(name);
    if (number && !s->track[number].path) {
        s->track[number].number = number;
        s->track[number].path = path_join(s->dir, name);
        s->track[number].size = size;
        s->found++;
    } else if (n > 4 && !ci_compare(name + n - 4, ".cue") && s->ncues < 16) {
        s->cues[s->ncues++] = path_join(s->dir, name);
    }
    return 0;
}

/* The directory source has no cue sheet of its own; the audio is in
 * `*(Track NN).bin` files beside the directory, as the host's CD scan has
 * always found it. */
static int load_sibling_tracks(Disc* d) {
    Siblings* s = (Siblings*)calloc(1, sizeof *s);
    char* dir = path_dirname(d->root);
    if (!s || !dir) { free(s); free(dir); return -1; }
    s->dir = dir;
    plat_list_dir(dir, sibling_cb, s);              /* an unreadable parent just means no music */
    char scratch[MSG_LEN];

    /* A cue sheet there that names exactly these files gives the real INDEX 01. */
    for (int i = 0; i < s->ncues && !d->ntracks; i++) {
        CueTable t;
        if (cue_parse(s->cues[i], &t, scratch)) continue;
        int same = t.ntracks == s->found;
        for (int k = 0; same && k < t.ntracks; k++) {
            int number = t.tracks[k].number;
            same = s->track[number].path && !ci_compare(s->track[number].path, t.tracks[k].path);
        }
        if (same) { d->tracks = t.tracks; d->ntracks = t.ntracks; }
        else cue_free(&t);
    }
    if (!d->ntracks && s->found) {
        d->tracks = (DiscTrack*)calloc((size_t)s->found, sizeof(DiscTrack));
        for (int number = 1; d->tracks && number <= MAX_TRACKS; number++) {
            if (!s->track[number].path) continue;
            DiscTrack* t = &d->tracks[d->ntracks++];
            uint64_t skip = number == 1 || s->track[number].size < PREGAP_FRAMES * (uint64_t)SECTOR_RAW ? 0 : PREGAP_FRAMES * (uint64_t)SECTOR_RAW;
            t->number = number;
            t->is_audio = number != 1;
            t->path = s->track[number].path;
            s->track[number].path = NULL;           /* now owned by the track */
            t->offset = skip;
            t->length = s->track[number].size - skip;
            t->sector_size = SECTOR_RAW;
        }
    }
    for (int i = 0; i <= MAX_TRACKS; i++) free(s->track[i].path);
    for (int i = 0; i < s->ncues; i++) free(s->cues[i]);
    free(dir);
    free(s);
    return 0;
}

/* ---- open ---- */

static int has_suffix(const char* path, const char* suffix) {
    size_t n = strlen(path), k = strlen(suffix);
    return n >= k && !ci_compare(path + n - k, suffix);
}

static Disc* open_cue(const char* path, char* err) {
    Disc* d = (Disc*)calloc(1, sizeof *d);
    if (!d) { err_set(err, "out of memory"); return NULL; }
    d->source = DISC_FROM_CUE;
    CueTable t;
    if (cue_parse(path, &t, err)) { free(d); return NULL; }
    d->tracks = t.tracks;
    d->ntracks = t.ntracks;
    const DiscTrack* data = NULL;
    for (int i = 0; i < t.ntracks && !data; i++)
        if (!t.tracks[i].is_audio) data = &t.tracks[i];
    if (!data) { err_set(err, "the cue sheet has no data track: %s", path); disc_close(d); return NULL; }
    d->data_path = str_dup(data->path);
    d->data_base = data->offset;
    d->data_sector = data->sector_size;
    d->data_sectors = data->length / data->sector_size;
    if (!d->data_path || iso_load(d, err)) { if (!d->data_path) err_set(err, "out of memory"); disc_close(d); return NULL; }
    return d;
}

static Disc* open_iso(const char* path, uint64_t size, char* err) {
    Disc* d = (Disc*)calloc(1, sizeof *d);
    if (!d) { err_set(err, "out of memory"); return NULL; }
    d->source = DISC_FROM_ISO;
    d->data_path = str_dup(path);
    d->data_sector = SECTOR_USER;
    d->data_sectors = size / SECTOR_USER;
    d->tracks = (DiscTrack*)calloc(1, sizeof(DiscTrack));
    if (!d->data_path || !d->tracks) { err_set(err, "out of memory"); disc_close(d); return NULL; }
    d->tracks[0].number = 1;
    d->tracks[0].path = str_dup(path);
    d->tracks[0].length = size;
    d->tracks[0].sector_size = SECTOR_USER;
    d->ntracks = 1;
    if (iso_load(d, err)) { disc_close(d); return NULL; }
    return d;
}

static Disc* open_dir(const char* path, char* err) {
    Disc* d = (Disc*)calloc(1, sizeof *d);
    if (!d) { err_set(err, "out of memory"); return NULL; }
    d->source = DISC_FROM_DIR;
    d->root = str_dup(path);
    Entry r = {0};
    r.name = str_dup("");
    r.is_dir = 1;
    r.parent = -1;
    if (!d->root || !r.name || add_entry(d, &r, err) < 0) {
        free(r.name);
        err_set(err, "out of memory");
        disc_close(d);
        return NULL;
    }
    for (int i = 0; i < d->n; i++)
        if (d->e[i].is_dir && expand_host(d, i, err)) { disc_close(d); return NULL; }
    load_sibling_tracks(d);
    return d;
}

Disc* disc_open(const char* path, char* err, size_t err_size) {
    char msg[MSG_LEN] = "";
    Disc* d = NULL;
    uint64_t size = 0;
    if (!path || !*path) err_set(msg, "no disc path given");
    else if (plat_is_dir(path)) d = open_dir(path, msg);
    else if (plat_file_size(path, &size)) err_set(msg, "no such file or directory: %s", path);
    else if (has_suffix(path, ".cue")) d = open_cue(path, msg);
    else if (has_suffix(path, ".iso")) d = open_iso(path, size, msg);
    else if (has_suffix(path, ".bin")) err_set(msg, "a bare .bin cannot be read: open the .cue sheet that goes with it (%s)", path);
    else if (has_suffix(path, ".zip") || has_suffix(path, ".7z") || has_suffix(path, ".rar") || has_suffix(path, ".chd") ||
             has_suffix(path, ".mdf") || has_suffix(path, ".mds") || has_suffix(path, ".nrg") || has_suffix(path, ".ccd") ||
             has_suffix(path, ".img") || has_suffix(path, ".cdi"))
        err_set(msg, "unsupported disc image format: unpack or convert it to .cue/.bin or .iso (%s)", path);
    else err_set(msg, "unsupported disc image type (expected a .cue, a .iso or a directory): %s", path);
    if (!d && err && err_size) {
        strncpy(err, msg, err_size - 1);
        err[err_size - 1] = 0;
    }
    return d;
}

/* ---- lookup ---- */

static int child_index(const Disc* d, int dir, const char* name) {
    const Entry* base = &d->e[dir];
    if (!base->is_dir || base->count <= 0) return -1;
    int lo = base->first, hi = base->first + base->count - 1;
    while (lo <= hi) {                              /* children are sorted by entry_cmp */
        int mid = lo + (hi - lo) / 2;
        int c = ci_compare(name, d->e[mid].name);
        if (!c) return mid;
        if (c < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return -1;
}

static int find_id(const Disc* d, const char* path) {
    int stack[MAX_DEPTH + 2], sp = 0, cur = 0;
    const char* p = path;
    while (*p) {
        while (*p == '/' || *p == '\\') p++;
        const char* start = p;
        while (*p && *p != '/' && *p != '\\') p++;
        size_t n = (size_t)(p - start);
        if (!n || (n == 1 && start[0] == '.')) continue;
        if (n == 2 && start[0] == '.' && start[1] == '.') {
            if (!sp) return -1;
            cur = stack[--sp];
            continue;
        }
        char comp[MAX_NAME + 1];
        if (n > MAX_NAME) return -1;
        memcpy(comp, start, n);
        comp[canonical_length(comp, n)] = 0;
        if (!comp[0] || sp >= MAX_DEPTH + 1) return -1;
        int next = child_index(d, cur, comp);
        if (next < 0) return -1;
        stack[sp++] = cur;
        cur = next;
    }
    return cur;
}

static void fill(const Disc* d, int id, DiscEntry* out) {
    const Entry* e = &d->e[id];
    out->name = e->name;
    out->size = e->is_dir ? 0 : e->size;
    out->is_dir = e->is_dir;
    out->id = id;
}

int disc_find(const Disc* d, const char* path, DiscEntry* out) {
    int id = path ? find_id(d, path) : -1;
    if (id < 0) return 0;
    if (out) fill(d, id, out);
    return 1;
}

static int valid(const Disc* d, const DiscEntry* e) { return e && e->id >= 0 && e->id < d->n; }

int disc_dir_count(const Disc* d, const DiscEntry* dir) {
    return valid(d, dir) && d->e[dir->id].is_dir ? d->e[dir->id].count : -1;
}

int disc_dir_entry(const Disc* d, const DiscEntry* dir, int index, DiscEntry* out) {
    int count = disc_dir_count(d, dir);
    if (count < 0 || index < 0 || index >= count || !out) return 0;
    fill(d, d->e[dir->id].first + index, out);
    return 1;
}

int disc_number(const Disc* d) {
    int one = disc_find(d, "DATA\\1CD.ID", NULL), two = disc_find(d, "DATA\\2CD.ID", NULL);
    return one && two ? DISC_NUMBER_AMBIGUOUS : one ? 1 : two ? 2 : 0;
}

/* ---- files ---- */

DiscFile* disc_file_open(const Disc* d, const DiscEntry* entry) {
    if (!valid(d, entry) || d->e[entry->id].is_dir) return NULL;
    const Entry* e = &d->e[entry->id];
    DiscFile* f = (DiscFile*)calloc(1, sizeof *f);
    if (!f) return NULL;
    f->size = e->size;
    if (d->source == DISC_FROM_DIR) {
        char* path = host_path(d, entry->id);
        f->f = path ? plat_fopen(path) : NULL;
        free(path);
        f->sector = SECTOR_USER;                    /* a plain file: bytes are bytes */
    } else {
        f->f = plat_fopen(d->data_path);
        f->base = d->data_base;
        f->pos0 = e->lba * SECTOR_USER;
        f->sector = d->data_sector;
        if (f->sector == SECTOR_RAW) {
            f->scratch = (uint8_t*)malloc((size_t)READ_CHUNK * SECTOR_RAW);
            if (!f->scratch) { disc_file_close(f); return NULL; }
        }
    }
    if (!f->f) { disc_file_close(f); return NULL; }
    return f;
}

void disc_file_close(DiscFile* f) {
    if (!f) return;
    if (f->f) fclose(f->f);
    free(f->scratch);
    free(f);
}

int64_t disc_file_read(DiscFile* f, uint64_t offset, void* buf, size_t n) {
    if (!f || (!buf && n)) return -1;
    if (offset >= f->size) return 0;
    if (n > f->size - offset) n = (size_t)(f->size - offset);
    if (!n) return 0;
    return read_user(f->f, f->base, f->sector, f->pos0 + offset, (uint8_t*)buf, n, f->scratch) ? -1 : (int64_t)n;
}

int disc_sha256_file(const Disc* d, const DiscEntry* entry, char hex[65]) {
    DiscFile* f = disc_file_open(d, entry);
    if (!f) return -1;
    uint8_t* chunk = (uint8_t*)malloc(HASH_CHUNK);
    Sha256 s;
    sha256_init(&s);
    int rc = chunk ? 0 : -1;
    for (uint64_t at = 0; !rc && at < f->size;) {
        int64_t got = disc_file_read(f, at, chunk, HASH_CHUNK);
        if (got <= 0) rc = -1;
        else { sha256_update(&s, chunk, (size_t)got); at += (uint64_t)got; }
    }
    if (!rc) {
        uint8_t digest[32];
        sha256_final(&s, digest);
        sha256_hex(digest, hex);
    }
    free(chunk);
    disc_file_close(f);
    return rc;
}

/* ---- tracks ---- */

int disc_track_count(const Disc* d) { return d->ntracks; }

int disc_track(const Disc* d, int number, DiscTrack* out) {
    for (int i = 0; i < d->ntracks; i++)
        if (d->tracks[i].number == number) {
            if (out) *out = d->tracks[i];
            return 1;
        }
    return 0;
}

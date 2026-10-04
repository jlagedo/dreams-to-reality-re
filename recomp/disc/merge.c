/*
 * Disc library - the two discs as one tree (disc_copy_merged, disc.h).
 *
 * The developers' game read one data tree on a hard disk; the Develop launch
 * mode plays from such a tree, made once from the player's two discs
 * (docs/specs/008-editor-restoration/spec.md, phase M; the merge rule and its
 * evidence are in docs/research/disc-layout.md).
 */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char* rel;          /* '/'-separated, the spelling it gets in dest */
    const Disc* disc;
    DiscEntry entry;
    int skip;           /* already in place at its full size */
} Planned;

typedef struct {
    Planned* v;
    size_t n, cap;
} Plan;

static int plan_add(Plan* p, const char* rel, const Disc* disc, const DiscEntry* e) {
    if (p->n == p->cap) {
        size_t cap = p->cap ? p->cap * 2 : 512;
        Planned* grown = (Planned*)realloc(p->v, cap * sizeof *grown);
        if (!grown) return -1;
        p->v = grown;
        p->cap = cap;
    }
    p->v[p->n].rel = str_dup(rel);
    if (!p->v[p->n].rel) return -1;
    p->v[p->n].disc = disc;
    p->v[p->n].entry = *e;
    p->v[p->n].skip = 0;
    p->n++;
    return 0;
}

static void plan_free(Plan* p) {
    for (size_t i = 0; i < p->n; i++) free(p->v[i].rel);
    free(p->v);
}

/* rel starts with the directory prefix (case-insensitive, then '/' or end). */
static int under(const char* rel, const char* dir) {
    size_t n = strlen(dir);
    char head[64];
    if (strlen(rel) < n || n >= sizeof head) return 0;
    memcpy(head, rel, n);
    head[n] = 0;
    return !ci_compare(head, dir) && (rel[n] == '/' || rel[n] == 0);
}

static const char kUvconfig[] = "DATA/UNIVBE/UVCONFIG.EXE";   /* the one file disc 2 has newer */

/* disc 2's path with every directory disc 1 also has spelt as on disc 1. */
static char* canonical(const Disc* disc1, const char* rel) {
    size_t n = strlen(rel);
    char* out = (char*)malloc(n + 1);
    char* probe = (char*)malloc(n + 1);
    if (!out || !probe) { free(out); free(probe); return NULL; }
    size_t used = 0;
    int mapping = 1;
    const char* seg = rel;
    while (*seg) {
        const char* end = strchr(seg, '/');
        size_t len = end ? (size_t)(end - seg) : strlen(seg);
        if (used) out[used++] = '/';
        memcpy(out + used, seg, len);
        out[used + len] = 0;
        if (mapping && end) {   /* a directory: take disc 1's name for it when disc 1 has it */
            DiscEntry e;
            memcpy(probe, out, used + len + 1);
            if (disc_find(disc1, probe, &e) && e.is_dir && strlen(e.name) == len) memcpy(out + used, e.name, len);
            else mapping = 0;
        }
        used += len;
        if (!end) break;
        seg = end + 1;
    }
    out[used] = 0;
    free(probe);
    return out;
}

typedef struct {
    const Disc* disc1;
    const Disc* disc2;
    Plan* plan;
    int which;          /* 1 or 2: the disc being walked */
    char* err;
} Walk;

static int walk(Walk* w, const DiscEntry* dir, const char* prefix) {
    const Disc* d = w->which == 1 ? w->disc1 : w->disc2;
    int count = disc_dir_count(d, dir);
    if (count < 0) return err_set(w->err, "cannot list %s on disc %d", prefix[0] ? prefix : "the root", w->which);
    for (int i = 0; i < count; i++) {
        DiscEntry e;
        if (!disc_dir_entry(d, dir, i, &e)) return err_set(w->err, "cannot read %s on disc %d", prefix, w->which);
        size_t n = strlen(prefix) + strlen(e.name) + 2;
        char* rel = (char*)malloc(n);
        if (!rel) return err_set(w->err, "out of memory");
        snprintf(rel, n, "%s%s%s", prefix, prefix[0] ? "/" : "", e.name);
        int rc = 0;
        if (e.is_dir) {
            if (!(w->which == 2 && under(rel, "DATA/GAME"))) rc = walk(w, &e, rel);
        } else if (w->which == 1) {
            DiscEntry other;
            int newer_on_2 = !ci_compare(rel, kUvconfig) && disc_find(w->disc2, rel, &other);
            if (ci_compare(rel, "DATA/FULL.ID") && !newer_on_2) rc = plan_add(w->plan, rel, d, &e);
        } else {
            DiscEntry other;
            if (!disc_find(w->disc1, rel, &other) || !ci_compare(rel, kUvconfig)) {
                char* spelt = canonical(w->disc1, rel);
                rc = spelt ? plan_add(w->plan, spelt, d, &e) : -1;
                free(spelt);
            }
        }
        free(rel);
        if (rc) return rc < 0 && !w->err[0] ? err_set(w->err, "out of memory") : rc;
    }
    return 0;
}

/* Create dest and its parents (each separator in turn). */
static int make_path(const char* path) {
    size_t n = strlen(path);
    char* p = (char*)malloc(n + 1);
    if (!p) return -1;
    memcpy(p, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (p[i] != '/' && p[i] != '\\') continue;
        if (p[i - 1] == ':') continue;   /* a drive root */
        char c = p[i];
        p[i] = 0;
        plat_mkdir(p);
        p[i] = c;
    }
    int rc = plat_mkdir(p);
    free(p);
    return rc;
}

/* The directories of rel below dest. */
static int make_parents(const char* dest, const char* rel) {
    char* full = path_join(dest, rel);
    char* dir = full ? path_dirname(full) : NULL;
    int rc = dir ? make_path(dir) : -1;
    free(full);
    free(dir);
    return rc;
}

#define BLOCK (1u << 20)

static int copy_one(const Planned* f, const char* dest, DiscCopyProgress* pr, DiscCopyFn cb, void* ctx, char* err) {
    char* out = path_join(dest, f->rel);
    size_t n = out ? strlen(out) + 6 : 0;
    char* part = out ? (char*)malloc(n) : NULL;
    void* block = malloc(BLOCK);
    DiscFile* in = disc_file_open(f->disc, &f->entry);
    FILE* o = NULL;
    int rc = 0;
    if (!out || !part || !block) { rc = err_set(err, "out of memory"); goto done; }
    snprintf(part, n, "%s.part", out);
    if (make_parents(dest, f->rel)) { rc = err_set(err, "cannot create the directory of %s", out); goto done; }
    if (!in) { rc = err_set(err, "cannot open %s on the disc", f->rel); goto done; }
    o = plat_fopen_write(part);
    if (!o) { rc = err_set(err, "cannot write %s", part); goto done; }
    for (uint64_t at = 0; at < f->entry.size;) {
        size_t want = f->entry.size - at < BLOCK ? (size_t)(f->entry.size - at) : BLOCK;
        int64_t got = disc_file_read(in, at, block, want);
        if (got <= 0) { rc = err_set(err, "cannot read %s on the disc", f->rel); goto done; }
        if (fwrite(block, 1, (size_t)got, o) != (size_t)got) { rc = err_set(err, "cannot write %s (disk full?)", part); goto done; }
        at += (uint64_t)got;
        pr->bytes_done += (uint64_t)got;
        if (cb && cb(ctx, pr)) { rc = 1; goto done; }
    }
    if (fclose(o)) { o = NULL; rc = err_set(err, "cannot write %s (disk full?)", part); goto done; }
    o = NULL;
    if (plat_replace(part, out)) rc = err_set(err, "cannot rename %s", part);
done:
    if (o) fclose(o);
    if (rc && part) plat_remove(part);
    disc_file_close(in);
    free(block);
    free(part);
    free(out);
    return rc;
}

int disc_copy_merged(const Disc* disc1, const Disc* disc2, const char* dest, DiscCopyFn cb, void* ctx, char* err,
                     size_t err_size) {
    char msg[MSG_LEN] = "";
    Plan plan = {0};
    DiscEntry root1, root2;
    int rc = 0;
    if (!disc1 || !disc2 || !dest || !dest[0]) {
        rc = err_set(msg, "disc_copy_merged needs both discs and a destination");
        goto done;
    }
    if (!disc_find(disc1, "", &root1) || !disc_find(disc2, "", &root2)) {
        rc = err_set(msg, "cannot read a disc's root directory");
        goto done;
    }
    Walk w = {disc1, disc2, &plan, 1, msg};
    if ((rc = walk(&w, &root1, ""))) goto done;
    w.which = 2;
    if ((rc = walk(&w, &root2, ""))) goto done;
    if (make_path(dest)) {
        rc = err_set(msg, "cannot create %s", dest);
        goto done;
    }

    DiscCopyProgress pr = {0};
    pr.files_total = (int)plan.n;
    for (size_t i = 0; i < plan.n; i++) {
        char* out = path_join(dest, plan.v[i].rel);
        uint64_t have = 0;
        plan.v[i].skip = out && !plat_file_size(out, &have) && have == plan.v[i].entry.size;
        if (!plan.v[i].skip) pr.bytes_total += plan.v[i].entry.size;
        free(out);
    }
    for (size_t i = 0; i < plan.n; i++) {
        pr.path = plan.v[i].rel;
        if (cb && cb(ctx, &pr)) { rc = 1; goto done; }
        if (plan.v[i].skip) pr.files_skipped++;
        else if ((rc = copy_one(&plan.v[i], dest, &pr, cb, ctx, msg))) goto done;
        pr.files_done++;
    }
    pr.path = "";
    if (cb) cb(ctx, &pr);
done:
    plan_free(&plan);
    if (rc < 0 && err && err_size) {
        strncpy(err, msg, err_size - 1);
        err[err_size - 1] = 0;
    }
    return rc;
}

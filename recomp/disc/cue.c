/*
 * Disc library - cue sheet parser.
 *
 * Reads FILE, TRACK and INDEX (every other line is ignored: REM, TITLE,
 * CATALOG, FLAGS, ISRC, PREGAP, POSTGAP...) and produces the track table: for
 * each track the file, the byte offset of INDEX 01 in it and the length of the
 * track from INDEX 01 to its end.
 *
 * A track ends where the next track of the same file begins (that track's
 * INDEX 00 when it has one, so a pregap stored in the file is not counted into
 * the previous track), else at the end of the file. PREGAP names silence the
 * file does not hold, so it moves nothing; INDEX 00 is data in the file, which
 * playback skips by starting at INDEX 01.
 *
 * Positions are mm:ss:ff frames of 2352 bytes (2048 for a MODE1/2048 track).
 * Only BINARY files are read.
 */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define MAX_CUE_BYTES (1u << 20)
#define MAX_CUE_FILES 99

typedef struct {
    int number, audio;
    unsigned sector_size;
    int file;                       /* index in files, -1 before any FILE */
    int64_t index0, index1;         /* frames, -1 when absent */
} Raw;

typedef struct { char* path; uint64_t size; } CueFile;

void cue_free(CueTable* t) {
    for (int i = 0; i < t->ntracks; i++) free((char*)t->tracks[i].path);
    free(t->tracks);
    t->tracks = NULL;
    t->ntracks = 0;
}

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

/* Next word of a line: a quoted string or a run of non-blanks. */
static char* next_word(char** p) {
    char* s = *p;
    while (is_space(*s)) s++;
    if (!*s) { *p = s; return NULL; }
    char* word;
    if (*s == '"') {
        word = ++s;
        while (*s && *s != '"') s++;
    } else {
        word = s;
        while (*s && !is_space(*s)) s++;
    }
    if (*s) *s++ = 0;
    *p = s;
    return word;
}

static int msf(const char* s, int64_t* frames) {
    unsigned m, sec, f;
    char tail;
    if (sscanf(s, "%u:%u:%u%c", &m, &sec, &f, &tail) != 3 || sec >= 60 || f >= FRAMES_PER_SECOND || m > 999)
        return -1;
    *frames = ((int64_t)m * 60 + sec) * FRAMES_PER_SECOND + f;
    return 0;
}

static char* read_text(const char* path, char* err) {
    uint64_t size;
    if (plat_file_size(path, &size)) { err_set(err, "cue sheet not found: %s", path); return NULL; }
    if (size > MAX_CUE_BYTES) { err_set(err, "cue sheet is too large (%llu bytes): %s", (unsigned long long)size, path); return NULL; }
    FILE* f = plat_fopen(path);
    char* text = (char*)malloc((size_t)size + 1);
    if (!f || !text) {
        err_set(err, "cannot read cue sheet: %s", path);
        free(text);
        if (f) fclose(f);
        return NULL;
    }
    size_t got = fread(text, 1, (size_t)size, f);
    fclose(f);
    text[got] = 0;
    return text;
}

int cue_parse(const char* cue_path, CueTable* out, char* err) {
    out->tracks = NULL;
    out->ntracks = 0;
    char* text = read_text(cue_path, err);
    if (!text) return -1;
    char* dir = path_dirname(cue_path);
    CueFile files[MAX_CUE_FILES];
    int nfiles = 0, ntracks = 0, rc = -1;
    Raw raw[MAX_TRACKS];
    int line_no = 0;
    char* p = text;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;

    while (*p) {
        char* line = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        line_no++;
        char* word = next_word(&line);
        if (!word) continue;
        if (!ci_compare(word, "FILE")) {
            char* name = next_word(&line);
            char* kind = next_word(&line);
            if (!name || !kind) { err_set(err, "cue line %d: FILE needs a name and a type", line_no); goto done; }
            if (ci_compare(kind, "BINARY")) {
                err_set(err, "cue line %d: unsupported FILE type %s (only BINARY images can be read; convert audio files to raw)", line_no, kind);
                goto done;
            }
            if (nfiles == MAX_CUE_FILES) { err_set(err, "cue sheet names too many files"); goto done; }
            char* path = dir ? path_join(dir, name) : NULL;
            if (!path) { err_set(err, "out of memory"); goto done; }
            files[nfiles].path = path;
            if (plat_file_size(path, &files[nfiles].size)) {
                nfiles++;
                err_set(err, "cue names a missing file: %s", path);
                goto done;
            }
            nfiles++;
        } else if (!ci_compare(word, "TRACK")) {
            char* num = next_word(&line);
            char* mode = next_word(&line);
            int n = num ? atoi(num) : 0;
            if (!mode || n < 1 || n > 99) { err_set(err, "cue line %d: bad TRACK", line_no); goto done; }
            if (!nfiles) { err_set(err, "cue line %d: TRACK before any FILE", line_no); goto done; }
            if (ntracks == MAX_TRACKS || (ntracks && n <= raw[ntracks - 1].number)) {
                err_set(err, "cue line %d: track numbers must increase", line_no);
                goto done;
            }
            Raw* t = &raw[ntracks++];
            t->number = n;
            t->file = nfiles - 1;
            t->index0 = t->index1 = -1;
            t->audio = !ci_compare(mode, "AUDIO");
            if (t->audio || !ci_compare(mode, "MODE1/2352")) t->sector_size = SECTOR_RAW;
            else if (!ci_compare(mode, "MODE1/2048")) t->sector_size = SECTOR_USER;
            else {
                err_set(err, "cue line %d: unsupported track mode %s (MODE1/2352, MODE1/2048 and AUDIO are read)", line_no, mode);
                goto done;
            }
        } else if (!ci_compare(word, "INDEX")) {
            char* num = next_word(&line);
            char* pos = next_word(&line);
            int64_t frames;
            if (!ntracks) { err_set(err, "cue line %d: INDEX before any TRACK", line_no); goto done; }
            if (!num || !pos || msf(pos, &frames)) { err_set(err, "cue line %d: bad INDEX", line_no); goto done; }
            int which = atoi(num);
            Raw* t = &raw[ntracks - 1];
            if (which == 0 || which == 1) {
                int64_t* slot = which ? &t->index1 : &t->index0;
                if (*slot >= 0) { err_set(err, "cue line %d: INDEX %02d given twice", line_no, which); goto done; }
                *slot = frames;
            }
        }
    }
    if (!ntracks) { err_set(err, "bad cue sheet: no TRACK found"); goto done; }

    out->tracks = (DiscTrack*)calloc((size_t)ntracks, sizeof(DiscTrack));
    if (!out->tracks) { err_set(err, "out of memory"); goto done; }
    for (int i = 0; i < ntracks; i++) {
        const Raw* t = &raw[i];
        const CueFile* f = &files[t->file];
        DiscTrack* d = &out->tracks[i];
        if (t->index1 < 0) { err_set(err, "cue: track %02d has no INDEX 01", t->number); goto done; }
        uint64_t start = (uint64_t)t->index1 * t->sector_size, end = f->size;
        if (start > f->size) {
            err_set(err, "cue: track %02d starts past the end of %s (file truncated?)", t->number, f->path);
            goto done;
        }
        if (i + 1 < ntracks && raw[i + 1].file == t->file) {
            const Raw* next = &raw[i + 1];
            int64_t at = next->index0 >= 0 ? next->index0 : next->index1;
            end = at >= 0 ? (uint64_t)at * next->sector_size : f->size;
            if (end > f->size || end < start) {
                err_set(err, "cue: track %02d ends outside %s (indexes out of order or file truncated)", t->number, f->path);
                goto done;
            }
        }
        d->path = str_dup(f->path);
        if (!d->path) { err_set(err, "out of memory"); goto done; }
        d->number = t->number;
        d->is_audio = t->audio;
        d->sector_size = t->sector_size;
        d->offset = start;
        d->length = end - start;
        out->ntracks = i + 1;       /* what cue_free must release */
    }
    rc = 0;
done:
    if (rc) cue_free(out);
    for (int i = 0; i < nfiles; i++) free(files[i].path);
    free(dir);
    free(text);
    return rc;
}

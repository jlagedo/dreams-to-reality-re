/*
 * Disc library - read a game disc without a drive.
 *
 * Opens a disc from a .cue (one .bin per track, or one .bin for all of them),
 * a bare .iso, or an already extracted directory, and gives read access to its
 * files and audio tracks. Plain C99 and libc; no dependency on the host
 * runtime or on SDL.
 *
 * Paths given to and returned by this library are UTF-8 (on Windows they are
 * converted for the wide-character file APIs; a path that is not valid UTF-8
 * is taken in the ANSI code page).
 *
 * Threads: a Disc is read-only after disc_open, so any number of threads may
 * use it at once (disc_find, disc_dir_*, disc_track, disc_file_open). Every
 * DiscFile owns its own FILE*; one DiscFile must not be used by two threads at
 * once, but different DiscFiles of one Disc may be.
 *
 * All image data is treated as untrusted: extents, record lengths, name
 * lengths and the directory depth are checked, and a damaged or truncated
 * image is refused with an error string, never read out of bounds.
 */
#ifndef DISC_H
#define DISC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Disc Disc;           /* an open disc (opaque) */
typedef struct DiscFile DiscFile;   /* an open file of a disc (opaque) */

typedef enum {
    DISC_FROM_CUE = 1,              /* .cue and its .bin files */
    DISC_FROM_ISO = 2,              /* bare .iso: 2048-byte sectors, no audio */
    DISC_FROM_DIR = 3               /* extracted directory tree */
} DiscSource;

/* A file or directory of the disc. name points into the Disc and stays valid
 * until disc_close. id is private to the library. */
typedef struct DiscEntry {
    const char* name;               /* last path component as on the disc; ";1" and a trailing '.' removed; "" for the root */
    uint64_t size;                  /* bytes; 0 for a directory */
    int is_dir;
    int id;
} DiscEntry;

/* One track as the CD lists it. Track 1 is normally the data track. The audio
 * is raw 44.1 kHz 16-bit stereo little-endian: offset and length address the
 * file at path. */
typedef struct DiscTrack {
    int number;                     /* 1-based, as on the CD */
    int is_audio;                   /* 0 for a data track */
    const char* path;               /* host file holding the track; valid until disc_close */
    uint64_t offset;                /* byte offset of INDEX 01 (audio starts here, after any pregap) */
    uint64_t length;                /* bytes from INDEX 01 to the end of the track */
    unsigned sector_size;           /* bytes per sector in the file: 2352 (raw) or 2048 (data only) */
} DiscTrack;

#define DISC_NUMBER_AMBIGUOUS (-1)  /* disc_number: both DATA\1CD.ID and DATA\2CD.ID exist */

/* ---- the disc ---- */

/* Open a disc. path is a .cue, a .iso or a directory. On failure returns NULL
 * and writes a message to err (when err_size > 0): missing file, bad cue, not
 * ISO 9660, truncated image, unsupported format. */
Disc* disc_open(const char* path, char* err, size_t err_size);
void disc_close(Disc* disc);        /* accepts NULL; invalidates every DiscEntry and DiscTrack of the disc */
DiscSource disc_source(const Disc* disc);

/* 1 if DATA\1CD.ID exists, 2 if DATA\2CD.ID exists, 0 if neither,
 * DISC_NUMBER_AMBIGUOUS if both. */
int disc_number(const Disc* disc);

/* ---- files ---- */

/* Look up a guest-style path: relative to the disc root, case-insensitive,
 * '\' and '/' both separate, "." and ".." resolved, a ";1" version suffix and
 * a trailing '.' ignored on every component. Returns 1 and fills out (when not
 * NULL) if found, else 0. "" is the root directory. */
int disc_find(const Disc* disc, const char* path, DiscEntry* out);

/* Entries of a directory, in name order. disc_dir_count returns -1 if dir is
 * not a directory; disc_dir_entry returns 1 and fills out for 0 <= index <
 * count, else 0. */
int disc_dir_count(const Disc* disc, const DiscEntry* dir);
int disc_dir_entry(const Disc* disc, const DiscEntry* dir, int index, DiscEntry* out);

/* Open a file for reading (NULL on a directory, an unknown entry or an I/O
 * error). The handle owns its own FILE*; it copies what it needs, but the disc
 * must stay open while it is read. */
DiscFile* disc_file_open(const Disc* disc, const DiscEntry* file);
void disc_file_close(DiscFile* file);   /* accepts NULL */

/* Read up to n bytes at byte offset of the file into buf. Returns the number
 * of bytes read: n, or fewer when the request runs past the end of the file (0
 * at or beyond the end). Returns -1 on an I/O error or an image that ends
 * early. Never reads outside the file. */
int64_t disc_file_read(DiscFile* file, uint64_t offset, void* buf, size_t n);

/* SHA-256 of a whole file of the disc as 64 lower-case hex digits plus the
 * terminating NUL. Returns 0, or -1 on a read error. */
int disc_sha256_file(const Disc* disc, const DiscEntry* file, char hex[65]);

/* SHA-256 of a memory block, same output format (also the self-test hook). */
void disc_sha256_buffer(const void* data, size_t n, char hex[65]);

/* ---- the two discs as one tree ---- */

/* Where a merged copy stands, for a progress display. path is the file being
 * copied or checked, relative, '/'-separated (valid during the call only). */
typedef struct DiscCopyProgress {
    uint64_t bytes_done, bytes_total;   /* of the files the copy writes */
    int files_done, files_total;        /* every file of the merged tree, skipped ones included */
    int files_skipped;                  /* already in place at their full size (a resumed copy) */
    const char* path;
} DiscCopyProgress;
/* Called before each file and after each block written; a nonzero return cancels. */
typedef int (*DiscCopyFn)(void* ctx, const DiscCopyProgress* progress);

/* Copy every file of disc1 and disc2 into dest (created, with its parents
 * missing at most one level) as one tree: the developers' single data tree,
 * which the Develop launch mode plays from (docs/specs/008-editor-
 * restoration/spec.md, phase M). The rules:
 *   - a path both discs have comes from disc 1, the newer copy of every file
 *     that differs (DREAMS.DAT, DATA\HNM\INTRO.HNM, DATA\ICONE\ICONES.BF,
 *     DATA\HD.ID), except DATA\UNIVBE\UVCONFIG.EXE, newer on disc 2 (a DOS
 *     tool the Windows game never reads);
 *   - disc 1's DATA\FULL.ID is left out: in one tree that is both the CD root
 *     and the install root it would switch the game to its copy-to-hard-disk
 *     mode, which purges the level files;
 *   - disc 2's DATA\GAME directory is left out: leftover saves of another
 *     version that the game would offer as the player's own;
 *   - a directory both discs have keeps disc 1's spelling.
 * Resumable: a file already in dest with the source's size is skipped, and
 * each file is written to <name>.part and renamed when complete, so an
 * interrupted copy leaves no short file under a real name. Returns 0, 1 if
 * the callback cancelled, or -1 with a message in err (when err_size > 0). */
int disc_copy_merged(const Disc* disc1, const Disc* disc2, const char* dest, DiscCopyFn progress, void* ctx,
                     char* err, size_t err_size);

/* ---- audio tracks ---- */

/* Number of tracks the disc has, data and audio (an .iso has 1). */
int disc_track_count(const Disc* disc);

/* Track by CD number (1-based). Returns 1 and fills out, or 0 when there is no
 * such track (numbers are normally 1..count; a directory source whose sibling
 * track files skip a number has a gap). For a .cue the offset is the file
 * position of INDEX 01, so playback skips a pregap stored under INDEX 00. For
 * a directory source the tracks are the `*(Track NN).bin` files beside it
 * (the directory's parent), with INDEX 01 taken from a .cue there that names
 * them, else assumed to be the standard 2 seconds in. */
int disc_track(const Disc* disc, int number, DiscTrack* out);

#ifdef __cplusplus
}
#endif

#endif

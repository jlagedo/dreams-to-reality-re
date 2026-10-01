/* Declarations shared by the disc library's sources. Not part of the API. */
#ifndef DISC_INTERNAL_H
#define DISC_INTERNAL_H

#include "disc.h"

#include <stdint.h>
#include <stdio.h>

#define SECTOR_RAW 2352u            /* a CD sector as dumped */
#define SECTOR_USER 2048u           /* the data in it */
#define RAW_USER_OFFSET 16u         /* sync (12) + header (4) before the user data of a MODE1 sector */
#define FRAMES_PER_SECOND 75u
#define MAX_TRACKS 99
#define MSG_LEN 512                 /* internal error text */

/* ---- plat.c: files and directories, UTF-8 paths, 64-bit offsets ---- */
FILE* plat_fopen(const char* path);                     /* "rb"; NULL on failure */
int plat_seek(FILE* f, uint64_t offset);                /* 0 on success */
int plat_file_size(const char* path, uint64_t* size);   /* 0 if path is a regular file */
int plat_is_dir(const char* path);
/* Calls cb for every entry but "." and ".."; stops with the callback's nonzero
 * result. Returns 0, -1 if the directory cannot be listed, else cb's result. */
typedef int (*PlatListFn)(void* ctx, const char* name, uint64_t size, int is_dir);
int plat_list_dir(const char* dir, PlatListFn cb, void* ctx);

/* ---- util (plat.c) ---- */
char* str_dup(const char* s);
char* path_join(const char* dir, const char* name);     /* dir/name with '\' in name turned into '/' */
char* path_dirname(const char* path);                   /* up to the last separator, "." if none */
int ci_compare(const char* a, const char* b);           /* ASCII case-insensitive strcmp */
int err_set(char* err, const char* fmt, ...);           /* writes err (MSG_LEN), returns -1 */

/* ---- cue.c ---- */
typedef struct {
    DiscTrack* tracks;              /* ntracks, in cue order; paths are heap strings the caller frees */
    int ntracks;
} CueTable;
int cue_parse(const char* cue_path, CueTable* out, char* err);
void cue_free(CueTable* t);

#endif

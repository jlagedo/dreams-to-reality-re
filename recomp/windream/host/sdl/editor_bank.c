/*
 * WINDREAM recompilation - the project bank of Develop
 * (docs/specs/008-editor-restoration/spec.md, phase 4) and its writing to
 * the developer folder (phase 5).
 *
 * July's editor kept all 150 project records unpacked in _tableSceneS and
 * edited them in place; retail keeps room for one (0x659044, 0x2200 bytes)
 * but its six bank functions still walk 150, and its Load and Delete bind the
 * save pointer _LoadSaveSceneSPtr (0x661d94) to DDAT_LoadRecord's scratch
 * output _CurrentScene2S (0x65b344), so a save never reaches the next level
 * transition (A4.3). Here the bank is host memory in the guest (shim_alloc,
 * which the Watcom start-up does not clear), unpacked from the DREAMS.DAT the
 * game mounted (_RLE_SAVES 0x633c14) each time DDAT_Load (0x448f5f) reads it,
 * and the retail functions that walk it are replaced (lift/replacements.py
 * HOST_ENTRIES; Develop only, from host_mode_install):
 *
 *   0x449822  free slot     first record with +0x14 bit 0 clear; 0x4a4760 = its index
 *   0x44988f  project list  in-use records into a host table (16-byte names, then
 *                           16-byte meshes from +0x60c at +0x960, July's layout),
 *                           count in 0x661d5c, instead of the one-byte tables
 *                           0x661e25/26 whose 150 strcpy's ran over the resource
 *                           heap's globals (0x661e28..; phase 3 research)
 *   0x448c6d  empty bank    150 "EMPTY" records (DDAT_Load's fallback with no
 *                           dreams.dat; retail cleared 0x13ec00 bytes from 0x659044,
 *                           past the end of .bss)
 *   0x449e7a  Create        as retail, or refused with a message when no slot is
 *                           free (B1; every shipped record is in use)
 *   0x449ee9  Load          the page, then the chosen record of the bank
 *   0x449f80  Delete        the page, then +0x14 of the chosen bank record zeroed
 *                           (the bank only: DREAMS.DAT in memory changes at the
 *                           next save)
 *   0x449f42  Save          as retail (the working copy into *0x661d94), then the
 *                           bank packed into _RLE_SAVES in place, so the retail
 *                           DDAT_LoadRecord serves the edit at the next transition;
 *                           refused (record put back) when it would not fit
 *   0x448f5f  DDAT_Load     the original, then the bank unpacked from it; a dirty
 *                           bank is written to disk first (phase 5)
 *   0x44900a  SaveDiskScene_  GAME_Shutdown's call, empty in retail: a dirty bank
 *                           into the developer folder's root, DREAMS.DAT and
 *                           EDITOR.DAT, as July wrote them (phase 5; also F10 with
 *                           the editor on, dev_keys.c)
 *
 * The project page itself (0x44991e) is editor_pickers.c's (phase 3): it pages
 * this bank's list (rebuilt every frame) and copies the chosen row's name into
 * the buffer Load and Delete pass (here a host buffer of 256 bytes, not the
 * 16-byte stack local); the row is the list row, 0x661d78.
 *
 * Host-only structure:
 *   - The pointer binding. Before each editor call (editor_frame_begin), when
 *     the working copy (0x65fb04) holds a level whose name differs from the
 *     last one seen, 0x661d94 is pointed at the bank record of that name (0 if
 *     none: a save then does nothing, as retail with no project). Create and
 *     Load set the name seen themselves. Every reader of 0x661d94 runs inside
 *     the editor call (Load, Delete, Save, the quick reload, the objet and link
 *     pastes 0x44a517 and 0x44b0e5, which call Create on 0 and so cleared the
 *     live level), so binding there is binding at every level load for them.
 *   - Load and Delete keep the pointer when the page is cancelled: retail set
 *     it to DDAT_LoadRecord's answer every frame (0 for "NULL"). After a cancel
 *     WorksGetEditor_ (0x44c625) reloads the level from the working copy when
 *     the pointer is non-zero, so Load hides it (0) for the rest of that editor
 *     call and editor_bank_frame_end puts it back.
 *   - The messages: drawn at the screen's foot for three seconds of editor
 *     frames with the editor's own TEXT_DrawString (0x44d5c7, font 0), and
 *     printed on stderr as "[bank] ...".
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RECOMP_GENERATED_CODE   /* the guest registers: the handlers' returns, the stack argument */
#include "host.h"
#include "render_boundary.h"
#include "editor_bank_rle.h"

#define RLE_SAVES    0x00633C14u  /* DREAMS.DAT as DDAT_Load read it, 0x25400 bytes */
#define PREV_NAME    0x00633BF4u  /* DDAT_LoadRecord's fallback name, 0x20 bytes */
#define FALLBACK     0x004A4804u  /* DDAT_LoadRecord: 1 when it fell back to PREV_NAME */
#define WORK         0x0065FB04u  /* _CurrentSceneS, the working project */
#define IMPORT_PTR   0x00661E04u  /* _ImportNewScenePtr */
#define SAVE_PTR     0x00661D94u  /* _LoadSaveSceneSPtr */
#define FREE_INDEX   0x004A4760u  /* the free slot's index, Create's "Project<n>" */
#define LIST_NB      0x00661D5Cu  /* the project list's count, the page's Nb */
#define SEMA_CREATE  0x004A46B8u
#define SEMA_LOAD    0x004A46BCu  /* _SemaLoadScene: the project page is open; the page clears it */
#define SEMA_SAVE    0x004A46C0u
#define SEMA_DELETE  0x004A46C4u
#define PAGE         0x0044991Eu  /* GetSeachSceneName_: EAX = the buffer the chosen name goes into */
#define TEXT_DRAW    0x0044D5C7u  /* TEXT_DrawString(EAX s, EDX x, EBX y, ECX 0x100, [esp+4] font), ret 4 */
#define DELETE_BANNER 0x004C5E86u /* "DELETE Project ..." (retail data) */
#define VIDEO_HEIGHT 0x0049DA00u
#define DIRTY        0x00661DA4u  /* the bank changed since DDAT_Load (WorksGetEditor_ sets it) */
#define NAME_CAP     32
#define BUF_CAP      256
#define LIST_STRIDE  16
#define LIST_MESHES  (BANK_RECORDS * LIST_STRIDE)   /* 0x960 */
#define MSG_FRAMES   90

static uint32_t g_bank;        /* guest VA of the 150 records */
static uint32_t g_list;        /* guest VA of the list: names, then meshes at +0x960 */
static int g_list_slot[BANK_RECORDS];
static int g_list_n;
static uint32_t g_buf;         /* the page's name buffer */
static uint32_t g_msg_va;      /* the message, a guest string */
static int g_msg_frames;
static int g_ready;            /* the bank holds the records */
static int g_from_empty;       /* DDAT_Load fell back to 0x448c6d */
static char g_seen[NAME_CAP];  /* the working copy's name at the last binding */
static uint32_t g_hidden;      /* the pointer Load hid for the rest of the editor call */
static int g_hiding;
static uint32_t g_packed;      /* _RLE_SAVES's size after the last unpack or save */
static uint8_t g_snapshot[BANK_RECORD_SIZE];

static uint8_t* host_rec(int i) { return (uint8_t*)wd_host_range(g_bank + (uint32_t)i * BANK_RECORD_SIZE, BANK_RECORD_SIZE, 1); }
static uint32_t rec_va(int i) { return g_bank + (uint32_t)i * BANK_RECORD_SIZE; }
static int slot_of(uint32_t va) {
    if (!g_bank || va < g_bank || va >= g_bank + BANK_RECORDS * BANK_RECORD_SIZE) return -1;
    return (va - g_bank) % BANK_RECORD_SIZE ? -1 : (int)((va - g_bank) / BANK_RECORD_SIZE);
}

static void message(const char* fmt, ...) {
    char text[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[bank] %s\n", text);
    if (g_msg_va) {
        guest_strcpy_out(g_msg_va, 128, text);
        g_msg_frames = MSG_FRAMES;
    }
}

static void draw_text(uint32_t s, uint32_t x, uint32_t y, uint32_t font) {
    wd_host_range(g_esp - 4, 4, 1);
    PUSH32(g_esp, font);
    guest_call_regs(TEXT_DRAW, s, x, y, 0x100);
    g_esp += 4;
}

static void rec_name(int i, char* out) {
    memcpy(out, host_rec(i), NAME_CAP - 1);
    out[NAME_CAP - 1] = 0;
}

uint32_t editor_bank_record(int slot) { return g_ready && slot >= 0 && slot < BANK_RECORDS ? rec_va(slot) : 0; }

/* The first record named so, as DDAT_LoadRecord matches (never "EMPTY"). */
uint32_t editor_bank_find(const char* name) {
    if (!g_ready || !name[0] || !strcmp(name, "EMPTY")) return 0;
    char have[NAME_CAP];
    for (int i = 0; i < BANK_RECORDS; i++) {
        rec_name(i, have);
        if (!strcmp(have, name)) return rec_va(i);
    }
    return 0;
}

int editor_bank_list(void) {
    g_list_n = 0;
    if (g_ready) {
        uint8_t* list = (uint8_t*)wd_host_range(g_list, 2 * LIST_MESHES, 1);
        memset(list, 0, 2 * LIST_MESHES);
        for (int i = 0; i < BANK_RECORDS; i++) {
            const uint8_t* r = host_rec(i);
            if (!(r[0x14] & 1)) continue;
            memcpy(list + g_list_n * LIST_STRIDE, r, LIST_STRIDE - 1);
            memcpy(list + LIST_MESHES + g_list_n * LIST_STRIDE, r + 0x60C, LIST_STRIDE - 1);
            g_list_slot[g_list_n++] = i;
        }
    }
    WD_HOST_WRITE32(LIST_NB) = (uint32_t)g_list_n;
    return g_list_n;
}
uint32_t editor_bank_list_names(void) { return g_list; }
int editor_bank_list_slot(int row) { return row >= 0 && row < g_list_n ? g_list_slot[row] : -1; }

static int free_slot(void) {
    if (!g_ready) return -1;
    for (int i = 0; i < BANK_RECORDS; i++)
        if (!(host_rec(i)[0x14] & 1)) return i;
    return -1;
}

/* The bank into _RLE_SAVES, in place: packed in a host copy (keeping bytes
 * 0x25c..0x3ff), the rest of the buffer zeroed as after DDAT_Load's read. */
static int recompress(void) {
    static uint8_t tmp[BANK_FILE_CAP];
    uint8_t* guest = (uint8_t*)wd_host_range(RLE_SAVES, BANK_FILE_CAP, 1);
    memcpy(tmp, guest, BANK_FILE_CAP);
    size_t n = bank_rle_pack((const uint8_t*)wd_host_range(g_bank, BANK_RECORDS * BANK_RECORD_SIZE, 0), tmp, BANK_FILE_CAP);
    if (!n) return 0;
    memset(tmp + n, 0, BANK_FILE_CAP - n);
    memcpy(guest, tmp, BANK_FILE_CAP);
    g_packed = (uint32_t)n;
    return 1;
}

static void unpack(void) {
    char why[160];
    const uint8_t* src = (const uint8_t*)wd_host_range(RLE_SAVES, BANK_FILE_CAP, 0);
    uint8_t* dst = (uint8_t*)wd_host_range(g_bank, BANK_RECORDS * BANK_RECORD_SIZE, 1);
    g_ready = bank_rle_unpack(src, BANK_FILE_CAP, dst, why, sizeof why);
    g_seen[0] = 0;   /* bind again at the next editor frame */
    if (!g_ready) {
        memset(dst, 0, BANK_RECORDS * BANK_RECORD_SIZE);
        fprintf(stderr, "[bank] DREAMS.DAT in memory does not unpack (%s): no project bank, Create refuses\n", why);
        return;
    }
    g_packed = BANK_HEADER + (src[0x258] | src[0x259] << 8 | src[0x25A] << 16 | (uint32_t)src[0x25B] << 24);
    fprintf(stderr, "[bank] %d records unpacked from DREAMS.DAT in memory (%u bytes); records at 0x%08X, list at 0x%08X\n",
            BANK_RECORDS, g_packed, g_bank, g_list);
}

/* ---- the replacements (__watcall, no stack arguments, a plain RET) ---- */

static void bank_free_slot(void) {
    int i = free_slot();
    if (i >= 0) WD_HOST_WRITE32(FREE_INDEX) = (uint32_t)i;
    g_eax = i >= 0 ? rec_va(i) : 0;
    g_esp += 4;
}

static void bank_list_handler(void) {
    editor_bank_list();
    g_esp += 4;
}

static void bank_empty(void) {
    uint8_t* bank = (uint8_t*)wd_host_range(g_bank, BANK_RECORDS * BANK_RECORD_SIZE, 1);
    memset(bank, 0, BANK_RECORDS * BANK_RECORD_SIZE);
    for (int i = 0; i < BANK_RECORDS; i++) {
        uint8_t* r = bank + (size_t)i * BANK_RECORD_SIZE;
        strcpy((char*)r, "EMPTY");
        for (int k = 0; k < 8; k++) strcpy((char*)r + 0x200 + 0x80 * k, "EMPTY");
        for (int k = 0; k < 16; k++) {
            strcpy((char*)r + 0x600 + 0xC0 * k, "EMPTY");
            strcpy((char*)r + 0x600 + 0xC0 * k + 0xC, "EMPTY");
        }
    }
    g_ready = 1;
    g_from_empty = 1;
    g_seen[0] = 0;
    fprintf(stderr, "[bank] no dreams.dat: the bank is 150 EMPTY records\n");
    g_esp += 4;
}

static void bank_create(void) {
    int i = free_slot();
    if (i < 0) {
        message(g_ready ? "Project Create refused: all %d projects are in use; delete one first"
                        : "Project Create refused: no project bank (%d records did not unpack)", BANK_RECORDS);
        WD_HOST_WRITE32(SEMA_CREATE) = 0;
        g_esp += 4;
        return;
    }
    char name[NAME_CAP];
    snprintf(name, sizeof name, "Project%d", i);
    WD_HOST_WRITE32(FREE_INDEX) = (uint32_t)i;
    WD_HOST_WRITE32(SAVE_PTR) = rec_va(i);
    memset(wd_host_range(WORK, BANK_RECORD_SIZE, 1), 0, BANK_RECORD_SIZE);
    WD_HOST_WRITE8(WORK + 0x14) |= 1;
    guest_strcpy_out(WORK, 16, name);
    snprintf(g_seen, sizeof g_seen, "%s", name);
    WD_HOST_WRITE32(SEMA_CREATE) = 0;
    message("Project Create: %s in slot %d (saved with W)", name, i);
    g_esp += 4;
}

/* The page, with the buffer holding `initial`; 1 and the chosen name once the
 * page has closed on a name other than `initial`. */
static int page(const char* initial, char* chosen) {
    guest_strcpy_out(g_buf, BUF_CAP, initial);
    guest_call_regs(PAGE, g_buf, 0, 0, 0);
    if (WD_HOST_READ32(SEMA_LOAD)) return 0;
    guest_str(g_buf, chosen, NAME_CAP);
    return strcmp(chosen, initial) != 0;
}

static void bank_load(void) {
    char name[NAME_CAP];
    editor_bank_list();
    int closed_on_name = page("NULL", name);
    if (!WD_HOST_READ32(SEMA_LOAD)) {
        uint32_t rec = closed_on_name ? editor_bank_find(name) : 0;
        if (rec) {
            char current[NAME_CAP];
            guest_str(WD_HOST_READ32(IMPORT_PTR), current, sizeof current);   /* as DDAT_LoadRecord does */
            guest_strcpy_out(PREV_NAME, 0x20, current);
            WD_HOST_WRITE32(FALLBACK) = 0;
            WD_HOST_WRITE32(SAVE_PTR) = rec;
            memcpy(wd_host_range(WORK, BANK_RECORD_SIZE, 1), wd_host_range(rec, BANK_RECORD_SIZE, 0), BANK_RECORD_SIZE);
            snprintf(g_seen, sizeof g_seen, "%s", name);
            fprintf(stderr, "[bank] Project Load: %s (slot %d)\n", name, slot_of(rec));
        } else {
            if (closed_on_name) message("Project Load: no project named \"%s\"", name);
            g_hidden = WD_HOST_READ32(SAVE_PTR);   /* no reload of the working copy */
            g_hiding = 1;
            WD_HOST_WRITE32(SAVE_PTR) = 0;
        }
    }
    g_esp += 4;
}

static void bank_delete(void) {
    char name[NAME_CAP];
    WD_HOST_WRITE32(SEMA_LOAD) = 1;
    editor_bank_list();
    draw_text(DELETE_BANNER, 0x96, 0x1E, 0);
    if (page("DELETE", name)) {
        uint32_t rec = editor_bank_find(name);
        if (rec) {
            WD_HOST_WRITE32(rec + 0x14) = 0;   /* the whole dword, as retail (bits 16-18 too) */
            message("Project Delete: %s (slot %d) marked free; DREAMS.DAT changes at the next save", name, slot_of(rec));
        } else {
            message("Project Delete: no project named \"%s\"", name);
        }
    }
    if (!WD_HOST_READ32(SEMA_LOAD)) WD_HOST_WRITE32(SEMA_DELETE) = 0;
    WD_HOST_WRITE32(SEMA_LOAD) = 0;
    g_esp += 4;
}

static void bank_save(void) {
    uint32_t ptr = WD_HOST_READ32(SAVE_PTR);
    int slot = slot_of(ptr);
    if (ptr && slot < 0) {
        memcpy(wd_host_range(ptr, BANK_RECORD_SIZE, 1), wd_host_range(WORK, BANK_RECORD_SIZE, 0), BANK_RECORD_SIZE);
    } else if (ptr) {
        uint8_t* rec = host_rec(slot);
        memcpy(g_snapshot, rec, BANK_RECORD_SIZE);
        memcpy(rec, wd_host_range(WORK, BANK_RECORD_SIZE, 0), BANK_RECORD_SIZE);
        if (recompress()) {
            char name[NAME_CAP];
            rec_name(slot, name);
            fprintf(stderr, "[bank] Project Save: %s into slot %d; DREAMS.DAT in memory is %u bytes\n", name, slot, g_packed);
        } else {
            memcpy(rec, g_snapshot, BANK_RECORD_SIZE);
            message("Project Save refused: the bank would not fit in DREAMS.DAT (%u bytes)", BANK_FILE_CAP);
        }
    }
    WD_HOST_WRITE32(SEMA_SAVE) = 0;
    g_esp += 4;
}

/* ---- the bank on disk (phase 5) ---- */

typedef struct { const char* want; char* found; } NameSearch;
static SDL_EnumerationResult SDLCALL name_seen(void* userdata, const char* dirname, const char* fname) {
    NameSearch* s = (NameSearch*)userdata;
    (void)dirname;
    if (!SDL_strcasecmp(fname, s->want)) { SDL_strlcpy(s->found, fname, NAME_CAP); return SDL_ENUM_SUCCESS; }
    return SDL_ENUM_CONTINUE;
}

/* data into root/name (the existing file of that name in any case, else name
 * as given), through name.tmp and a rename: a crash leaves the old file or the
 * new one, never half of one. */
static int write_file(const char* root, const char* name, const void* data, size_t size, char* why, size_t cap) {
    char leaf[NAME_CAP], path[1024], tmp[1040];
    NameSearch s = { name, leaf };
    SDL_strlcpy(leaf, name, sizeof leaf);
    SDL_EnumerateDirectory(root, name_seen, &s);
    SDL_snprintf(path, sizeof path, "%s/%s", root, leaf);
    SDL_snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (!SDL_SaveFile(tmp, data, size)) {
        SDL_snprintf(why, cap, "cannot write %s: %s", tmp, SDL_GetError());
        SDL_RemovePath(tmp);
        return 0;
    }
    if (!SDL_RenamePath(tmp, path)) {
        SDL_snprintf(why, cap, "cannot replace %s: %s", path, SDL_GetError());
        SDL_RemovePath(tmp);
        return 0;
    }
    return 1;
}

/* When the bank is dirty (0x661da4: retail's WorksGetEditor_ sets it after a
 * project save or delete and an objet, link, box or event delete; DDAT_Load
 * clears it), the bank into the developer folder's root, as July's
 * SaveDiskScene_ wrote it: EDITOR.DAT the 150 records raw (0x13ec00 bytes),
 * then DREAMS.DAT packed. The bank is packed into _RLE_SAVES first, so the file
 * and the game's copy in memory are the same bytes (a Delete reaches both,
 * as July packed the whole bank at the write). 1 when written. */
int editor_bank_write_disk(const char* why_now) {
    char why[1200];
    const char* tree = files_tree();
    if (!g_ready || !host_develop()) return 0;
    if (!WD_HOST_READ32(DIRTY)) {
        if (!strcmp(why_now, "F10")) message("Bank unchanged since DREAMS.DAT was read: nothing written");
        return 0;
    }
    if (!tree) {
        message("Bank not written (%s): no developer folder (WD_TREE)", why_now);
        return 0;
    }
    if (!recompress()) {
        message("Bank not written (%s): it would not fit in DREAMS.DAT (%u bytes)", why_now, BANK_FILE_CAP);
        return 0;
    }
    if (!write_file(tree, "EDITOR.DAT", wd_host_range(g_bank, BANK_RECORDS * BANK_RECORD_SIZE, 0),
                    BANK_RECORDS * BANK_RECORD_SIZE, why, sizeof why)
        || !write_file(tree, "DREAMS.DAT", wd_host_range(RLE_SAVES, g_packed, 0), g_packed, why, sizeof why)) {
        message("Bank not written (%s): %s", why_now, why);
        return 0;
    }
    WD_HOST_WRITE32(DIRTY) = 0;
    message("Bank written (%s): DREAMS.DAT (%u bytes) and EDITOR.DAT into %s", why_now, g_packed, tree);
    return 1;
}

/* SaveDiskScene_ (0x44900a, GAME_Shutdown's call; retail's body is empty). */
static void bank_save_disk(void) {
    editor_bank_write_disk("shutdown");
    g_esp += 4;
}

/* DDAT_Load reads DREAMS.DAT again at New Game: a dirty bank is written first,
 * so the edits saved since are played and kept (coordinator decision,
 * 2026-10-04). When that write fails, the bank is kept and packed over what
 * the read brought, and stays dirty for the next F10 or shutdown. */
static void bank_ddat_load(void) {
    recomp_func_t original = recomp_lookup_reference(0x448F5Fu);
    int keep = 0;
    g_from_empty = 0;
    if (!original) {
        fprintf(stderr, "[bank] FATAL: no reference DDAT_Load (0x448f5f); re-run lift.py\n");
        abort();
    }
    if (g_ready && WD_HOST_READ32(DIRTY)) keep = !editor_bank_write_disk("DREAMS.DAT read again");
    original();   /* consumes the return address */
    if (keep && recompress()) {
        WD_HOST_WRITE32(DIRTY) = 1;
        fprintf(stderr, "[bank] DREAMS.DAT read again: the unwritten bank kept and packed over it\n");
    } else if (!g_from_empty) {
        unpack();
    }
}

/* ---- around the editor call ---- */

void editor_bank_frame_begin(void) {
    if (!g_ready) return;
    char name[NAME_CAP];
    guest_str(WORK, name, 16);
    if (!name[0] || !strcmp(name, g_seen)) return;
    snprintf(g_seen, sizeof g_seen, "%s", name);
    uint32_t rec = editor_bank_find(name);
    if (WD_HOST_READ32(SAVE_PTR) != rec)
        fprintf(stderr, "[bank] the level is %s: the save pointer -> %s (slot %d)\n", name, rec ? "its record" : "0, not in the bank", slot_of(rec));
    WD_HOST_WRITE32(SAVE_PTR) = rec;
}

void editor_bank_frame_end(void) {
    if (g_hiding) {
        if (!WD_HOST_READ32(SAVE_PTR)) WD_HOST_WRITE32(SAVE_PTR) = g_hidden;
        g_hiding = 0;
    }
    if (g_msg_frames > 0) {
        g_msg_frames--;
        uint32_t h = WD_HOST_READ32(VIDEO_HEIGHT);
        draw_text(g_msg_va, 0x14, h > 40 && h <= 4096 ? h - 20 : 0x1AE, 0);
    }
}

void editor_bank_install(void) {
    static const struct { uint32_t va; wd_guest_replacement fn; const char* what; } k[] = {
        {0x00449822u, bank_free_slot, "free slot"},
        {0x0044988Fu, bank_list_handler, "project list"},
        {0x00448C6Du, bank_empty, "empty bank"},
        {0x00449E7Au, bank_create, "Project Create"},
        {0x00449EE9u, bank_load, "Project Load"},
        {0x00449F80u, bank_delete, "Project Delete"},
        {0x00449F42u, bank_save, "Project Save"},
        {0x00448F5Fu, bank_ddat_load, "DDAT_Load"},
        {0x0044900Au, bank_save_disk, "SaveDiskScene_"},
    };
    g_bank = shim_alloc(BANK_RECORDS * BANK_RECORD_SIZE, 16);
    g_list = shim_alloc(2 * LIST_MESHES, 16);
    g_buf = shim_alloc(BUF_CAP, 16);
    g_msg_va = shim_alloc(128, 16);
    int ok = 0;
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        if (wd_install_replacement(k[i].va, k[i].fn)) ok++;
        else fprintf(stderr, "[bank] WARNING: cannot replace %s (0x%06x)\n", k[i].what, k[i].va);
    }
    fprintf(stderr, "[bank] %d of %d bank functions replaced; records at 0x%08X\n", ok, (int)(sizeof k / sizeof k[0]), g_bank);
}

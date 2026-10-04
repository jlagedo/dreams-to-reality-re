/*
 * WINDREAM recompilation - the save guard, in every launch mode
 * (docs/specs/008-editor-restoration/spec.md B1 "Saves", phase 7).
 *
 * GAME_LoadGame (0x40f94a; __watcall, EAX the slot's title, EAX 0 loaded or
 * -1 not; its one call site 0x437960 in MENU_HandleSaveSlotInput, from the
 * in-game Load page and the main menu) finds the first of the 10 index slots
 * with that title, opens install root + data\game\game<n>.dat (n the slot's
 * file number, 0x52eb70), reads the level-state ring 0x5e2b08 and 0x49da84,
 * then the 32-byte project name and only then DDAT_LoadRecord(name), whose
 * NULL it copies from without a check (memcpy_ to 0x52c970) [verified in
 * code 0x40f94a, 0x40fa9f]. A save naming a project the bank lacks (a bank
 * changed outside the editor) or a foreign, wrong-size file therefore crashes
 * the game or, with the editor off, loads the last level by DDAT_LoadRecord's
 * fallback (0x633bf4), with the state already overwritten.
 *
 * The replacement (host-only structure, lift/replacements.py HOST_ENTRIES)
 * reads the same file first through the host file layer and refuses it
 * unless it is 11,388 bytes (the 0x2880 ring, 4, the 32-byte name, 4, 4,
 * 0x3b8, 0xc, 0xc: GAME_SaveGame's writes) and its name is NUL-terminated
 * within 32 bytes, not "EMPTY", and found in _RLE_SAVES by DDAT_LoadRecord's
 * own rule (strcmp of each of the 150 offsets' names at 0x634014 + offset).
 * A refusal returns -1 with nothing written, as the original does when the
 * file does not open, after putting the guard's wording into help entry 5
 * (0x4a121b, "Error on disk / impossible to load / this game", three 33-byte
 * lines), which the in-game Load page shows for code 8; the next call puts
 * the original text back. Anything else goes to the original untouched, so a
 * valid load is retail's. The main menu discards code 8 (0x4a2ef5); the
 * inserted call wd_menu_save_slots (lift.py CALLS 0x43630c, after
 * MENU_DrawSaveSlots in MENU_Tick) draws the same entry there while it is 8.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define RECOMP_GENERATED_CODE
#include "host.h"
#include "render_boundary.h"

#define LOAD_GAME       0x0040F94Au
#define INSTALL_ROOT    0x004285BEu   /* FILE_GetInstallRoot: EAX char* */
#define SLOT_NAMES      0x005DABF8u   /* 10 x 0x16 */
#define SLOT_FILES      0x0052EB70u   /* 10 x int: game<n>.dat */
#define RLE_OFFSETS     0x00633C14u   /* 151 offsets into the data at 0x634014 */
#define RLE_DATA        0x00634014u
#define HELP_LOAD_ERROR 0x004A121Bu   /* help entry 5: 3 lines of 33 bytes */
#define HELP_DRAW       0x0043126Au   /* MENU_DrawHelpText(EAX entry) */
#define MENU_CODE       0x004A2EF5u   /* the main menu's last slot-page code */
#define SAVE_SIZE       11388u
#define NAME_AT         0x2884u

static uint32_t g_path;            /* guest buffer for the save's path */
static char g_help[99];            /* help entry 5 as shipped */
static int g_help_changed;

static void help_restore(void) {
    if (!g_help_changed) return;
    memcpy(wd_host_range(HELP_LOAD_ERROR, sizeof g_help, 1), g_help, sizeof g_help);
    g_help_changed = 0;
}

/* Three lines of at most 32 characters into help entry 5. */
static void help_say(const char* a, const char* b, const char* c) {
    const char* line[3] = {a, b, c};
    uint8_t* h = (uint8_t*)wd_host_range(HELP_LOAD_ERROR, sizeof g_help, 1);
    memset(h, 0, sizeof g_help);
    for (int i = 0; i < 3; i++) SDL_strlcpy((char*)h + 33 * i, line[i], 33);
    g_help_changed = 1;
}

/* DDAT_LoadRecord's rule: "EMPTY" never, else the first of the 150 names
 * equal to it (strcmp over the packed records' leading names). */
static int in_bank(const char* name) {
    if (!strcmp(name, "EMPTY")) return 0;
    for (uint32_t i = 0; i < 150; i++) {
        char have[33];
        guest_str(RLE_DATA + WD_HOST_READ32(RLE_OFFSETS + 4 * i), have, sizeof have);
        if (!strcmp(have, name)) return 1;
    }
    return 0;
}

/* Why the save must not load, or NULL. */
static const char* refuse_why(const uint8_t* data, size_t n, char* name) {
    if (n != SAVE_SIZE) return "size";
    memcpy(name, data + NAME_AT, 32);
    name[32] = 0;
    if (!memchr(data + NAME_AT, 0, 32)) return "name";
    if (!in_bank(name)) return "level";
    return NULL;
}

static void guard_load_game(void) {
    recomp_func_t original = recomp_lookup_reference(LOAD_GAME);
    if (!original) {
        fprintf(stderr, "[save] FATAL: no reference GAME_LoadGame (0x40f94a); re-run lift.py\n");
        abort();
    }
    help_restore();
    char title[0x17];
    guest_str(g_eax, title, sizeof title);
    int slot = -1;
    for (int i = 0; i < 10 && slot < 0; i++) {
        char have[0x17];
        guest_str(SLOT_NAMES + 0x16u * (uint32_t)i, have, sizeof have);
        if (!strcmp(have, title)) slot = i;
    }
    if (slot >= 0 && g_path) {
        char path[300];
        guest_str(guest_call_regs(INSTALL_ROOT, 0, 0, 0, 0), path, 260);
        size_t at = strlen(path);
        snprintf(path + at, sizeof path - at, "data\\game\\game%d.dat", (int)WD_HOST_READ32(SLOT_FILES + 4u * (uint32_t)slot));
        guest_strcpy_out(g_path, 300, path);
        size_t n = 0;
        uint8_t* data = (uint8_t*)files_read_guest(g_path, &n);
        if (data) {
            char name[33] = "";
            const char* why = refuse_why(data, n, name);
            free(data);
            if (why) {
                if (!strcmp(why, "size")) {
                    help_say("Save refused", "not a save of this game", "(wrong size)");
                    fprintf(stderr, "[save] guard: \"%s\" refused: %s is %u bytes, not 11,388\n", title, path, (unsigned)n);
                } else if (!strcmp(why, "name")) {
                    help_say("Save refused", "its level name is damaged", "");
                    fprintf(stderr, "[save] guard: \"%s\" refused: %s has no level name\n", title, path);
                } else {
                    help_say("Save refused: its level", name, "is not in this bank");
                    fprintf(stderr, "[save] guard: \"%s\" refused: level \"%s\" is not in the bank\n", title, name);
                }
                g_eax = 0xFFFFFFFFu;
                g_esp += 4;
                return;
            }
        }
    }
    original();   /* consumes the return address */
}

/* lift.py CALLS 0x43630c: MENU_Tick, after MENU_DrawSaveSlots on the main
 * menu's slot page. The page discards code 8; show what the in-game page
 * shows for it. */
void wd_menu_save_slots(void) {
    if (WD_HOST_READ32(MENU_CODE) == 8) guest_call_regs(HELP_DRAW, 5, 0, 0, 0);
}

void save_guard_install(void) {
    memcpy(g_help, wd_host_range(HELP_LOAD_ERROR, sizeof g_help, 0), sizeof g_help);
    g_path = shim_alloc(300, 4);
    if (wd_install_replacement(LOAD_GAME, guard_load_game))
        fprintf(stderr, "[save] guard on GAME_LoadGame (0x40f94a)\n");
    else
        fprintf(stderr, "[save] WARNING: cannot replace GAME_LoadGame (0x40f94a): no save guard\n");
}

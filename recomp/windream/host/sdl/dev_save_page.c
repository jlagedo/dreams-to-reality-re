/*
 * WINDREAM recompilation - Cryo's Save page on keypad 0 in Develop
 * (docs/specs/008-editor-restoration/spec.md B1 "Save on toggle", phase 2
 * rule 6, phase 7 "Save page").
 *
 * 0x4313b5 is MENU_OpenLoadPage in save mode, never called: it sets the menu
 * page (0x4a1537 = 2, 0x4a155b = 1 save, 0x4a155f, 0x4a153b, 0x4a1543), picks
 * a slot with MENU_InitSaveSlotSelect(1) (0x437aa2) into 0x4a1563 and runs the
 * modal MENU_RunGameMenu (0x4337c0) [verified in code]. Inside it the page's
 * typed-title entry is live (MENU_HandleSaveSlotInput 0x4373a7: codes
 * 0x20-0x7e of event 0x33, at most 20 characters, Backspace from the key
 * state 0x6308e0, Return confirms, Esc puts the title back from 0x626f30),
 * but Return writes only the index (GAME_SaveIndex, game.dat), never the
 * slot's game<n>.dat or its .ico, and the slot is frozen on the most recent
 * unprotected one, which the page renames.
 *
 * The host's recipe (host-only structure around Cryo's page):
 *   - Keypad 0 latches a request; dev_tools.c serves it from wd_editor_frame
 *     (GAME_TickFrame before GAME_HandleHotkeys, the L key's context) with the
 *     editor off, a level playing and no video, load, exit fade, quit or demo.
 *   - The slot is the owner's (2026-10-04): the first empty one, else the
 *     least recent unprotected one, so keypad 0 adds a save and never replaces
 *     the latest. The opener's five stores and MENU_InitSaveSlotSelect's own
 *     (0x4a2f3d the slot, its title into 0x626f30, GAME_LoadSaveIcon,
 *     0x4a2f35/49/39/55, 0x4a1563 = 2 unprotected) are made here for that
 *     slot, whose title is cleared for the entry (Esc puts it back from
 *     0x626f30, as the page does); 0x4a2f41 = 1 (save mode before the first
 *     draw); then MENU_RunGameMenu.
 *   - While the page is open, dev_keys.c passes the game only Backspace,
 *     Tab, Return and Esc, and queues typed ASCII 0x20-0x7e here; the
 *     PeekMessageA bridge posts one character per menu frame as event 0x33
 *     into the standing handler's queue (0x626f70) while the handler is
 *     CTRL_Dispatcher (0x40e75c), which keeps only the last 0x33 of a drain.
 *   - When the menu returns with status 4 (the page's confirm), the slot of
 *     highest recency holds the title; a title another slot has already is
 *     refused (the index is put back and written again); else the autosave's
 *     three calls make the save: GAME_SaveGame (0x40f542), GAME_SaveIndex
 *     (0x40f202), GAME_SaveThumbnail (0x40fd54), each checked for 0.
 */
#include <stdio.h>
#include <string.h>
#define RECOMP_GENERATED_CODE
#include "host.h"

#define EDITOR_FLAG     0x004A477Cu
#define GAME_HANDLER    0x00626F74u
#define GAME_QUEUE      0x00626F70u
#define GAME_TICK_FRAME 0x00416D45u
#define CTRL_DISPATCHER 0x0040E75Cu
#define POST_MESSAGE    0x0043B31Au   /* MGM_PostMessage(queue, type, a, b) */
#define MENU_TIMER      0x005DF488u   /* MGM 0x11 time, refreshed once per menu frame */
#define KEY_CODE        0x00626FD8u
#define PLAYER          0x004FBA78u
#define PROJECT_PTR     0x00661E04u
#define LOADING         0x00661E08u
#define VIDEO_ON        0x0049D5B4u
#define VIDEO_CUT       0x0049D5B8u
#define FADE_TIMER      0x005E5480u
#define QUIT            0x004A4780u
#define DEMO_MODE       0x0049D34Au   /* 2 normal, 0 record, 1 play */

#define MENU_RUN        0x004337C0u
#define LOAD_ICON       0x0040FC1Fu   /* GAME_LoadSaveIcon(EAX name) */
#define SAVE_GAME       0x0040F542u   /* GAME_SaveGame(EAX title) */
#define SAVE_INDEX      0x0040F202u   /* GAME_SaveIndex() */
#define SAVE_THUMBNAIL  0x0040FD54u   /* GAME_SaveThumbnail(EAX title) */

#define SLOT_NAMES      0x005DABF8u   /* 10 x 0x16 */
#define SLOT_PROTECTED  0x005DAB98u   /* 10 x int */
#define SLOT_RECENCY    0x005DABC0u   /* 10 x int */
#define SLOT_FILES      0x0052EB70u   /* 10 x int: game<n>.dat, -1 none */
#define SLOT_TITLE      0x00626F30u   /* the selected slot's title before the entry */
#define SLOTS           10

/* MENU_InitSaveSlotSelect's and the opener's cells */
#define PAGE            0x004A1537u
#define PAGE_SAVE       0x004A155Bu
#define PAGE_F          0x004A155Fu
#define PAGE_B          0x004A153Bu
#define PAGE_HELP_TIME  0x004A1543u
#define PAGE_STATUS     0x004A1563u   /* 4: saved (confirm) */
#define MENU_EXIT       0x004A1533u   /* MENU_RunGameMenu loops while 0 */
#define SEL_CLOSED      0x004A2F35u
#define SEL_DIRTY       0x004A2F39u
#define SEL_SLOT        0x004A2F3Du
#define SEL_SAVE_MODE   0x004A2F41u
#define SEL_DONE        0x004A2F49u
#define SEL_55          0x004A2F55u

static int g_open;              /* inside the host's MENU_RunGameMenu call */
static uint8_t g_typed[32];
static int g_typedn;
static uint32_t g_last_timer;
static uint32_t g_title;        /* guest copy of the title for the save calls */

typedef struct { uint8_t names[SLOTS * 0x16]; uint8_t prot[SLOTS * 4], rec[SLOTS * 4], files[SLOTS * 4]; } Index;

static void index_read(Index* x) {
    memcpy(x->names, wd_host_range(SLOT_NAMES, sizeof x->names, 0), sizeof x->names);
    memcpy(x->prot, wd_host_range(SLOT_PROTECTED, sizeof x->prot, 0), sizeof x->prot);
    memcpy(x->rec, wd_host_range(SLOT_RECENCY, sizeof x->rec, 0), sizeof x->rec);
    memcpy(x->files, wd_host_range(SLOT_FILES, sizeof x->files, 0), sizeof x->files);
}
static void index_write(const Index* x) {
    memcpy(wd_host_range(SLOT_NAMES, sizeof x->names, 1), x->names, sizeof x->names);
    memcpy(wd_host_range(SLOT_PROTECTED, sizeof x->prot, 1), x->prot, sizeof x->prot);
    memcpy(wd_host_range(SLOT_RECENCY, sizeof x->rec, 1), x->rec, sizeof x->rec);
    memcpy(wd_host_range(SLOT_FILES, sizeof x->files, 1), x->files, sizeof x->files);
}

static int rd(uint32_t va) { return (int)WD_HOST_READ32(va); }
static void slot_name(int i, char out[0x17]) { guest_str(SLOT_NAMES + 0x16u * (uint32_t)i, out, 0x17); }

/* The owner's slot: the first empty one, else the least recent unprotected. */
static int choose_slot(void) {
    int best = -1;
    for (int i = 0; i < SLOTS; i++) {
        char n[0x17];
        slot_name(i, n);
        if (!n[0]) return i;
    }
    for (int i = 0; i < SLOTS; i++) {
        if (rd(SLOT_PROTECTED + 4u * (uint32_t)i)) continue;
        if (best < 0 || rd(SLOT_RECENCY + 4u * (uint32_t)i) < rd(SLOT_RECENCY + 4u * (uint32_t)best)) best = i;
    }
    return best;   /* at most five slots are protected (MENU_HandleSaveSlotInput) */
}

int dev_save_page_ready(const char** why) {
    if (rd(EDITOR_FLAG)) { *why = "the editor is on"; return 0; }
    if ((uint32_t)rd(GAME_HANDLER) != GAME_TICK_FRAME || !rd(PLAYER + 0x30) || !rd(PROJECT_PTR)) { *why = "no level is playing"; return 0; }
    if (rd(VIDEO_ON) || rd(VIDEO_CUT)) { *why = "a video plays"; return 0; }
    if (rd(LOADING) || rd(FADE_TIMER) > 0) { *why = "a level loads or fades"; return 0; }
    if (rd(QUIT)) { *why = "the game is quitting"; return 0; }
    if (rd(DEMO_MODE) != 2) { *why = "a demo records or plays"; return 0; }
    return 1;
}

/* Served from wd_editor_frame (dev_tools.c). Returns when the menu closes. */
void dev_save_page_run(void (*say)(const char* fmt, ...)) {
    const char* why = "";
    if (!dev_save_page_ready(&why)) { say("Save page: not now (%s)", why); return; }
    int slot = choose_slot();
    if (slot < 0) { say("Save page: no slot can take a save"); return; }
    Index before;
    index_read(&before);
    char old[0x17];
    slot_name(slot, old);
    int save_mode = rd(SEL_SAVE_MODE);

    /* 0x4313b5's stores, then MENU_InitSaveSlotSelect(1)'s for our slot */
    WD_HOST_WRITE32(PAGE) = 2;
    WD_HOST_WRITE32(PAGE_SAVE) = 1;
    WD_HOST_WRITE32(PAGE_F) = 1;
    WD_HOST_WRITE32(PAGE_B) = 1;
    WD_HOST_WRITE32(PAGE_HELP_TIME) = 0x14;
    WD_HOST_WRITE32(SEL_SLOT) = (uint32_t)slot;
    guest_strcpy_out(SLOT_TITLE, 0x16, old);
    WD_HOST_WRITE8(SLOT_NAMES + 0x16u * (uint32_t)slot) = 0;   /* host-only: a fresh title */
    guest_call_regs(LOAD_ICON, SLOT_NAMES + 0x16u * (uint32_t)slot, 0, 0, 0);
    WD_HOST_WRITE32(SEL_CLOSED) = 0;
    WD_HOST_WRITE32(SEL_DONE) = 0;
    WD_HOST_WRITE32(SEL_DIRTY) = 1;
    WD_HOST_WRITE32(SEL_55) = 0;
    WD_HOST_WRITE32(PAGE_STATUS) = rd(SLOT_PROTECTED + 4u * (uint32_t)slot) ? 1 : 2;
    WD_HOST_WRITE32(SEL_SAVE_MODE) = 1;
    fprintf(stderr, "[tools] keypad 0 Save page: slot %d (%s)\n", slot, old[0] ? old : "empty");

    g_typedn = 0;
    g_open = 1;
    guest_call_regs(MENU_RUN, 0, 0, 0, 0);
    g_open = 0;
    g_typedn = 0;
    WD_HOST_WRITE16(KEY_CODE) = 0;
    WD_HOST_WRITE32(SEL_SAVE_MODE) = (uint32_t)save_mode;

    int status = rd(PAGE_STATUS);
    if (status != 4) {   /* Esc, or no title: the slot keeps its title */
        char now[0x17];
        slot_name(slot, now);
        if (strcmp(now, old)) guest_strcpy_out(SLOT_NAMES + 0x16u * (uint32_t)slot, 0x16, old);
        say("Save page closed: nothing saved");
        return;
    }
    /* GAME_SaveIndex sorted the index: the new title is the most recent */
    int k = 0;
    for (int i = 1; i < SLOTS; i++)
        if (rd(SLOT_RECENCY + 4u * (uint32_t)i) > rd(SLOT_RECENCY + 4u * (uint32_t)k)) k = i;
    char title[0x17];
    slot_name(k, title);
    for (int i = 0; i < SLOTS; i++) {
        char other[0x17];
        slot_name(i, other);
        if (i != k && other[0] && !strcmp(other, title)) {
            index_write(&before);
            int w = (int)guest_call_regs(SAVE_INDEX, 0, 0, 0, 0);
            say("Save refused: another save is called \"%s\"%s", title, w ? " (index not written back)" : "");
            return;
        }
    }
    if (!g_title) g_title = shim_alloc(0x20, 4);
    guest_strcpy_out(g_title, 0x17, title);
    int g = (int)guest_call_regs(SAVE_GAME, g_title, 0, 0, 0);
    int x = (int)guest_call_regs(SAVE_INDEX, 0, 0, 0, 0);
    int t = (int)guest_call_regs(SAVE_THUMBNAIL, g_title, 0, 0, 0);
    if (g || x || t) say("Save \"%s\": failed (game %d, index %d, icon %d)", title, g, x, t);
    else {
        int file = -1;
        for (int i = 0; i < SLOTS; i++) {
            char n[0x17];
            slot_name(i, n);
            if (!strcmp(n, title)) { file = rd(SLOT_FILES + 4u * (uint32_t)i); break; }
        }
        say("Saved \"%s\" (game%d.dat)", title, file);
    }
}

/* Rule 6: the page takes the typed text. */
int dev_save_page_typing(void) {
    return g_open && !rd(SEL_CLOSED) && (uint32_t)rd(GAME_HANDLER) == CTRL_DISPATCHER;
}

void dev_save_page_type(uint8_t c) {
    if (c >= 0x20 && c < 0x7F && g_typedn < (int)sizeof g_typed) g_typed[g_typedn++] = c;
}

/* The PeekMessageA bridge: one character per menu frame; and the menu closed
 * once the page has (host-only). Retail's save mode never closes it: after the
 * confirm (0x4a2f35 = 0x4a2f49 = 1) MENU_HandleSystemPageInput has no branch
 * for that state and Esc no longer reaches the menu (0x4a155f stays 1); after
 * Esc it falls back to the system list with the cursor on the hidden Save
 * item. Keypad 0 opened the page alone, so the menu ends with it, as
 * MENU_HandleGameMenuInput's Esc ends it (0x4a1533 = 1, 0x4a153b = 0x4a155f = 0). */
void dev_save_page_pump(void) {
    if (g_open && rd(SEL_CLOSED) && (uint32_t)rd(GAME_HANDLER) == CTRL_DISPATCHER && !rd(MENU_EXIT)) {
        WD_HOST_WRITE32(MENU_EXIT) = 1;
        WD_HOST_WRITE32(PAGE_B) = 0;
        WD_HOST_WRITE32(PAGE_F) = 0;
    }
    if (!g_typedn || !dev_save_page_typing()) return;
    uint32_t q = WD_HOST_READ32(GAME_QUEUE), now = WD_HOST_READ32(MENU_TIMER);
    if (!q || now == g_last_timer) return;
    g_last_timer = now;
    guest_call_regs(POST_MESSAGE, q, 0x33, g_typed[0], 0);
    memmove(g_typed, g_typed + 1, (size_t)--g_typedn);
}

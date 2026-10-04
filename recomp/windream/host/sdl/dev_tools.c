/*
 * WINDREAM recompilation - Cryo's developer tools in Develop
 * (docs/specs/008-editor-restoration/spec.md, phase 7; research brief
 * out/research/phase7-brief/BRIEF.md).
 *
 * Each tool is Cryo's code reached by a host call or a data write; the
 * structure around it is host-only and labelled so. dev_keys.c latches the
 * key (phase 2); the work is served here, from the inserted editor call
 * (user.c wd_editor_frame, lift.py CALLS 0x41743a), which runs once per
 * gameplay frame of GAME_TickFrame after GAME_Tick, the HUD and the readout,
 * before GAME_HandleHotkeys: the context of retail's own L key. Nothing here
 * runs in Play or Play edits.
 *
 *   capture (6, 7; keypad 4 is the same flag): SaveImage_ (0x4479c7), the
 *     test WorksEdit_ (0x44d46d) makes in its first block, made here while the
 *     editor is off (while it is on, WorksEdit_ makes it). Not by calling
 *     0x44d46d with the editor off: that also runs ShowBox_, which draws a
 *     working BOX left over from an editor session. The writer does not check
 *     fopen_, so DATA\TGA is made in the developer folder at Develop start
 *     (the folder copy copies files, not the disc's empty directories).
 *   give all items (keypad 6): the retail flag 0x49d5e0 (GAME_TickFrame adds
 *     one of the 14 names per frame, 0x4170e4); host-only guards before it and
 *     a restore of the held entries after it (below).
 *   collision views (keypad 7): Cryo's Display_Collision_Sphere_ (0x45ea00)
 *     and Display_Overlap_Sphere_ (0x45f104) for every collider of the
 *     collision world 0x66e01c (pointers +0x10, count +0x40c), after the
 *     render. No retail or July build calls them (host-only call sites).
 *   profiler (keypad 8): Cryo's bar and text (Display_Info_Timer_ 0x4997f8,
 *     info_timer_text_ 0x499874, 3DC_PROF.C, uncalled) over banks the host
 *     fills (0x68117c): host-only timing of four coarse stages, ours, not
 *     July's (July timed Render_, Morph_Obj_* and the sphere-mesh queries):
 *     bank 0 the frame, 1 the 3D render (REND_DrawFrame), 2 the entities
 *     (ENT_TickAll), 3 collision (PHYS_ResolveCollisions); lift.py CALLS
 *     around the three calls. cpu_init_lib_ is not used (it waits in vbl_),
 *     the running flags stay 0 (a 1 makes Display_Info_Timer_ call getch_)
 *     and nothing is drawn until the frame bank is above 0.
 *   console window (keypad 9): dev_console.c, with the guest's standard
 *     output teed into it (files.c wd_std_tee); on each opening Cryo's dump
 *     helpers Scan_Mem_ (0x458384) and PrintMisEntry_ (0x43afcd) print once.
 *   free camera (-): the turn deltas 0x4a3154/0x4a3158 are written only by
 *     the mouse and joystick events and never cleared, so after the mouse
 *     stops the camera would keep turning at the last delta: while the camera
 *     is free (mode byte 0x52c874 == 7) and the editor off, they are zeroed
 *     after the frame has used them (the effect of a zero-delta event 0x34,
 *     without a second event in the queue that could overwrite a real one).
 *     user.c turns SDL's relative mouse mode on meanwhile.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RECOMP_GENERATED_CODE
#include "host.h"
#include "render_boundary.h"

#define EDITOR_FLAG     0x004A477Cu
#define GAME_HANDLER    0x00626F74u
#define GAME_TICK_FRAME 0x00416D45u
#define PLAYER          0x004FBA78u   /* the player's actor; +0x30 its inventory */
#define PROJECT_PTR     0x00661E04u
#define LOADING         0x00661E08u   /* DDAT_Load, GAME_LoadGame */
#define VIDEO_ON        0x0049D5B4u
#define VIDEO_CUT       0x0049D5B8u
#define FRAME_BUFFER    0x005E549Cu   /* RGB565 */
#define FRAME_WIDTH     0x0049D9FCu
#define FRAME_HEIGHT    0x0049DA00u
#define CAMERA_MODE     0x0052C874u   /* byte: 7 free (CAM_ToggleFree 0x40b386) */
#define TURN_X          0x004A3154u
#define TURN_Y          0x004A3158u

#define CAPTURE_EVERY   0x004A4758u   /* also pins the step at 2.0 (0x417268) */
#define CAPTURE_ONE     0x004A475Cu
#define SAVE_IMAGE      0x004479C7u   /* SaveImage_: data\tga\<scn>_%04d.tga */

#define GIVE_FLAG       0x0049D5E0u
#define GIVE_INDEX      0x0049D5E4u
#define GIVE_NAMES      0x0049DB12u   /* 14 names, 9 bytes apart */
#define GIVE_COUNT      14

#define WORLD           0x0066E01Cu   /* the collision world */
#define WORLD_COLLIDERS (WORLD + 0x10u)
#define WORLD_COUNT     (WORLD + 0x40Cu)
#define BEN_CAMERA      0x00661EE8u   /* the 3D engine's camera the draws project with */
#define SPHERE_DRAW     0x0045EA00u   /* Display_Collision_Sphere_(fb, collider, dot, line) */
#define OVERLAP_DRAW    0x0045F104u   /* Display_Overlap_Sphere_(fb, collider, colour) */

#define PROF_BANKS      0x0068117Cu   /* 8 x 16: +0 time (float), +0xc running (short), +0xe colour */
#define PROF_REFERENCE  0x00681200u   /* info_timer_text_ prints this / time */
#define PROF_WIDTH      0x00661EBCu   /* the 3D engine's screen width: bank 0's bar */
#define PROF_BAR        0x004997F8u
#define PROF_TEXT       0x00499874u

#define SCAN_MEM        0x00458384u
#define PRINT_MIS       0x0043AFCDu
#define MIS_COUNT       0x004A2F98u
#define MIS_TABLE       0x004A2F94u
#define OBJECT_TABLE    0x00661EE0u

#define DEMO_MODE       0x0049D34Au   /* 2 normal, 0 record, 1 play */
#define DEMO_RECORDING  0x0049D5DCu   /* July's key state; DEMO_RecordFrame flips it when the buffer fills */
#define DEMO_RECORD     0x0040DB80u   /* start recording (dead in retail) */
#define DEMO_SAVE       0x0040EDAFu   /* DEMO_SaveReplay: data\replay.bin, then quit to the title */
#define DEMO_REPLAY     0x0040EE88u   /* load data\replay.bin and play it (dead in retail) */
#define DEMO_STOP       0x0040DBF1u   /* DEMO_StopPlayback */
#define INPUT_MODE      0x0049D2F8u   /* byte; its saved copy is the next byte, 0x49d2f9 */
#define INSTALL_ROOT    0x004285BEu   /* FILE_GetInstallRoot: EAX char* */
#define FADE_TIMER      0x005E5480u
#define QUIT            0x004A4780u
#define MENU_RUN        0x004337C0u
#define TEXT_DRAW       0x0044D5C7u   /* TEXT_DrawString(EAX s, EDX x, EBX y, ECX 0x100, [esp+4] font), ret 4 */
#define REPLAY_RECORD   112u          /* the Windows record; the DOS build packed it in 104 */
#define REPLAY_MAX      6144u

static int g_collision, g_profiler, g_console;

static int rd(uint32_t va) { return (int)WD_HOST_READ32(va); }
static int in_level(void);

/* ---- messages: stderr, and about 3 s at the foot of the game frame ---- */
static uint32_t g_msg_va;
static int g_msg_frames;

static void say(const char* fmt, ...) {
    char text[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[tools] %s\n", text);
    if (!g_msg_va) g_msg_va = shim_alloc(128, 16);
    guest_strcpy_out(g_msg_va, 128, text);
    g_msg_frames = 90;
}

void dev_tools_say(const char* fmt, ...) {
    char text[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    say("%s", text);
}

static void message_draw(void) {
    if (g_msg_frames <= 0 || !g_msg_va) return;
    g_msg_frames--;
    uint32_t h = WD_HOST_READ32(FRAME_HEIGHT);
    wd_host_range(g_esp - 4, 4, 1);
    PUSH32(g_esp, 0);
    guest_call_regs(TEXT_DRAW, g_msg_va, 0x14, h > 40 && h <= 4096 ? h - 20 : 0x1AE, 0x100);
    g_esp += 4;
}

/* ---- MENU_RunGameMenu (0x4337c0), counted (host-only): a game menu is up ---- */
static int g_menu_depth;

static void menu_run(void) {
    recomp_func_t original = recomp_lookup_reference(MENU_RUN);
    if (!original) {
        fprintf(stderr, "[tools] FATAL: no reference MENU_RunGameMenu (0x4337c0); re-run lift.py\n");
        abort();
    }
    g_menu_depth++;
    original();   /* consumes the return address */
    g_menu_depth--;
}

/* ---- the demo recorder (r, R) ----
 * r: DEMO_StartRecord (0x40db80) and July's key state 0x49d5dc = 1, or, with
 * it set, DEMO_SaveReplay (0x40edaf), which writes data\replay.bin and returns
 * to the title; DEMO_RecordFrame does the same itself when the buffer fills.
 * R: data\replay.bin is played only when its size is 4 + n x 112 with n its
 * count, 1 to 6,144 (the shipped file is a DOS recording of 104-byte records);
 * the input mode byte is kept and put back when playback ends (Cryo's restore
 * leaves input dead after a caption, host-only fix), and when a game menu opens
 * during playback (the death page, where playback would hang) the host stops
 * it with DEMO_StopPlayback (host-only guard). */
static int g_replaying;
static uint8_t g_input_mode;
static uint32_t g_replay_path;

static const char* demo_not_now(void) {
    if (rd(EDITOR_FLAG)) return "the editor is on";
    if (!in_level()) return "no level is playing, or a load or video";
    if (rd(FADE_TIMER) > 0) return "a level fades";
    if (rd(QUIT)) return "the game is quitting";
    return NULL;
}

static void demo_record(void) {
    const char* why = demo_not_now();
    if (!why && rd(DEMO_MODE) == 1) why = "a demo plays";
    if (why) { say("r record: not now (%s)", why); return; }
    if (rd(DEMO_RECORDING) && rd(DEMO_MODE) == 0) {
        WD_HOST_WRITE32(DEMO_RECORDING) = 0;
        say("r record: stopped, writing DATA\\REPLAY.BIN (%u frames); back to the title", (unsigned)WD_HOST_READ32(0x0049D346u));
        guest_call_regs(DEMO_SAVE, 0, 0, 0, 0);
        return;
    }
    guest_call_regs(DEMO_RECORD, 0, 0, 0, 0);
    WD_HOST_WRITE32(DEMO_RECORDING) = 1;
    say("r record: recording (r again stops and writes DATA\\REPLAY.BIN)");
}

static void demo_replay(void) {
    const char* why = demo_not_now();
    if (!why && rd(DEMO_MODE) != 2) why = "a demo records or plays";
    if (why) { say("R replay: not now (%s)", why); return; }
    char path[300];
    guest_str(guest_call_regs(INSTALL_ROOT, 0, 0, 0, 0), path, 260);
    SDL_strlcat(path, "data\\replay.bin", sizeof path);
    if (!g_replay_path) g_replay_path = shim_alloc(300, 4);
    guest_strcpy_out(g_replay_path, 300, path);
    size_t n = 0;
    uint8_t* data = (uint8_t*)files_read_guest(g_replay_path, &n);
    uint32_t count = data && n >= 4 ? (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24 : 0;
    int found = data != NULL;
    free(data);
    if (!found) { say("R replay: no DATA\\REPLAY.BIN"); return; }
    if (n < 4 || (n - 4) % REPLAY_RECORD || count != (n - 4) / REPLAY_RECORD || count < 1 || count > REPLAY_MAX) {
        say("R replay: DATA\\REPLAY.BIN refused (%u bytes, count %u: not 4 + n x 112, n 1-6144)", (unsigned)n, (unsigned)count);
        return;
    }
    g_input_mode = WD_HOST_READ8(INPUT_MODE);
    g_replaying = 1;
    say("R replay: %u frames (Space stops)", (unsigned)count);
    guest_call_regs(DEMO_REPLAY, 0, 0, 0, 0);
}

/* Playback over: the input mode back as it was. */
static void demo_watch(void) {
    if (!g_replaying) return;
    if (rd(DEMO_MODE) == 1 && g_menu_depth > 0) {
        fprintf(stderr, "[tools] R replay: a game menu opened during playback; the host stops it\n");
        guest_call_regs(DEMO_STOP, 0, 0, 0, 0);
    }
    if (rd(DEMO_MODE) == 1) return;
    WD_HOST_WRITE8(INPUT_MODE) = g_input_mode;
    g_replaying = 0;
    fprintf(stderr, "[tools] R replay: playback over; input mode %u restored\n", g_input_mode);
}

/* A level is playing and nothing is loading or showing a video. */
static int in_level(void) {
    return WD_HOST_READ32(GAME_HANDLER) == GAME_TICK_FRAME && rd(PLAYER + 0x30) && rd(PROJECT_PTR) &&
           !rd(LOADING) && !rd(VIDEO_ON) && !rd(VIDEO_CUT);
}

/* ---- give all items (keypad 6) ----
 * ENT_AddInventoryItem (0x42a182) needs an empty entry even for a name held
 * already (it looks for one first, then matches with stricmp_), adds 1 to a
 * held entry's count (+0x314), raises its level (+0x294) with the count, and
 * overwrites its name with the give-all's lower-case spelling, which
 * ENT_HasInventoryItem (case-sensitive) then misses. So the host acts only
 * when the empty entries cover the missing names plus one if any is held,
 * remembers each held entry and the level of each hotkey bound to one, and
 * puts them back when the flag reads 0 again (host-only structure). */
typedef struct { int index; char name[16]; uint32_t count, level; } Held;
static Held g_held[GIVE_COUNT];
static int g_heldn, g_giving;
static uint32_t g_give_inv, g_hot_index[3], g_hot_level[3];
static const uint32_t g_hot_level_at[3] = {0xE0, 0xD8, 0xE8};   /* the player's hotkey levels, slots 0-2 */

static void give_name(int i, char out[16]) {
    memset(out, 0, 16);
    for (int k = 0; k < 9; k++) out[k] = (char)WD_HOST_READ8(GIVE_NAMES + 9u * (uint32_t)i + (uint32_t)k);
    out[8] = 0;
}

static void inv_name(uint32_t inv, int e, char out[17]) {
    for (int k = 0; k < 16; k++) out[k] = (char)WD_HOST_READ8(inv + 4u + 16u * (uint32_t)e + (uint32_t)k);
    out[16] = 0;
}

static void give_all(void) {
    if (!in_level() || rd(GIVE_FLAG) || g_giving) {
        fprintf(stderr, "[tools] keypad 6 give all items: not now (no level, a load or video, or already giving)\n");
        return;
    }
    uint32_t inv = WD_HOST_READ32(PLAYER + 0x30);
    int empty = 0, missing = 0;
    g_heldn = 0;
    for (int e = 0; e < 32; e++)
        if (!WD_HOST_READ8(inv + 4u + 16u * (uint32_t)e)) empty++;
    for (int i = 0; i < GIVE_COUNT; i++) {
        char want[16], have[17];
        give_name(i, want);
        int found = -1;
        for (int e = 0; e < 32 && found < 0; e++) {
            inv_name(inv, e, have);
            if (have[0] && !SDL_strcasecmp(have, want)) found = e;
        }
        if (found < 0) { missing++; continue; }
        Held* h = &g_held[g_heldn++];
        h->index = found;
        memcpy(h->name, have, 16);
        h->count = WD_HOST_READ32(inv + 0x314u + 4u * (uint32_t)found);
        h->level = WD_HOST_READ32(inv + 0x294u + 4u * (uint32_t)found);
    }
    int need = missing + (g_heldn ? 1 : 0);
    if (empty < need) {
        fprintf(stderr, "[tools] keypad 6 give all items: refused, %d empty entries for %d missing names%s\n",
                empty, missing, g_heldn ? " and one more for the held ones" : "");
        g_heldn = 0;
        return;
    }
    for (int k = 0; k < 3; k++) {
        g_hot_index[k] = WD_HOST_READ32(inv + 0x208u + 4u * (uint32_t)k);
        g_hot_level[k] = WD_HOST_READ32(PLAYER + g_hot_level_at[k]);
    }
    g_give_inv = inv;
    g_giving = 1;
    WD_HOST_WRITE32(GIVE_FLAG) = 1;
    fprintf(stderr, "[tools] keypad 6 give all items: %d missing, %d held (restored afterwards)\n", missing, g_heldn);
}

static void give_all_frame(void) {
    if (!g_giving) return;
    uint32_t inv = WD_HOST_READ32(PLAYER + 0x30);
    if (inv != g_give_inv) {   /* New Game or a load: the inventory is another */
        g_giving = 0;
        fprintf(stderr, "[tools] give all items: the inventory changed; held entries not restored\n");
        return;
    }
    if (rd(GIVE_FLAG)) return;
    for (int j = 0; j < g_heldn; j++) {
        const Held* h = &g_held[j];
        uint32_t e = (uint32_t)h->index;
        for (uint32_t k = 0; k < 16; k++) WD_HOST_WRITE8(inv + 4u + 16u * e + k) = (uint8_t)h->name[k];
        WD_HOST_WRITE32(inv + 0x314u + 4u * e) = h->count;
        WD_HOST_WRITE32(inv + 0x294u + 4u * e) = h->level;
        for (int k = 0; k < 3; k++)
            if (g_hot_index[k] == e && WD_HOST_READ32(inv + 0x208u + 4u * (uint32_t)k) == e)
                WD_HOST_WRITE32(PLAYER + g_hot_level_at[k]) = g_hot_level[k];
    }
    g_giving = 0;
    fprintf(stderr, "[tools] give all items: done, %d held entries restored\n", g_heldn);
}

/* ---- the profiler (keypad 8): lift.py CALLS around three stages ---- */
enum { ST_RENDER, ST_ENTITIES, ST_COLLISION, ST_N };
static uint64_t g_stage_start[ST_N], g_stage_ticks[ST_N], g_last_frame;
static float g_smooth[ST_N + 1];

static void stage_begin(int s) { if (g_profiler) g_stage_start[s] = SDL_GetPerformanceCounter(); }
static void stage_end(int s) {
    if (g_profiler && g_stage_start[s]) g_stage_ticks[s] += SDL_GetPerformanceCounter() - g_stage_start[s];
    g_stage_start[s] = 0;
}
void wd_prof_render_begin(void) { stage_begin(ST_RENDER); }
void wd_prof_render_end(void) { stage_end(ST_RENDER); }
void wd_prof_entities_begin(void) { stage_begin(ST_ENTITIES); }
void wd_prof_entities_end(void) { stage_end(ST_ENTITIES); }
void wd_prof_collision_begin(void) { stage_begin(ST_COLLISION); }
void wd_prof_collision_end(void) { stage_end(ST_COLLISION); }

static void profiler_draw(void) {
    uint64_t now = SDL_GetPerformanceCounter();
    double freq = (double)SDL_GetPerformanceFrequency();
    float sample[ST_N + 1];
    sample[0] = g_last_frame ? (float)((double)(now - g_last_frame) / freq) : 0.0f;
    for (int s = 0; s < ST_N; s++) sample[s + 1] = (float)((double)g_stage_ticks[s] / freq);
    g_last_frame = now;
    memset(g_stage_ticks, 0, sizeof g_stage_ticks);
    for (int b = 0; b <= ST_N; b++) g_smooth[b] = g_smooth[b] ? g_smooth[b] * 0.9f + sample[b] * 0.1f : sample[b];
    if (!(g_smooth[0] > 0.0f)) return;   /* bank 0 divides every width */
    static const uint16_t colour[8] = {0x7BEF, 0xF800, 0x07E0, 0x001F, 0, 0, 0, 0};   /* grey, red, green, blue */
    for (uint32_t b = 0; b < 8; b++) {
        uint32_t at = PROF_BANKS + 16u * b;
        float t = b <= ST_N ? g_smooth[b] : 0.0f;
        memcpy(wd_host_range(at, 4, 1), &t, 4);
        WD_HOST_WRITE32(at + 4) = 0;
        WD_HOST_WRITE32(at + 8) = 0;
        WD_HOST_WRITE16(at + 0xC) = 0;   /* running 1 would make the bar call getch_ */
        WD_HOST_WRITE16(at + 0xE) = colour[b];
    }
    float one = 1.0f;   /* the text is 1 / the frame time: frames per second */
    memcpy(wd_host_range(PROF_REFERENCE, 4, 1), &one, 4);
    uint32_t fb = WD_HOST_READ32(FRAME_BUFFER), w = WD_HOST_READ32(FRAME_WIDTH), h = WD_HOST_READ32(FRAME_HEIGHT);
    if (!fb || !w || h < 16 || !WD_HOST_READ32(PROF_WIDTH)) return;
    /* Cryo's layout: the frame fills a row, the stages follow on the next.
     * Four such pairs make the bar 8 pixels high (host-only thickness). */
    for (uint32_t r = 0; r < 8; r += 2) guest_call_regs(PROF_BAR, fb + r * w * 2u, 0, 0, 0);
    guest_call_regs(PROF_TEXT, fb, 0, 0, 0);
}

/* ---- collision views (keypad 7) ---- */
static void collision_draw(void) {
    uint32_t fb = WD_HOST_READ32(FRAME_BUFFER), n = WD_HOST_READ32(WORLD_COUNT);
    if (!fb || !WD_HOST_READ32(BEN_CAMERA) || n > 255) return;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = WD_HOST_READ32(WORLD_COLLIDERS + 4u * i);
        if (!c) continue;
        guest_call_regs(SPHERE_DRAW, fb, c, 0xFFE0u, 0xF800u);   /* yellow centre, red wall boxes */
        guest_call_regs(OVERLAP_DRAW, fb, c, 0x07FFu, 0);        /* cyan floor faces */
    }
}

/* ---- the console window (keypad 9) ---- */
int dev_console_open(const char** how);
void dev_console_close(void);
void dev_console_write(const void* data, unsigned n);
void dev_console_print(const char* text);

static void console_tee(const void* data, uint32_t n) { dev_console_write(data, n); }

static void console_toggle(void) {
    if (g_console) {
        wd_std_tee = NULL;
        dev_console_close();
        g_console = 0;
        fprintf(stderr, "[tools] keypad 9 console window: closed\n");
        return;
    }
    const char* how = "";
    if (!dev_console_open(&how)) {
        fprintf(stderr, "[tools] keypad 9 console window: not opened (%s)\n", how);
        return;
    }
    g_console = 1;
    wd_std_tee = console_tee;
    fprintf(stderr, "[tools] keypad 9 console window: %s\n", how);
    dev_console_print("Dreams to Reality, Develop: the game's own output (keypad 9 closes this)\r\n");
    for (size_t i = 0; i < dev_keys_count(); i++) {
        char line[160];
        snprintf(line, sizeof line, "  %-20s %s\r\n", dev_keys_entry(i, 0), dev_keys_entry(i, 1));
        dev_console_print(line);
    }
    /* Cryo's dump helpers, once per opening: the 3D engine's object list and
     * the message entries (printf_, so through the tee). */
    if (in_level()) {
        uint32_t list = WD_HOST_READ32(OBJECT_TABLE);
        if (list && WD_HOST_READ32(list)) guest_call_regs(SCAN_MEM, 0, 0, 0, 0);
        if (rd(MIS_COUNT) > 0 && WD_HOST_READ32(MIS_TABLE)) guest_call_regs(PRINT_MIS, 0, 0, 0, 0);
    }
}

/* ---- requests (dev_keys.c) and the frame (user.c) ---- */
void dev_tools_request(int tool) {
    switch (tool) {
    case DEV_TOOL_CAPTURE_EVERY: {
        uint32_t v = WD_HOST_WRITE32(CAPTURE_EVERY) = !WD_HOST_READ32(CAPTURE_EVERY);
        fprintf(stderr, "[tools] 6 capture every frame: [0x%08X] = %u\n", CAPTURE_EVERY, v);
        break;
    }
    case DEV_TOOL_CAPTURE_ONE:
        WD_HOST_WRITE32(CAPTURE_ONE) = 1;
        fprintf(stderr, "[tools] 7 capture one frame\n");
        break;
    case DEV_TOOL_GIVE_ALL: give_all(); break;
    case DEV_TOOL_COLLISION:
        g_collision = !g_collision;
        fprintf(stderr, "[tools] keypad 7 collision views: %s\n", g_collision ? "on" : "off");
        break;
    case DEV_TOOL_PROFILER:
        g_profiler = !g_profiler;
        g_last_frame = 0;
        memset(g_smooth, 0, sizeof g_smooth);
        memset(g_stage_ticks, 0, sizeof g_stage_ticks);
        fprintf(stderr, "[tools] keypad 8 profiler: %s\n", g_profiler ? "on" : "off");
        break;
    case DEV_TOOL_CONSOLE: console_toggle(); break;
    case DEV_TOOL_SAVE_PAGE: dev_save_page_run(say); break;
    case DEV_TOOL_RECORD: demo_record(); break;
    case DEV_TOOL_REPLAY: demo_replay(); break;
    }
}

/* The PeekMessageA bridge in Develop, also while a menu or the title runs
 * (wd_editor_frame does not): the Save page's characters, the end of a
 * playback. */
void dev_tools_pump(void) {
    dev_save_page_pump();
    demo_watch();
}

/* Before the editor call: captures of the game frame, the give-all restore,
 * the free camera's deltas. */
void dev_tools_frame_begin(void) {
    give_all_frame();
    int editor = WD_HOST_READ32(EDITOR_FLAG) != 0;
    if (!editor && (rd(CAPTURE_EVERY) == 1 || rd(CAPTURE_ONE) == 1)) {
        if (in_level()) {
            dev_overlay_materialize();   /* direct renderer: the frame SaveImage_ reads (dev_overlay.c) */
            guest_call_regs(SAVE_IMAGE, 0, 0, 0, 0);
        }
        WD_HOST_WRITE32(CAPTURE_ONE) = 0;
    }
    if (!editor && WD_HOST_READ8(CAMERA_MODE) == 7) {
        WD_HOST_WRITE32(TURN_X) = 0;
        WD_HOST_WRITE32(TURN_Y) = 0;
    }
}

/* After the editor call: the overlays, over everything else. */
void dev_tools_frame_end(void) {
    int collision = g_collision && in_level();
    if (collision || g_profiler) {
        dev_overlay_begin(0);   /* their CPU pixels under the direct renderer (dev_overlay.c) */
        if (collision) collision_draw();
        if (g_profiler) profiler_draw();
        dev_overlay_end();
    }
    message_draw();
}

/* Relative mouse mode for user.c: the free camera turns with the mouse. */
int dev_tools_mouse_relative(void) {
    return host_develop() && !WD_HOST_READ32(EDITOR_FLAG) && WD_HOST_READ32(GAME_HANDLER) == GAME_TICK_FRAME &&
           WD_HOST_READ8(CAMERA_MODE) == 7;
}

/* Develop start (host_mode_install): MENU_RunGameMenu counted, DATA\TGA in
 * the developer folder. */
void dev_tools_install(void) {
    if (!wd_install_replacement(MENU_RUN, menu_run))
        fprintf(stderr, "[tools] WARNING: cannot replace MENU_RunGameMenu (0x4337c0): playback is not stopped on a menu\n");
    const char* tree = host_env("WD_TREE");
    if (!tree) return;
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/DATA/TGA", tree);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(dir, &info) && info.type == SDL_PATHTYPE_DIRECTORY) return;
    if (SDL_CreateDirectory(dir)) fprintf(stderr, "[tools] made %s for the TGA capture\n", dir);
    else fprintf(stderr, "[tools] cannot make %s: the TGA capture would crash (%s)\n", dir, SDL_GetError());
}

/*
 * WINDREAM recompilation - the keyboard of Develop: DOS keys
 * (docs/specs/008-editor-restoration/spec.md, phase 2).
 *
 * Cryo's tools were written for the DOS build, which read commands as typed
 * characters (_clavierChar, code page 850) and held controls from the key
 * state table. Retail Windows reads only virtual keys: INPUT_PollKeyboard
 * (0x440757) polls GetAsyncKeyState into the state table 0x6308d8, and
 * INPUT_PostEvents (0x42493b) posts each new press as event 0x33 with the
 * virtual key, which GAME_TickFrame (0x416d45) stores as the frame's key code
 * 0x626fd8 (word). Two readers share that code in the same frame, one after
 * the other at the inserted editor call (lift.py CALLS, 0x41743a):
 *   - the editor (0x44d46d): sceneKeyboard_ (0x44c28c) looks the code up in a
 *     table of DOS characters re-based by 0x21 (0x44c1f7: A Z E R T, Q S D F G,
 *     W X C V B, 1-6, ? . / : % ! and 0xf5 0xe6), the leaf editor tests '0'
 *     (0x44d0e2), the pickers Esc and Space (0x449274...);
 *   - GAME_HandleHotkeys (0x415aa7), right after: Esc and Space stop a video,
 *     J, K, P, and 0x97 (the object page); the rest from the state table.
 * Read as virtual keys, the editor's characters are accidents: 'a' is VK 'A'
 * (create a project), Delete is '.' (paste a project), the left arrow '%'.
 *
 * So in Develop the host keeps the two apart (host-only structure):
 *   - Text input is on. A key press waits until the pump has drained (SDL
 *     reports the key, then the character it typed); the character, mapped to
 *     CP850, decides whether the host consumes the key. A consumed key is
 *     hidden from the game: no state bit, so no event 0x33, and its repeats
 *     and release are dropped too. Any other key goes on as in Play.
 *   - The editor's characters wait in a queue; for the editor call alone the
 *     host writes the next one (or 0) into 0x626fd8 and puts the game's code
 *     back afterwards, so the editor sees only DOS characters and
 *     GAME_HandleHotkeys only virtual keys. One character per frame, as in DOS.
 *     (The spec's wording is "post event 0x33"; posting would leave the
 *     virtual keys of unconsumed keys, the arrows among them, in the editor's
 *     code.)
 *   - Developer keys are consumed in Develop, editor on or off; while the
 *     editor is on its commands win (A, D, 6, R). Their work is served at the
 *     next editor frame, in GAME_TickFrame's context (the GAME_HandleHotkeys
 *     call, where the L key opens the Load page), and only when a level plays;
 *     flag toggles are immediate data writes, as keypad 1-5 are (user.c).
 *   - The keypad never types (rule 3): its characters are dropped.
 *   - Ctrl+Shift+2/3/4 (by scan code, any layout) give the editor 0xf5, 0x21
 *     and 0xe6, the objet, link and box pastes; '!' itself only toggles.
 *   - Space and Esc go to the editor and not the game while a page is open:
 *     one of the WorksGetEditor_ semaphores (0x44c625's chain) is 1.
 *   - F10 with the editor on is the bank write (phase 5); off, the key help.
 * Play and Play edits never come here (user.c): their keys are as retail.
 *
 * The phase 7 tools are dev_tools.c (6, 7, r, R, keypad 6-9) and
 * dev_save_page.c (keypad 0, Cryo's Save page). While the Save page is open
 * (rule 6) the developer keys are suspended: typed ASCII 0x20-0x7e goes to its
 * title and only Backspace, Tab, Return and Esc reach the game's key state.
 * F10's bank write is editor_bank_write_disk (phase 5).
 */
#include <stdio.h>
#define RECOMP_GENERATED_CODE
#include "host.h"
#include "render_live.h"   /* wd_render_requested */
#include "../../../launcher/dev_keys.h"   /* the key list the launcher shows too */

#define EDITOR_FLAG      0x004A477Cu   /* _editor */
#define KEY_CODE         0x00626FD8u   /* word: the frame's last event 0x33 */
#define GAME_HANDLER     0x00626F74u
#define GAME_TICK_FRAME  0x00416D45u
#define POST_MESSAGE     0x0043B31Au   /* MGM_PostMessage(queue, type, a, b) */
#define SEND_MESSAGE     0x0043A306u   /* MGM_SendMessage(type, a, b) */
#define CAM_POST_MESSAGE 0x00409998u   /* CAM_PostMessage(type, a, b) into [0x52c850] */
#define CAM_QUEUE        0x0052C850u
#define HUD_QUEUE        0x00626F2Cu   /* MENJ_Dispatcher's queue: message 0x40 plays a dialogue */
#define REPLACE_MATERIAL 0x004577CCu   /* MDL_ReplaceMaterial(handle, from, to) */
#define SCENE_HANDLE     0x004FBDBCu   /* actor slot 2 +0x74: the level's model */
#define PROJECT_PTR      0x00661E04u   /* the current project record */
#define VIDEO_ON         0x0049D5B4u
#define VIDEO_CUT        0x0049D5B8u
#define READOUT          0x0049D5C0u   /* Frame Rate / Mem 3DTR (keypad 1 too) */
#define HUD_ON           0x0049D5D4u   /* _FlagAfficheInterface, initial 1 */
#define LOADING          0x00661E08u   /* DDAT_Load, GAME_LoadGame */
#define FADE_TIMER       0x005E5480u   /* float: an exit's fade */
#define DEMO_MODE        0x0049D34Au   /* 2 normal, 0 record, 1 play */
#define MOVIE_PREFIX     0x0041564Fu   /* the 32 bytes "data\hnm\" */

static void (*g_pass)(const SDL_KeyboardEvent*);   /* user.c: the Play path for a key not consumed */

/* ---- CP850 ----
 * The DOS build's characters (the July cases compare section 0xf5, micro 0xe6,
 * u grave 0x97: CP850, not CP437). Unicode of 0x80..0xff; checked against
 * Python's cp850 codec by tests/recomp/test_editor_keys.py. */
static const uint16_t g_cp850[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00F8, 0x00A3, 0x00D8, 0x00D7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x00AE, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x00C1, 0x00C2, 0x00C0, 0x00A9, 0x2563, 0x2551, 0x2557, 0x255D, 0x00A2, 0x00A5, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x00E3, 0x00C3, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x00A4,
    0x00F0, 0x00D0, 0x00CA, 0x00CB, 0x00C8, 0x0131, 0x00CD, 0x00CE, 0x00CF, 0x2518, 0x250C, 0x2588, 0x2584, 0x00A6, 0x00CC, 0x2580,
    0x00D3, 0x00DF, 0x00D4, 0x00D2, 0x00F5, 0x00D5, 0x00B5, 0x00FE, 0x00DE, 0x00DA, 0x00DB, 0x00D9, 0x00FD, 0x00DD, 0x00AF, 0x00B4,
    0x00AD, 0x00B1, 0x2017, 0x00BE, 0x00B6, 0x00A7, 0x00F7, 0x00B8, 0x00B0, 0x00A8, 0x00B7, 0x00B9, 0x00B3, 0x00B2, 0x25A0, 0x00A0,
};

/* The CP850 code of a Unicode character, 0 if CP850 has none (or it is a control). */
static uint8_t cp850(uint32_t u) {
    if (u >= 0x20 && u < 0x7F) return (uint8_t)u;
    for (int i = 0; i < 128; i++)
        if (g_cp850[i] == u) return (uint8_t)(0x80 + i);
    return 0;
}

/* The next code point of UTF-8 text, advancing *s; 0 at the end or on a bad byte. */
static uint32_t utf8_next(const char** s) {
    const uint8_t* p = (const uint8_t*)*s;
    uint32_t u;
    int n;
    if (!*p) return 0;
    if (*p < 0x80) { u = *p; n = 1; }
    else if ((*p & 0xE0) == 0xC0) { u = *p & 0x1Fu; n = 2; }
    else if ((*p & 0xF0) == 0xE0) { u = *p & 0x0Fu; n = 3; }
    else if ((*p & 0xF8) == 0xF0) { u = *p & 0x07u; n = 4; }
    else { *s += 1; return 0xFFFD; }
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *s += i; return 0xFFFD; }
        u = u << 6 | (p[i] & 0x3Fu);
    }
    *s += n;
    return u;
}

/* ---- the editor's characters ---- */
/* sceneKeyboard_'s table (0x44c1f7 + 0x21) without '!', which only toggles
 * (owner decision; paste link is Ctrl+Shift+3), and the leaf editor's '0'. */
static const uint8_t g_editor_chars[] = {
    0xF5, 0xE6, 'Z', 'X', 'W', 'V', 'T', 'S', 'R', 'Q', 'G', 'F', 'E', 'D', 'C', 'B', 'A',
    '?', ':', '6', '5', '4', '3', '2', '1', '/', '.', '%', '0',
};
static uint8_t g_editor_q[16];
static int g_editor_qn;

static int editor_on(void) { return WD_HOST_READ32(EDITOR_FLAG) != 0; }
static int in_level(void) { return WD_HOST_READ32(GAME_HANDLER) == GAME_TICK_FRAME; }

static int is_editor_char(uint8_t c) {
    for (size_t i = 0; i < sizeof g_editor_chars; i++)
        if (g_editor_chars[i] == c) return 1;
    return 0;
}

static void editor_put(uint8_t c) {
    if (!in_level()) { fprintf(stderr, "[keys] editor 0x%02X: ignored outside a level\n", c); return; }
    if (g_editor_qn < (int)sizeof g_editor_q) g_editor_q[g_editor_qn++] = c;
    fprintf(stderr, "[keys] editor 0x%02X\n", c);
}

/* A page of the editor is open: WorksGetEditor_ (0x44c625) serves the first of
 * these semaphores that is 1 and returns non-zero while it does. */
static const uint32_t g_semas[] = {
    0x4A4710, 0x4A4730, 0x4A46D8, 0x4A46DC, 0x4A46E4, 0x4A46E0, 0x4A4720, 0x4A4724, 0x4A4728, 0x4A472C,
    0x4A46C8, 0x4A46CC, 0x4A4718, 0x4A4714, 0x4A46D0, 0x4A46D4, 0x4A46EC, 0x4A46F0, 0x4A46F8, 0x4A46F4,
    0x4A470C, 0x4A46E8, 0x4A46FC, 0x4A4700, 0x4A4704, 0x4A4708, 0x4A4734, 0x4A4738, 0x4A471C, 0x4A46B8,
    0x4A46C4, 0x4A46C0, 0x4A46BC, 0x4A46B4, 0x4A4740, 0x4A4744, 0x4A4748, 0x4A474C,
};
static int page_open(void) {
    for (size_t i = 0; i < sizeof g_semas / sizeof g_semas[0]; i++)
        if (WD_HOST_READ32(g_semas[i]) == 1) return 1;
    return 0;
}

/* ---- developer work, served at the next editor frame ---- */
enum { ACT_FREE_CAMERA = 1, ACT_OVERHEAD, ACT_DIALOGUE, ACT_MOVIE, ACT_CLASS, ACT_OBJECT_PAGE, ACT_BANK_WRITE, ACT_TOOL };
typedef struct { int act; uint32_t from, to; } Act;
static Act g_act[16];
static int g_actn;

static void queue(int act, uint32_t from, uint32_t to, const char* what) {
    if (!in_level()) { fprintf(stderr, "[keys] %s: ignored outside a level\n", what); return; }
    if (g_actn < (int)(sizeof g_act / sizeof g_act[0])) g_act[g_actn++] = (Act){act, from, to};
    fprintf(stderr, "[keys] %s\n", what);
}

/* The same, served at the next editor frame even outside a level (the console). */
static void queue_any(int act, uint32_t from, const char* what) {
    if (g_actn < (int)(sizeof g_act / sizeof g_act[0])) g_act[g_actn++] = (Act){act, from, 0};
    fprintf(stderr, "[keys] %s\n", what);
}

static void toggle(uint32_t va, const char* what) {
    uint32_t v = WD_HOST_WRITE32(va) = !WD_HOST_READ32(va);
    fprintf(stderr, "[keys] %s: [0x%08X] = %u\n", what, va, v);
}

/* F10 with the editor on: the bank write of phase 5 (editor_bank.c, as July's
 * SaveDiskScene_), served in the frame like the other actions. */
static void bank_write(void) { queue(ACT_BANK_WRITE, 0, 0, "F10 write the bank"); }

/* The level's movie, as GAME_InitSubsystems plays the project's movie at
 * boot (0x415f18-0x41605d; spec 008 phase 7: 0x416015 is a block of that
 * function, not a callable level-entry sequence): the 32-byte prefix at
 * 0x41564f ("data\hnm\"), strcat of the project's +0x3c name, MGM 0x17
 * (open, 5), and if it opened MGM 0x18 (play) and the video flags. Esc or
 * Space stop it as any video; event 0x3f ends it. */
static void level_movie(void) {
    static uint32_t path;
    uint32_t rec = WD_HOST_READ32(PROJECT_PTR);
    if (!rec || !WD_HOST_READ8(rec + 0x3C) || WD_HOST_READ32(VIDEO_ON) || WD_HOST_READ32(VIDEO_CUT) ||
        WD_HOST_READ32(LOADING) || (int32_t)WD_HOST_READ32(FADE_TIMER) > 0 || WD_HOST_READ32(DEMO_MODE) != 2) {
        fprintf(stderr, "[keys] H: no movie to play now\n");
        return;
    }
    if (!path) path = shim_alloc(64, 4);
    uint32_t n = 0;
    for (; n < 32 && WD_HOST_READ8(MOVIE_PREFIX + n); n++) WD_HOST_WRITE8(path + n) = WD_HOST_READ8(MOVIE_PREFIX + n);
    for (uint32_t i = 0; i < 16 && n < 63 && WD_HOST_READ8(rec + 0x3C + i); i++, n++)
        WD_HOST_WRITE8(path + n) = WD_HOST_READ8(rec + 0x3C + i);
    WD_HOST_WRITE8(path + n) = 0;
    if (guest_call_regs(SEND_MESSAGE, 0x17, path, 5, 0)) {
        guest_call_regs(SEND_MESSAGE, 0x18, 0, 0, 0);
        WD_HOST_WRITE32(VIDEO_CUT) = 0;
        WD_HOST_WRITE32(VIDEO_ON) = 1;
        fprintf(stderr, "[keys] H: playing the level's movie\n");
    } else {
        fprintf(stderr, "[keys] H: the movie did not open\n");
    }
}

static void serve(int act, uint32_t from, uint32_t to) {
    switch (act) {
    case ACT_FREE_CAMERA:
    case ACT_OVERHEAD:
        if (WD_HOST_READ32(CAM_QUEUE)) guest_call_regs(CAM_POST_MESSAGE, act == ACT_FREE_CAMERA ? 0x31 : 0x30, 0, 0, 0);
        break;
    case ACT_DIALOGUE:
        if (WD_HOST_READ32(HUD_QUEUE)) guest_call_regs(POST_MESSAGE, WD_HOST_READ32(HUD_QUEUE), 0x40, 0, 0);
        break;
    case ACT_MOVIE: level_movie(); break;
    case ACT_CLASS:
        /* Classes 6 and 0x1c are drawn by the software rasterizer only: the
         * direct renderer follows the 3dfx build, whose Glide hook draws
         * nothing for them (render_scene_draw.cpp glide_no_draw), so the
         * level would vanish. Spec 008 phase D. */
        if (wd_render_requested()) { dev_tools_say("render classes: software renderer only"); break; }
        if (WD_HOST_READ32(SCENE_HANDLE)) guest_call_regs(REPLACE_MATERIAL, WD_HOST_READ32(SCENE_HANDLE), from, to, 0);
        break;
    case ACT_OBJECT_PAGE: WD_HOST_WRITE16(KEY_CODE) = 0x97; break;   /* as event 0x33 would set it, for GAME_HandleHotkeys */
    case ACT_BANK_WRITE: editor_bank_write_disk("F10"); break;
    case ACT_TOOL: dev_tools_request((int)from); break;
    }
}

/* ---- deciding a key ---- */

/* A typed character. 1 if the host takes it. */
static int take_char(uint8_t c, int editor) {
    if (c == '!') { toggle(EDITOR_FLAG, "! editor"); return 1; }
    if (editor && is_editor_char(c)) { editor_put(c); return 1; }
    switch (c) {   /* developer keys; with the editor on, A D 6 R were its commands above */
    case '-':
        if (editor) fprintf(stderr, "[keys] -: inert while the editor is on\n");
        else queue(ACT_FREE_CAMERA, 0, 0, "- free camera");
        return 1;
    case '9': queue(ACT_OVERHEAD, 0, 0, "9 overhead camera"); return 1;
    case '8': toggle(READOUT, "8 readout"); return 1;
    case '6': dev_tools_request(DEV_TOOL_CAPTURE_EVERY); return 1;
    case '7': dev_tools_request(DEV_TOOL_CAPTURE_ONE); return 1;
    case 'A': toggle(HUD_ON, "A HUD"); return 1;
    case 'D': queue(ACT_DIALOGUE, 0, 0, "D dialogue test"); return 1;
    case 'H': queue(ACT_MOVIE, 0, 0, "H level movie"); return 1;
    case 'r':
        if (editor) fprintf(stderr, "[keys] r: the recorder needs the editor off\n");
        else queue(ACT_TOOL, DEV_TOOL_RECORD, 0, "r record a demo / stop and write it");
        return 1;
    case 'R':   /* with the editor on, Box Create (above) */
        queue(ACT_TOOL, DEV_TOOL_REPLAY, 0, "R replay DATA\\REPLAY.BIN");
        return 1;
    case 'e': queue(ACT_CLASS, 6, 3, "e render class 6 -> 3"); return 1;
    case 'f': queue(ACT_CLASS, 3, 0x1C, "f render class 3 -> 0x1c"); return 1;
    case 'l': queue(ACT_CLASS, 3, 6, "l render class 3 -> 6"); return 1;
    case 'v': queue(ACT_CLASS, 0x1C, 3, "v render class 0x1c -> 3"); return 1;
    case 0x97: queue(ACT_OBJECT_PAGE, 0, 0, "u grave object page"); return 1;
    default: return 0;
    }
}

static int keypad(SDL_Scancode sc) { return sc >= SDL_SCANCODE_KP_DIVIDE && sc <= SDL_SCANCODE_KP_PERIOD; }

/* A key press with the characters it typed (UTF-8, maybe empty; NULL key:
 * text with no press before it). 1 if the host takes it. */
static int take(const SDL_KeyboardEvent* k, const char* text) {
    int editor = editor_on();
    if (dev_save_page_typing()) {   /* rule 6: the Save page's title entry */
        if (k && (k->scancode == SDL_SCANCODE_BACKSPACE || k->scancode == SDL_SCANCODE_TAB ||
                  k->scancode == SDL_SCANCODE_RETURN || k->scancode == SDL_SCANCODE_ESCAPE))
            return 0;
        if (!(k && keypad(k->scancode)))
            for (const char* s = text; s && *s;) {
                uint32_t u = utf8_next(&s);
                if (u >= 0x20 && u < 0x7F) dev_save_page_type((uint8_t)u);
            }
        return 1;
    }
    if (k && keypad(k->scancode)) {   /* rule 3: never a character */
        switch (k->scancode) {
        case SDL_SCANCODE_KP_6: queue(ACT_TOOL, DEV_TOOL_GIVE_ALL, 0, "keypad 6 give all items"); return 1;
        case SDL_SCANCODE_KP_7: dev_tools_request(DEV_TOOL_COLLISION); return 1;
        case SDL_SCANCODE_KP_8: dev_tools_request(DEV_TOOL_PROFILER); return 1;
        case SDL_SCANCODE_KP_9: queue_any(ACT_TOOL, DEV_TOOL_CONSOLE, "keypad 9 console window"); return 1;
        case SDL_SCANCODE_KP_0: queue(ACT_TOOL, DEV_TOOL_SAVE_PAGE, 0, "keypad 0 Save page"); return 1;
        default: return 0;   /* keypad 1-5: user.c's debug_toggle; the rest as in Play */
        }
    }
    if (k && editor) {
        SDL_Keymod m = k->mod;
        if ((m & SDL_KMOD_CTRL) && (m & SDL_KMOD_SHIFT) && !(m & SDL_KMOD_ALT)) {
            uint8_t c = k->scancode == SDL_SCANCODE_2 ? 0xF5 : k->scancode == SDL_SCANCODE_3 ? 0x21
                      : k->scancode == SDL_SCANCODE_4 ? 0xE6 : 0;
            if (c) { editor_put(c); return 1; }
        }
        if (k->scancode == SDL_SCANCODE_F10) { bank_write(); return 1; }
        if ((k->scancode == SDL_SCANCODE_ESCAPE || k->scancode == SDL_SCANCODE_SPACE) && page_open()) {
            editor_put(k->scancode == SDL_SCANCODE_ESCAPE ? 0x1B : 0x20);
            return 1;
        }
    }
    int taken = 0;
    for (const char* s = text; s && *s;) {
        uint8_t c = cp850(utf8_next(&s));
        if (c) taken |= take_char(c, editor);
    }
    return taken;
}

/* ---- the pump ---- */
typedef struct { int has_key; SDL_KeyboardEvent key; char text[32]; } Item;
static Item g_items[64];
static int g_n;
static uint8_t g_consumed[SDL_SCANCODE_COUNT];

void dev_keys_init(void (*pass)(const SDL_KeyboardEvent*)) { g_pass = pass; }

void dev_keys_flush(void) {
    for (int i = 0; i < g_n; i++) {
        const Item* it = &g_items[i];
        if (!it->has_key) { take(NULL, it->text); continue; }
        const SDL_KeyboardEvent* k = &it->key;
        if ((unsigned)k->scancode >= SDL_SCANCODE_COUNT) continue;
        if (k->down) {
            if (g_consumed[k->scancode]) continue;               /* a consumed key's repeat */
            if (!k->repeat && take(k, it->text)) { g_consumed[k->scancode] = 1; continue; }
        } else if (g_consumed[k->scancode]) {
            g_consumed[k->scancode] = 0;                          /* and its release */
            continue;
        }
        if (g_pass) g_pass(k);
    }
    g_n = 0;
}

int dev_keys_take(const SDL_Event* e) {
    if (!host_develop()) return 0;
    if (e->type == SDL_EVENT_TEXT_INPUT) {
        Item* last = g_n ? &g_items[g_n - 1] : NULL;
        if (!last || !last->has_key || !last->key.down) {   /* text with no press before it */
            if (g_n == (int)(sizeof g_items / sizeof g_items[0])) dev_keys_flush();
            last = &g_items[g_n++];
            memset(last, 0, sizeof *last);
        }
        SDL_strlcat(last->text, e->text.text ? e->text.text : "", sizeof last->text);
        return 1;
    }
    if (e->type != SDL_EVENT_KEY_DOWN && e->type != SDL_EVENT_KEY_UP) return 0;
    if (g_n == (int)(sizeof g_items / sizeof g_items[0])) dev_keys_flush();
    Item* it = &g_items[g_n++];
    memset(it, 0, sizeof *it);
    it->has_key = 1;
    it->key = e->key;
    return 1;
}

void dev_keys_release_all(void) {
    memset(g_consumed, 0, sizeof g_consumed);
    g_n = 0;
}

int dev_keys_editor_letters(void) { return host_develop() && editor_on(); }

void dev_keys_start(SDL_Window* window) {
    if (!host_develop() || !window) return;
    if (!SDL_StartTextInput(window)) fprintf(stderr, "[keys] SDL_StartTextInput: %s\n", SDL_GetError());
}

size_t dev_keys_count(void) { return WD_DEV_KEY_COUNT; }
const char* dev_keys_entry(size_t i, int column) { return i < WD_DEV_KEY_COUNT ? wd_dev_keys[i][column ? 1 : 0] : ""; }

void dev_keys_print(void) {
    printf("Dreams to Reality, Develop: the keys (Cryo's F10 key help stays the game's)\n");
    for (size_t i = 0; i < WD_DEV_KEY_COUNT; i++) printf("  %-20s %s\n", wd_dev_keys[i][0], wd_dev_keys[i][1]);
    fflush(stdout);
}

/* ---- the frame (user.c wd_editor_frame) ---- */
void dev_keys_frame(void) {
    for (int i = 0; i < g_actn; i++) serve(g_act[i].act, g_act[i].from, g_act[i].to);
    g_actn = 0;
    if (!editor_on()) g_editor_qn = 0;
}

uint16_t dev_keys_editor_enter(void) {
    uint16_t game = WD_HOST_READ16(KEY_CODE);
    uint16_t c = 0;
    if (g_editor_qn) {
        c = g_editor_q[0];
        memmove(g_editor_q, g_editor_q + 1, (size_t)--g_editor_qn);
    }
    WD_HOST_WRITE16(KEY_CODE) = c;
    return game;
}

void dev_keys_editor_leave(uint16_t game) { WD_HOST_WRITE16(KEY_CODE) = game; }

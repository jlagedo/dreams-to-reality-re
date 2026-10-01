/*
 * WINDREAM recompilation - USER32 bridges on SDL3: window, messages, keyboard.
 *
 * The game's window is an SDL window behind a fake HWND. What the game asks
 * of USER32 is small (docs/research/engine.md, "Input"):
 *   - its WndProc (0x44627b) handles WM_CREATE (INPUT_Reset), WM_DESTROY
 *     (PostQuitMessage) and swallows the Alt system keys and SC_KEYMENU;
 *   - MGM_DispatchMessages (0x43a64c) drains PeekMessageA until WM_QUIT and
 *     runs the frame only while GetFocus() is the game window;
 *   - INPUT_PollKeyboard (0x440757) polls GetAsyncKeyState(0..255) once per
 *     frame; no key message is ever read.
 *
 * So:
 *   - CreateWindowExA creates the SDL window and sends WM_CREATE, with a
 *     32-bit CREATESTRUCTA, to the guest WndProc through guest_call.
 *   - The message queue holds what SDL produces for the guest: WM_CLOSE when
 *     the window is closed. DefWindowProcA turns WM_CLOSE into WM_DESTROY as
 *     DestroyWindow would, and PostQuitMessage makes WM_QUIT. Key messages are
 *     not posted (the WndProc would only pass them to DefWindowProcA).
 *   - GetAsyncKeyState answers from SDL key events mapped to virtual keys:
 *     bit 15 while held, bit 0 if the key went down since the previous call,
 *     so a tap shorter than a frame still counts.
 *   - GetFocus returns the window while it has keyboard focus, so the game
 *     pauses in the background as the original does.
 *
 * Host keys: F11 toggles fullscreen (the game sees F11 as well); keypad 1-5
 * toggle retail debug flags (debug_toggle; the game does not see those keys).
 *
 * Environment:
 *   WD_SCALE=2          initial window size in multiples of the game's size
 *   WD_FULLSCREEN=1     start fullscreen (borderless, desktop resolution)
 *   WD_KEYS="3000:RETURN,5000:ESC"  press keys at ms since start (150 ms hold)
 *   WD_FOCUS=1          report the window as focused, so a background run
 *                       still reads (scripted) keys
 *   WD_QUIET=1          log message boxes instead of showing them
 *   WD_HEADLESS=1       keep the window hidden; force focus/quiet and mute the mixer
 */
#include <ctype.h>
#include <math.h>
#include "display_script_parse.h"
#define RECOMP_GENERATED_CODE
#include "host.h"
#include "render_live.h"

int g_wd_quiet;
static int g_force_focus;
static int g_headless;
static uint64_t g_t0, g_last_pump;
static uint32_t g_guest_wndproc;
static SDL_Window* g_window;
static SDL_Renderer* g_renderer;
static int g_destroyed;
static int g_cursor_count;

SDL_Window* host_window(void) { return g_window; }
SDL_Renderer* host_renderer(void) { return g_renderer; }
const char* host_env(const char* name) {
    const char* v = getenv(name);
    return v && *v ? v : NULL;
}

/* ---- scripted keys ---- */
#define KEY_HOLD_MS 150u
static struct { uint32_t ms; uint8_t vk; } g_script[64];
static int g_script_n;
static uint8_t g_script_fired[64];
typedef struct { uint32_t ms; int width, height, fired; } ResizeStep;
typedef struct { uint32_t ms; float x, y; int kind, fired; } MouseStep;
static ResizeStep g_resize[128];
static MouseStep g_mouse_script[128];
static int g_resize_n, g_mouse_script_n;

static void script_error(const char* name) {
    fprintf(stderr, "[user] invalid %s script\n", name);
    abort();
}
static void display_script_init(void) {
    const char* spec = host_env("WD_RESIZE");
    while (spec && *spec) {
        uint32_t ms; int w, h;
        if (g_resize_n == 128 || !wd_script_resize(&spec, &ms, &w, &h) ||
            (g_resize_n && ms < g_resize[g_resize_n - 1].ms)) script_error("WD_RESIZE");
        g_resize[g_resize_n++] = (ResizeStep){ms, w, h, 0};
    }
    spec = host_env("WD_MOUSE");
    while (spec && *spec) {
        uint32_t ms; int x, y, kind;
        if (g_mouse_script_n == 128 || !wd_script_mouse(&spec, &ms, &kind, &x, &y) ||
            (g_mouse_script_n && ms < g_mouse_script[g_mouse_script_n - 1].ms)) script_error("WD_MOUSE");
        g_mouse_script[g_mouse_script_n++] = (MouseStep){ms, (float)x, (float)y, kind, 0};
    }
}

static uint8_t vk_from_name(const char* s, size_t n) {
    static const struct { const char* name; uint8_t vk; } tab[] = {
        {"UP", W32_VK_UP}, {"DOWN", W32_VK_DOWN}, {"LEFT", W32_VK_LEFT}, {"RIGHT", W32_VK_RIGHT},
        {"RETURN", W32_VK_RETURN}, {"ENTER", W32_VK_RETURN}, {"ESC", W32_VK_ESCAPE},
        {"ESCAPE", W32_VK_ESCAPE}, {"SPACE", W32_VK_SPACE}, {"TAB", W32_VK_TAB},
        {"CTRL", W32_VK_CONTROL}, {"SHIFT", W32_VK_SHIFT}, {"ALT", W32_VK_MENU},
        {"F1", W32_VK_F1}, {"F2", W32_VK_F1 + 1}, {"F3", W32_VK_F1 + 2}, {"F10", W32_VK_F10},
        {"F4", W32_VK_F1 + 3}, {"F5", W32_VK_F1 + 4}, {"F6", W32_VK_F1 + 5},
        {"F7", W32_VK_F1 + 6}, {"F8", W32_VK_F1 + 7}, {"F9", W32_VK_F1 + 8},
        {"F11", W32_VK_F10 + 1},
        {"KP1", 0x61}, {"KP2", 0x62}, {"KP3", 0x63}, {"KP4", 0x64}, {"KP5", 0x65},
        {"BACK", W32_VK_BACK},
    };
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (strlen(tab[i].name) == n && !SDL_strncasecmp(tab[i].name, s, n)) return tab[i].vk;
    return n == 1 ? (uint8_t)toupper((unsigned char)s[0]) : 0;
}

void host_init(void) {
    SDL_SetAppMetadata("Dreams to Reality (recomp)", NULL, "org.opendreams.windream-recomp");
    g_t0 = SDL_GetTicks();   /* also sets the 1 ms Windows timer resolution (SDL_HINT_TIMER_RESOLUTION) */
    g_headless = host_env("WD_HEADLESS") != NULL;
    g_wd_quiet = g_headless || host_env("WD_QUIET") != NULL;
    g_force_focus = g_headless || host_env("WD_FOCUS") != NULL;
    if (g_headless) fprintf(stderr, "[user] WD_HEADLESS: window remains hidden, scripted focus enabled\n");
    display_script_init();
    const char* spec = host_env("WD_KEYS");
    while (spec && *spec && g_script_n < 64) {
        char* end;
        uint32_t ms = (uint32_t)strtoul(spec, &end, 10);
        if (end == spec || *end != ':') break;
        const char* name = end + 1;
        const char* comma = strchr(name, ',');
        size_t len = comma ? (size_t)(comma - name) : strlen(name);
        uint8_t vk = vk_from_name(name, len);
        if (vk) { g_script[g_script_n].ms = ms; g_script[g_script_n].vk = vk; g_script_n++; }
        fprintf(stderr, "[keys] %ums %.*s -> vk 0x%02X\n", ms, (int)len, name, vk);
        spec = comma ? comma + 1 : name + len;
    }
}
uint32_t host_elapsed_ms(void) { return (uint32_t)(SDL_GetTicks() - g_t0); }

static int script_down(int vk) {
    if (vk >= 0x61 && vk <= 0x65) return 0; // host debug controls, as with physical keypad
    uint32_t t = host_elapsed_ms();
    for (int i = 0; i < g_script_n; i++)
        if (g_script[i].vk == vk && t >= g_script[i].ms && t < g_script[i].ms + KEY_HOLD_MS) return 1;
    return 0;
}

/* ---- virtual-key state ---- */
static uint8_t g_vk_count[256];               /* keys and buttons holding each VK down */
static uint8_t g_vk_latch[256];               /* went down since the last GetAsyncKeyState */
static uint16_t g_sc_vk[SDL_SCANCODE_COUNT];  /* VKs a held key set: specific | generic << 8 */
static uint8_t g_mouse_vk[8];

static void vk_press(int vk) { if (g_vk_count[vk] < 255) g_vk_count[vk]++; g_vk_latch[vk] = 1; }
static void vk_release(int vk) { if (g_vk_count[vk]) g_vk_count[vk]--; }
void host_key_latch(int vk) { g_vk_latch[vk & 0xFF] = 1; }

static void release_all(void) {
    memset(g_vk_count, 0, sizeof g_vk_count);
    memset(g_sc_vk, 0, sizeof g_sc_vk);
    memset(g_mouse_vk, 0, sizeof g_mouse_vk);
}

/* The virtual keys Windows reports for a key: the specific one, plus the
 * generic VK_SHIFT / VK_CONTROL / VK_MENU for the modifiers. Letters follow the
 * keyboard layout, as VK codes do; other keys go by position (US layout for
 * the OEM punctuation keys). The keypad follows Num Lock. */
static uint16_t vk_of(const SDL_KeyboardEvent* k) {
    SDL_Scancode sc = k->scancode;
    if (k->key >= 'a' && k->key <= 'z') return (uint16_t)('A' + (k->key - 'a'));
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return (uint16_t)('A' + (sc - SDL_SCANCODE_A));
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return (uint16_t)('1' + (sc - SDL_SCANCODE_1));
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return (uint16_t)(W32_VK_F1 + (sc - SDL_SCANCODE_F1));
    if (sc >= SDL_SCANCODE_F13 && sc <= SDL_SCANCODE_F24) return (uint16_t)(W32_VK_F1 + 12 + (sc - SDL_SCANCODE_F13));
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_0) {
        static const uint8_t nav[10] = {W32_VK_END, W32_VK_DOWN, W32_VK_NEXT, W32_VK_LEFT, W32_VK_CLEAR,
                                        W32_VK_RIGHT, W32_VK_HOME, W32_VK_UP, W32_VK_PRIOR, W32_VK_INSERT};
        int i = sc - SDL_SCANCODE_KP_1;   /* KP_1 .. KP_9, KP_0 */
        return (k->mod & SDL_KMOD_NUM) ? (uint16_t)(W32_VK_NUMPAD0 + (i + 1) % 10) : nav[i];
    }
    switch (sc) {
    case SDL_SCANCODE_0: return '0';
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return W32_VK_RETURN;
    case SDL_SCANCODE_ESCAPE: return W32_VK_ESCAPE;
    case SDL_SCANCODE_BACKSPACE: return W32_VK_BACK;
    case SDL_SCANCODE_TAB: return W32_VK_TAB;
    case SDL_SCANCODE_SPACE: return W32_VK_SPACE;
    case SDL_SCANCODE_MINUS: return W32_VK_OEM_MINUS;
    case SDL_SCANCODE_EQUALS: return W32_VK_OEM_PLUS;
    case SDL_SCANCODE_LEFTBRACKET: return W32_VK_OEM_4;
    case SDL_SCANCODE_RIGHTBRACKET: return W32_VK_OEM_6;
    case SDL_SCANCODE_BACKSLASH: case SDL_SCANCODE_NONUSHASH: return W32_VK_OEM_5;
    case SDL_SCANCODE_SEMICOLON: return W32_VK_OEM_1;
    case SDL_SCANCODE_APOSTROPHE: return W32_VK_OEM_7;
    case SDL_SCANCODE_GRAVE: return W32_VK_OEM_3;
    case SDL_SCANCODE_COMMA: return W32_VK_OEM_COMMA;
    case SDL_SCANCODE_PERIOD: return W32_VK_OEM_PERIOD;
    case SDL_SCANCODE_SLASH: return W32_VK_OEM_2;
    case SDL_SCANCODE_NONUSBACKSLASH: return W32_VK_OEM_102;
    case SDL_SCANCODE_CAPSLOCK: return W32_VK_CAPITAL;
    case SDL_SCANCODE_PRINTSCREEN: return W32_VK_SNAPSHOT;
    case SDL_SCANCODE_SCROLLLOCK: return W32_VK_SCROLL;
    case SDL_SCANCODE_PAUSE: return W32_VK_PAUSE;
    case SDL_SCANCODE_INSERT: return W32_VK_INSERT;
    case SDL_SCANCODE_HOME: return W32_VK_HOME;
    case SDL_SCANCODE_PAGEUP: return W32_VK_PRIOR;
    case SDL_SCANCODE_DELETE: return W32_VK_DELETE;
    case SDL_SCANCODE_END: return W32_VK_END;
    case SDL_SCANCODE_PAGEDOWN: return W32_VK_NEXT;
    case SDL_SCANCODE_RIGHT: return W32_VK_RIGHT;
    case SDL_SCANCODE_LEFT: return W32_VK_LEFT;
    case SDL_SCANCODE_DOWN: return W32_VK_DOWN;
    case SDL_SCANCODE_UP: return W32_VK_UP;
    case SDL_SCANCODE_NUMLOCKCLEAR: return W32_VK_NUMLOCK;
    case SDL_SCANCODE_KP_DIVIDE: return W32_VK_DIVIDE;
    case SDL_SCANCODE_KP_MULTIPLY: return W32_VK_MULTIPLY;
    case SDL_SCANCODE_KP_MINUS: return W32_VK_SUBTRACT;
    case SDL_SCANCODE_KP_PLUS: return W32_VK_ADD;
    case SDL_SCANCODE_KP_PERIOD: return (k->mod & SDL_KMOD_NUM) ? W32_VK_DECIMAL : W32_VK_DELETE;
    case SDL_SCANCODE_APPLICATION: return W32_VK_APPS;
    case SDL_SCANCODE_LSHIFT: return W32_VK_LSHIFT | W32_VK_SHIFT << 8;
    case SDL_SCANCODE_RSHIFT: return W32_VK_RSHIFT | W32_VK_SHIFT << 8;
    case SDL_SCANCODE_LCTRL: return W32_VK_LCONTROL | W32_VK_CONTROL << 8;
    case SDL_SCANCODE_RCTRL: return W32_VK_RCONTROL | W32_VK_CONTROL << 8;
    case SDL_SCANCODE_LALT: return W32_VK_LMENU | W32_VK_MENU << 8;
    case SDL_SCANCODE_RALT: return W32_VK_RMENU | W32_VK_MENU << 8;
    case SDL_SCANCODE_LGUI: return W32_VK_LWIN;
    case SDL_SCANCODE_RGUI: return W32_VK_RWIN;
    default: return 0;
    }
}

static void key_event(const SDL_KeyboardEvent* k) {
    if ((unsigned)k->scancode >= SDL_SCANCODE_COUNT) return;
    if (k->down) {
        uint16_t v = vk_of(k);
        if (!v || g_sc_vk[k->scancode]) return;   /* unmapped, or auto-repeat */
        g_sc_vk[k->scancode] = v;
        vk_press(v & 0xFF);
        if (v >> 8) vk_press(v >> 8);
    } else {
        uint16_t v = g_sc_vk[k->scancode];
        g_sc_vk[k->scancode] = 0;
        if (v & 0xFF) vk_release(v & 0xFF);
        if (v >> 8) vk_release(v >> 8);
    }
}

static void mouse_event(const SDL_MouseButtonEvent* b) {
    static const uint8_t vk[6] = {0, W32_VK_LBUTTON, W32_VK_MBUTTON, W32_VK_RBUTTON, W32_VK_XBUTTON1, W32_VK_XBUTTON2};
    if (b->button < 1 || b->button > 5) return;
    if (b->down && !g_mouse_vk[b->button]) { g_mouse_vk[b->button] = 1; vk_press(vk[b->button]); }
    else if (!b->down && g_mouse_vk[b->button]) { g_mouse_vk[b->button] = 0; vk_release(vk[b->button]); }
}

/* ---- editor mouse ----
 * The Dreams Editor reads a cursor that no retail build produces (spec 005):
 * GAME_TickFrame and CTRL_Dispatcher decode events 0x34 (x << 16 | y, then
 * dx << 16 | dy), 0x35/0x36 (left down/up) and 0x37/0x38 (right down/up), but
 * nothing posts them, and MGM_DispatchMessages treats them as fatal in its
 * input queue. So they go straight into the standing handler's queue 0x626f70
 * with the game's MGM_PostMessage (0x43b31a), before the pump dispatches, and
 * only while the handler is one of those two: another handler may raise
 * "unknown message type". The deltas share the joystick globals
 * 0x4a3154/0x4a3158 that the free camera and joystick input mode read. */
#define WD_GAME_QUEUE    0x00626F70u
#define WD_GAME_HANDLER  0x00626F74u
#define WD_POST_MESSAGE  0x0043B31Au
static int g_mx, g_my, g_mdx, g_mdy, g_mmoved;
static uint8_t g_mbtn[16];
static int g_mbtn_n;

static void mouse_game_pos(SDL_Event* e, float* x, float* y) {
    SDL_Renderer* r = host_renderer();
    if (r) SDL_ConvertEventToRenderCoordinates(r, e);
    else if (wd_render_requested()) wd_render_mouse(e);
    int w = (int)WD_HOST_READ32(0x0049D9FCu), h = (int)WD_HOST_READ32(0x0049DA00u);   /* game frame size */
    if (w <= 0 || h <= 0) { w = 640; h = 480; }
    float cx = *x < 0 ? 0 : *x > (float)(w - 1) ? (float)(w - 1) : *x;
    float cy = *y < 0 ? 0 : *y > (float)(h - 1) ? (float)(h - 1) : *y;
    g_mx = (int)cx; g_my = (int)cy;
}

static void mouse_motion(SDL_Event* e) {
    mouse_game_pos(e, &e->motion.x, &e->motion.y);
    g_mdx += (int)e->motion.xrel; g_mdy += (int)e->motion.yrel;
    g_mmoved = 1;
}

static void mouse_button_game(SDL_Event* e) {
    mouse_game_pos(e, &e->button.x, &e->button.y);
    g_mmoved = 1;
    uint8_t t = e->button.button == SDL_BUTTON_LEFT ? (e->button.down ? 0x35 : 0x36)
              : e->button.button == SDL_BUTTON_RIGHT ? (e->button.down ? 0x37 : 0x38) : 0;
    if (t && g_mbtn_n < (int)sizeof g_mbtn) g_mbtn[g_mbtn_n++] = t;
}

static void mouse_post(void) {
    uint32_t h = WD_HOST_READ32(WD_GAME_HANDLER), q = WD_HOST_READ32(WD_GAME_QUEUE);
    int ok = q && (h == 0x00416D45u || h == 0x0040E75Cu);
    if (ok && (g_mmoved || g_mbtn_n)) {
        uint32_t pos = (uint32_t)g_mx << 16 | ((uint32_t)g_my & 0xFFFFu);
        uint32_t d = ((uint32_t)g_mdx & 0xFFFFu) << 16 | ((uint32_t)g_mdy & 0xFFFFu);
        guest_call_regs(WD_POST_MESSAGE, q, 0x34, pos, d);
        for (int i = 0; i < g_mbtn_n; i++) guest_call_regs(WD_POST_MESSAGE, q, g_mbtn[i], 0, 0);
    }
    g_mmoved = 0; g_mdx = g_mdy = 0; g_mbtn_n = 0;
}

/* Called by lifted GAME_TickFrame before GAME_HandleHotkeys (lift.py CALLS):
 * runs the Dreams Editor draw 0x44d46d, which retail never calls, while the
 * editor flag (keypad 5) is set. Every guest register is restored. */
void wd_editor_frame(void) {
    if (WD_HOST_READ32(0x004A477Cu))
        guest_call_regs(0x0044D46Du, g_eax, g_edx, g_ebx, g_ecx);
}

/* ---- message queue ---- */
typedef struct { uint32_t hwnd, msg, wp, lp; } Msg;
static Msg g_q[32];
static int g_qn;
static int g_quit;
static uint32_t g_quit_code;

static void post(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    for (int i = 0; i < g_qn; i++)
        if (g_q[i].hwnd == hwnd && g_q[i].msg == msg) return;   /* one WM_CLOSE is enough */
    if (g_qn < (int)(sizeof g_q / sizeof g_q[0])) g_q[g_qn++] = (Msg){hwnd, msg, wp, lp};
}

/* Keypad 1-4 toggle retail debug flags that no retail code sets:
 *   1  Frame Rate/Mem 3DTR readout (GAME_TickFrame, 0x49d5c0; hidden while
 *      the editor flag 0x4a477c is set)
 *   2  object HUD (DBG_DrawObjectInfo, 0x49d5d0)
 *   3  byte 0x4ac8c8: REND_DrawFrame collects triangles (SW_CollectFaceTriangles)
 *      instead of rasterizing and skips the span flush, so the collision
 *      wireframe drawn while Backspace is held (PHYS_ResolveCollisions) stays
 *      on screen; nothing clears the frame
 *   4  0x4a4758 == 1: GAME_TickFrame pins the step 0x5e5388 at 2.0 (outside
 *      demo recording)
 *   5  editor flag 0x4a477c: no collision, no momentum, faster root motion,
 *      no camera lag or collision, no level exits; the editor menu itself is
 *      unreachable. VID_SetResolution (F1-F6) clears it.
 * The keys are kept from the game. */
static const struct { SDL_Scancode sc; uint32_t va; int byte; } g_debug_keys[] = {
    {SDL_SCANCODE_KP_1, 0x0049D5C0u, 0},
    {SDL_SCANCODE_KP_2, 0x0049D5D0u, 0},
    {SDL_SCANCODE_KP_3, 0x004AC8C8u, 1},
    {SDL_SCANCODE_KP_4, 0x004A4758u, 0},
    {SDL_SCANCODE_KP_5, 0x004A477Cu, 0},
};

static int debug_toggle(const SDL_KeyboardEvent* k) {
    for (size_t i = 0; i < sizeof g_debug_keys / sizeof g_debug_keys[0]; i++) {
        uint32_t va = g_debug_keys[i].va;
        if (k->scancode != g_debug_keys[i].sc) continue;
        if (k->down && !k->repeat) {
            uint32_t v = g_debug_keys[i].byte ? (WD_HOST_WRITE8(va) = !WD_HOST_READ8(va)) : (WD_HOST_WRITE32(va) = !WD_HOST_READ32(va));
            fprintf(stderr, "[debug] [0x%08X] = %u\n", va, v);
        }
        return 1;
    }
    return 0;
}

static void display_checkpoint(const char* reason) {
    int w = 0, h = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(g_window, &w, &h);
    SDL_GetWindowSizeInPixels(g_window, &pw, &ph);
    fprintf(stderr, "[display] client=%dx%d drawable=%dx%d fullscreen=%d reason=%s\n",
            w, h, pw, ph, (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0, reason);
}

static void display_script_pump(void) {
    if (!g_window || g_destroyed) return;
    const uint32_t now = host_elapsed_ms();
    for (int i = 0; i < g_script_n; ++i) {
        if (g_script_fired[i] || now < g_script[i].ms) continue;
        g_script_fired[i] = 1;
        if (g_script[i].vk == W32_VK_F10 + 1) {
            if (g_headless) fprintf(stderr, "[display] headless F11 ignored\n");
            else if (!SDL_SetWindowFullscreen(g_window, !(SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN)))
                script_error("F11 fullscreen");
        } else if (g_script[i].vk >= 0x61 && g_script[i].vk <= 0x65) {
            SDL_KeyboardEvent key = {0};
            key.scancode = g_debug_keys[g_script[i].vk - 0x61].sc;
            key.down = true;
            debug_toggle(&key);
        }
    }
    for (int i = 0; i < g_resize_n; ++i) {
        ResizeStep* step = &g_resize[i];
        if (step->fired || now < step->ms) continue;
        step->fired = 1;
        if (!SDL_SetWindowSize(g_window, step->width, step->height)) script_error("WD_RESIZE");
        display_checkpoint("resize");
    }
    for (int i = 0; i < g_mouse_script_n; ++i) {
        MouseStep* step = &g_mouse_script[i];
        if (step->fired || now < step->ms) continue;
        step->fired = 1;
        SDL_Event e = {0};
        if (!step->kind) {
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.windowID = SDL_GetWindowID(g_window);
            e.motion.x = step->x; e.motion.y = step->y;
            /* Absolute test input; do not invent relative camera motion. */
            mouse_motion(&e);
        } else {
            e.type = (step->kind & 1) ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.windowID = SDL_GetWindowID(g_window);
            e.button.x = step->x; e.button.y = step->y;
            e.button.button = step->kind <= 2 ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
            e.button.down = (step->kind & 1) != 0;
            mouse_event(&e.button); mouse_button_game(&e);
        }
        fprintf(stderr, "[mouse-script] ms=%u kind=%d client=%.3f,%.3f logical=%d,%d\n",
                step->ms, step->kind, step->x, step->y, g_mx, g_my);
    }
}

void host_pump(void) {
    if (!SDL_WasInit(SDL_INIT_EVENTS)) return;
    display_script_pump();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (g_window && !g_destroyed) post(WD_HWND_MAIN, W32_WM_CLOSE, 0, 0);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (debug_toggle(&e.key)) break;
            if (e.key.scancode == SDL_SCANCODE_F11 && !e.key.repeat && g_window)
                SDL_SetWindowFullscreen(g_window, !(SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN));
            key_event(&e.key);
            break;
        case SDL_EVENT_KEY_UP:
            if (!debug_toggle(&e.key)) key_event(&e.key);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: mouse_event(&e.button); mouse_button_game(&e); break;
        case SDL_EVENT_MOUSE_MOTION: mouse_motion(&e); break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: release_all(); break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            if (g_window) display_checkpoint("event");
            break;
        default: joy_event(&e); break;
        }
    }
    g_last_pump = SDL_GetTicks();
}

static uint32_t wndproc(uint32_t msg, uint32_t wp, uint32_t lp) {
    if (!g_guest_wndproc) return 0;
    uint32_t a[4] = {WD_HWND_MAIN, msg, wp, lp};
    return guest_call(g_guest_wndproc, 4, a);
}

static void write_msg(uint32_t va, const Msg* m) {
    WD_HOST_WRITE32(va + W32_MSG_HWND) = m->hwnd;
    WD_HOST_WRITE32(va + W32_MSG_MESSAGE) = m->msg;
    WD_HOST_WRITE32(va + W32_MSG_WPARAM) = m->wp;
    WD_HOST_WRITE32(va + W32_MSG_LPARAM) = m->lp;
    WD_HOST_WRITE32(va + W32_MSG_TIME) = host_elapsed_ms();
    WD_HOST_WRITE32(va + W32_MSG_PT_X) = 0;
    WD_HOST_WRITE32(va + W32_MSG_PT_Y) = 0;
}

void imp_PeekMessageA(void) {  /* (lpMsg, hWnd, min, max, remove) */
    host_pump();
    mouse_post();
    uint32_t hw = ARG(1), lo = ARG(2), hi = ARG(3), rm = ARG(4) & W32_PM_REMOVE;
    for (int i = 0; i < g_qn; i++) {
        Msg m = g_q[i];
        if ((hw && m.hwnd != hw) || ((lo || hi) && (m.msg < lo || m.msg > hi))) continue;
        write_msg(ARG(0), &m);
        if (rm) { memmove(&g_q[i], &g_q[i + 1], (size_t)(g_qn - i - 1) * sizeof(Msg)); g_qn--; }
        RET(1); STDRET(5);
        return;
    }
    if (g_quit) {   /* WM_QUIT comes last and ignores the filters */
        Msg q = {0, W32_WM_QUIT, g_quit_code, 0};
        write_msg(ARG(0), &q);
        if (rm) g_quit = 0;
        RET(1); STDRET(5);
        return;
    }
    RET(0); STDRET(5);
}
void imp_TranslateMessage(void) { RET(0); STDRET(1); }
void imp_DispatchMessageA(void) {
    uint32_t m = ARG(0), r = 0;
    if (WD_HOST_READ32(m + W32_MSG_HWND) == WD_HWND_MAIN && !g_destroyed)
        r = wndproc(WD_HOST_READ32(m + W32_MSG_MESSAGE), WD_HOST_READ32(m + W32_MSG_WPARAM), WD_HOST_READ32(m + W32_MSG_LPARAM));
    RET(r); STDRET(1);
}
void imp_PostQuitMessage(void) { g_quit = 1; g_quit_code = ARG(0); STDRET(1); }

void imp_DefWindowProcA(void) {  /* (hwnd, msg, wParam, lParam) */
    if (ARG(0) == WD_HWND_MAIN && ARG(1) == W32_WM_CLOSE && !g_destroyed) {
        /* DestroyWindow: WM_DESTROY to the WndProc, which posts the quit */
        wndproc(W32_WM_DESTROY, 0, 0);
        g_destroyed = 1;
        if (g_window) SDL_HideWindow(g_window);
    }
    RET(0); STDRET(4);
}

/* ---- class and window ---- */
void imp_LoadIconA(void) { RET(WD_HICON); STDRET(2); }
void imp_LoadCursorA(void) { RET(WD_HCURSOR); STDRET(2); }

void imp_RegisterClassA(void) {  /* (const WNDCLASSA*) */
    uint32_t wc = ARG(0);
    char cls[128];
    guest_str(WD_HOST_READ32(wc + W32_WC_CLASSNAME), cls, sizeof cls);
    if (g_guest_wndproc && g_guest_wndproc != WD_HOST_READ32(wc + W32_WC_WNDPROC))
        fprintf(stderr, "[user] second window class \"%s\": only one guest WndProc is tracked\n", cls);
    g_guest_wndproc = WD_HOST_READ32(wc + W32_WC_WNDPROC);
    fprintf(stderr, "[user] RegisterClassA(\"%s\") WndProc=0x%08X\n", cls, g_guest_wndproc);
    RET(0xC001u); STDRET(1);
}

static int video_init(void) {
    if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        fprintf(stderr, "[user] SDL video: %s\n", SDL_GetError());
        return 0;
    }
    joy_init();
    return 1;
}

/* The largest WD_SCALE (default 2) at which the window fits the desktop. */
static int window_scale(int w, int h) {
    const char* s = host_env("WD_SCALE");
    int scale = s ? atoi(s) : 2;
    if (scale < 1) scale = 1;
    SDL_Rect r;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &r))
        while (scale > 1 && (w * scale > r.w || h * scale > r.h - 40)) scale--;
    return scale;
}

void imp_CreateWindowExA(void) {  /* (exStyle, cls, name, style, x, y, w, h, parent, menu, inst, param) */
    char title[128];
    guest_str(ARG(2), title, sizeof title);
    if (g_window) {
        fprintf(stderr, "[user] CreateWindowExA(\"%s\"): only one window is supported\n", title);
        RET(0); STDRET(12);
        return;
    }
    if (!video_init()) { RET(0); STDRET(12); return; }
    /* VID_CreateWindow (0x4460cf) asks for its AdjustWindowRectEx rectangle
     * plus one. AdjustWindowRectEx adds no frame here (SDL draws it), so the
     * client area the game wants is one less. */
    int cw = (int)ARG(6) - 1, ch = (int)ARG(7) - 1;
    if (cw < 64 || ch < 64) { cw = 640; ch = 480; }
    int scale = window_scale(cw, ch);
    int width = cw * scale, height = ch * scale;
    const char* ew = host_env("WD_WIDTH");
    const char* eh = host_env("WD_HEIGHT");
    if (ew || eh) {
        if (!ew || !eh) script_error("WD_WIDTH/WD_HEIGHT");
        int w, h;
        if (!wd_script_dimension(&ew, &w) || *ew || !wd_script_dimension(&eh, &h) || *eh)
            script_error("WD_WIDTH/WD_HEIGHT");
        width = w; height = h;
    }
    g_window = SDL_CreateWindow(title, width, height, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | wd_render_window_flags());
    g_renderer = g_window && !wd_render_requested() ? SDL_CreateRenderer(g_window, NULL) : NULL;
    if (!g_window || (wd_render_requested() ? !wd_render_open(g_window) : !g_renderer)) {
        fprintf(stderr, "[user] SDL window: %s\n", SDL_GetError());
        if (g_window) SDL_DestroyWindow(g_window);
        g_window = NULL;
        RET(0); STDRET(12);
        return;
    }
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SetWindowMinimumSize(g_window, 64, 64);
    if (!g_headless && host_env("WD_FULLSCREEN")) SDL_SetWindowFullscreen(g_window, true);
    fprintf(stderr, "[user] CreateWindowExA(\"%s\", %dx%d) -> SDL window %dx%d, renderer %s\n",
            title, (int)ARG(6), (int)ARG(7), width, height,
            wd_render_requested() ? "sokol direct" : SDL_GetRendererName(g_renderer));
    display_checkpoint("initial");

    uint32_t cs = shim_alloc(W32_CS_SIZE, 16);
    WD_HOST_WRITE32(cs + W32_CS_INSTANCE) = ARG(10);
    WD_HOST_WRITE32(cs + W32_CS_CY) = ARG(7);
    WD_HOST_WRITE32(cs + W32_CS_CX) = ARG(6);
    WD_HOST_WRITE32(cs + W32_CS_Y) = ARG(5);
    WD_HOST_WRITE32(cs + W32_CS_X) = ARG(4);
    WD_HOST_WRITE32(cs + W32_CS_STYLE) = ARG(3);
    WD_HOST_WRITE32(cs + W32_CS_NAME) = ARG(2);
    WD_HOST_WRITE32(cs + W32_CS_CLASS) = ARG(1);
    WD_HOST_WRITE32(cs + W32_CS_EXSTYLE) = ARG(0);
    if ((int32_t)wndproc(W32_WM_CREATE, 0, cs) == -1) {
        fprintf(stderr, "[user] WM_CREATE failed\n");
        wd_render_close();
        SDL_DestroyRenderer(g_renderer);
        SDL_DestroyWindow(g_window);
        g_window = NULL; g_renderer = NULL;
        RET(0); STDRET(12);
        return;
    }
    if (!g_headless && (ARG(3) & W32_WS_VISIBLE)) SDL_ShowWindow(g_window);
    RET(WD_HWND_MAIN); STDRET(12);
}
void imp_ShowWindow(void) {  /* (hwnd, nCmdShow): shown whatever the CRT's nCmdShow */
    int was = g_window && !(SDL_GetWindowFlags(g_window) & SDL_WINDOW_HIDDEN);
    if (ARG(0) == WD_HWND_MAIN && g_window && !g_destroyed && !g_headless) {
        SDL_ShowWindow(g_window);
        SDL_RaiseWindow(g_window);
    }
    RET(was); STDRET(2);
}
void imp_UpdateWindow(void) { RET(ARG(0) == WD_HWND_MAIN); STDRET(1); }
void imp_GetClientRect(void) {  /* (hwnd, RECT*) */
    int w = 0, h = 0;
    if (ARG(0) != WD_HWND_MAIN || !g_window) { RET(0); STDRET(2); return; }
    SDL_GetWindowSize(g_window, &w, &h);
    memset(wd_host_range(ARG(1), 16, 1), 0, 16);
    WD_HOST_WRITE32(ARG(1) + W32_RECT_RIGHT) = (uint32_t)w;
    WD_HOST_WRITE32(ARG(1) + W32_RECT_BOTTOM) = (uint32_t)h;
    RET(1); STDRET(2);
}
void imp_AdjustWindowRectEx(void) { RET(1); STDRET(4); }   /* no frame: SDL owns the decorations */
void imp_GetSystemMetrics(void) {
    int i = (int)ARG(0), v = 0;
    const SDL_DisplayMode* m = video_init() ? SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay()) : NULL;
    if (m && i == W32_SM_CXSCREEN) v = m->w;
    else if (m && i == W32_SM_CYSCREEN) v = m->h;
    else fprintf(stderr, "[user] GetSystemMetrics(%d): 0\n", i);
    RET(v); STDRET(1);
}
void imp_ShowCursor(void) {
    g_cursor_count += ARG(0) ? 1 : -1;
    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        if (g_cursor_count < 0) SDL_HideCursor(); else SDL_ShowCursor();
    }
    RET(g_cursor_count); STDRET(1);
}
void imp_GetFocus(void) {
    int focused = g_window && !g_destroyed &&
                  (g_force_focus || (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS));
    RET(focused ? WD_HWND_MAIN : 0); STDRET(0);
}

/* ---- keyboard ---- */
void imp_GetAsyncKeyState(void) {  /* (vk) -> SHORT */
    int vk = (int)(ARG(0) & 0xFF);
    if (SDL_GetTicks() - g_last_pump >= 4) host_pump();   /* loops that poll keys without PeekMessageA */
    uint16_t s = 0;
    if (g_vk_count[vk] || script_down(vk) || joy_key_down(vk)) s |= 0x8000;
    if (g_vk_latch[vk]) { s |= 1; g_vk_latch[vk] = 0; }
    RET((uint32_t)(int32_t)(int16_t)s); STDRET(1);
}

/* ---- text ---- */
/* The guest's code page is 1252 (GetACP). */
static uint8_t cp1252_upper(uint8_t c) {
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 32);
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return (uint8_t)(c - 32);
    switch (c) {
    case 0x9A: return 0x8A;   /* š */
    case 0x9C: return 0x8C;   /* œ */
    case 0x9E: return 0x8E;   /* ž */
    case 0xFF: return 0x9F;   /* ÿ */
    default: return c;
    }
}
void imp_CharUpperBuffA(void) {  /* (str, len) -> len */
    for (uint32_t i = 0; i < ARG(1); i++) WD_HOST_WRITE8(ARG(0) + i) = cp1252_upper(WD_HOST_READ8(ARG(0) + i));
    RET(ARG(1)); STDRET(2);
}

static void cp1252_to_utf8(const char* in, char* out, size_t cap) {
    static const uint16_t hi[32] = {   /* 0x80..0x9F; 0 = unassigned */
        0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
        0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
    };
    size_t n = 0;
    for (; *in && n + 4 < cap; in++) {
        uint8_t c = (uint8_t)*in;
        uint32_t u = c >= 0x80 && c < 0xA0 ? (hi[c - 0x80] ? hi[c - 0x80] : '?') : c;
        if (u < 0x80) out[n++] = (char)u;
        else if (u < 0x800) { out[n++] = (char)(0xC0 | u >> 6); out[n++] = (char)(0x80 | (u & 0x3F)); }
        else { out[n++] = (char)(0xE0 | u >> 12); out[n++] = (char)(0x80 | (u >> 6 & 0x3F)); out[n++] = (char)(0x80 | (u & 0x3F)); }
    }
    out[n] = 0;
}

/* ---- misc ---- */
void imp_MessageBeep(void) { RET(1); STDRET(1); }
void imp_MessageBoxA(void) {  /* (hwnd, text, caption, type) */
    static const struct { int n; int id[3]; const char* label[3]; } sets[6] = {
        {1, {W32_IDOK}, {"OK"}},
        {2, {W32_IDOK, W32_IDCANCEL}, {"OK", "Cancel"}},
        {3, {W32_IDABORT, W32_IDRETRY, W32_IDIGNORE}, {"Abort", "Retry", "Ignore"}},
        {3, {W32_IDYES, W32_IDNO, W32_IDCANCEL}, {"Yes", "No", "Cancel"}},
        {2, {W32_IDYES, W32_IDNO}, {"Yes", "No"}},
        {2, {W32_IDRETRY, W32_IDCANCEL}, {"Retry", "Cancel"}},
    };
    char text[512], cap[128], utext[1536], ucap[384];
    guest_str(ARG(1), text, sizeof text);
    guest_str(ARG(2), cap, sizeof cap);
    uint32_t type = ARG(3), t = type & W32_MB_TYPEMASK, icon = type & W32_MB_ICONMASK;
    if (t >= 6) t = W32_MB_OK;
    int def = (int)((type & W32_MB_DEFMASK) >> 8);
    if (def >= sets[t].n) def = 0;
    fprintf(stderr, "[user] MessageBox \"%s\": %s\n", cap, text);
    int r = sets[t].id[def];
    if (!g_wd_quiet) {
        SDL_MessageBoxButtonData b[3];
        for (int i = 0; i < sets[t].n; i++) {
            int id = sets[t].id[i];
            b[i].flags = (i == def ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0) |
                         (id == W32_IDCANCEL || (t == W32_MB_YESNO && id == W32_IDNO) ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0);
            b[i].buttonID = id;
            b[i].text = sets[t].label[i];
        }
        cp1252_to_utf8(text, utext, sizeof utext);
        cp1252_to_utf8(cap, ucap, sizeof ucap);
        SDL_MessageBoxData d = {0};
        d.flags = (icon == W32_MB_ICONHAND ? SDL_MESSAGEBOX_ERROR : icon == W32_MB_ICONEXCLAMATION ? SDL_MESSAGEBOX_WARNING
                   : icon ? SDL_MESSAGEBOX_INFORMATION : 0) | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
        d.window = g_window;
        d.title = ucap;
        d.message = utext;
        d.numbuttons = sets[t].n;
        d.buttons = b;
        int hit = -1;
        if (SDL_ShowMessageBox(&d, &hit) && hit >= 0) r = hit;
    }
    RET(r); STDRET(4);
}

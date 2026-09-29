/*
 * WINDREAM recompilation - USER32 bridges: window, messages, keyboard.
 *
 * The game gets a real Win32 window. RegisterClassA records the game's window
 * procedure (a guest VA) and registers the class with host_wndproc instead;
 * host_wndproc forwards every message into the lifted procedure through
 * guest_call. Windows only calls a window procedure on the thread that owns the
 * window, from inside a USER32 call, so this always happens inside a bridge on
 * the game's main thread, where the guest register state is live.
 *
 * Messages carry 64-bit wParam/lParam on the host and 32-bit ones in the game.
 * The game's procedure only reads scalars; everything else it hands back to
 * DefWindowProcA, which recovers the original 64-bit values from the message
 * being delivered (g_ctx) instead of the truncated ones.
 *
 * Environment (testing):
 *   WD_KEYS="3000:RETURN,5000:ESC"  press keys at ms since start (150 ms hold)
 *   WD_FOCUS=1                      report the game window as focused, so a
 *                                   background run still reads (scripted) keys
 *   WD_QUIET=1                      log message boxes instead of showing them
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <ctype.h>
#define RECOMP_GENERATED_CODE
#include "imports.h"

int g_wd_quiet;
static int g_force_focus;
static uint32_t g_t0;
static uint32_t g_guest_wndproc;
static HWND g_hwnd;
static HMODULE g_res;           /* the guest EXE, loaded as a resource file */

#define KEY_HOLD_MS 150u
static struct { uint32_t ms; uint8_t vk; } g_script[64];
static int g_script_n;

static uint8_t vk_from_name(const char* s, size_t n) {
    static const struct { const char* name; uint8_t vk; } tab[] = {
        {"UP", VK_UP}, {"DOWN", VK_DOWN}, {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT},
        {"RETURN", VK_RETURN}, {"ENTER", VK_RETURN}, {"ESC", VK_ESCAPE}, {"ESCAPE", VK_ESCAPE},
        {"SPACE", VK_SPACE}, {"TAB", VK_TAB}, {"CTRL", VK_CONTROL}, {"SHIFT", VK_SHIFT},
        {"ALT", VK_MENU}, {"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F10", VK_F10},
        {"BACK", VK_BACK},
    };
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (strlen(tab[i].name) == n && !_strnicmp(tab[i].name, s, n)) return tab[i].vk;
    return n == 1 ? (uint8_t)toupper((unsigned char)s[0]) : 0;
}

void host_init(void) {
    timeBeginPeriod(1);
    g_t0 = timeGetTime();
    g_wd_quiet = getenv("WD_QUIET") != NULL;
    g_force_focus = getenv("WD_FOCUS") != NULL;
    const char* spec = getenv("WD_KEYS");
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
uint32_t host_elapsed_ms(void) { return timeGetTime() - g_t0; }

static int script_down(int vk) {
    uint32_t t = host_elapsed_ms();
    for (int i = 0; i < g_script_n; i++)
        if (g_script[i].vk == vk && t >= g_script[i].ms && t < g_script[i].ms + KEY_HOLD_MS) return 1;
    return 0;
}

/* ---- message delivery ---- */
typedef struct { HWND h; UINT msg; WPARAM wp; LPARAM lp; } Ctx;
static __declspec(thread) Ctx t_ctx[16];
static __declspec(thread) int t_depth;

static LRESULT CALLBACK host_wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (!g_guest_wndproc || t_depth >= 16) return DefWindowProcA(h, m, wp, lp);
    t_ctx[t_depth++] = (Ctx){h, m, wp, lp};
    uint32_t a[4] = {H32(h), m, (uint32_t)wp, (uint32_t)lp};
    uint32_t r = guest_call(g_guest_wndproc, 4, a);
    t_depth--;
    return (LRESULT)(int32_t)r;
}

void imp_DefWindowProcA(void) {  /* (hwnd, msg, wParam, lParam) */
    HWND h = (HWND)HHOST(ARG(0));
    UINT m = ARG(1);
    WPARAM wp = ARG(2);
    LPARAM lp = (LPARAM)(int32_t)ARG(3);
    for (int i = t_depth - 1; i >= 0; i--)
        if (t_ctx[i].msg == m && (uint32_t)t_ctx[i].wp == ARG(2) && (uint32_t)t_ctx[i].lp == ARG(3)) {
            h = t_ctx[i].h; wp = t_ctx[i].wp; lp = t_ctx[i].lp;
            break;
        }
    RET((uint32_t)DefWindowProcA(h, m, wp, lp)); STDRET(4);
}

/* PeekMessageA keeps the full host MSG, so Translate/DispatchMessageA of the
 * guest copy act on the original 64-bit values. */
static __declspec(thread) MSG t_msg;
static __declspec(thread) int t_have_msg;

static void msg_to_guest(uint32_t va, const MSG* m) {
    MEM32(va + 0) = H32(m->hwnd);
    MEM32(va + 4) = m->message;
    MEM32(va + 8) = (uint32_t)m->wParam;
    MEM32(va + 12) = (uint32_t)m->lParam;
    MEM32(va + 16) = m->time;
    MEM32(va + 20) = (uint32_t)m->pt.x;
    MEM32(va + 24) = (uint32_t)m->pt.y;
}
static MSG msg_from_guest(uint32_t va) {
    if (t_have_msg && MEM32(va + 4) == t_msg.message && MEM32(va + 8) == (uint32_t)t_msg.wParam
        && MEM32(va + 12) == (uint32_t)t_msg.lParam && MEM32(va + 0) == H32(t_msg.hwnd))
        return t_msg;
    MSG m = {0};
    m.hwnd = (HWND)HHOST(MEM32(va)); m.message = MEM32(va + 4);
    m.wParam = MEM32(va + 8); m.lParam = (LPARAM)(int32_t)MEM32(va + 12);
    m.time = MEM32(va + 16); m.pt.x = (LONG)MEM32(va + 20); m.pt.y = (LONG)MEM32(va + 24);
    return m;
}

void imp_PeekMessageA(void) {  /* (lpMsg, hWnd, min, max, remove) */
    MSG m;
    BOOL r = PeekMessageA(&m, (HWND)HHOST(ARG(1)), ARG(2), ARG(3), ARG(4));
    if (r) { t_msg = m; t_have_msg = 1; msg_to_guest(ARG(0), &m); }
    RET(r); STDRET(5);
}
void imp_TranslateMessage(void) { MSG m = msg_from_guest(ARG(0)); RET(TranslateMessage(&m)); STDRET(1); }
void imp_DispatchMessageA(void) { MSG m = msg_from_guest(ARG(0)); RET((uint32_t)DispatchMessageA(&m)); STDRET(1); }
void imp_PostQuitMessage(void) { PostQuitMessage((int)ARG(0)); STDRET(1); }

/* ---- class and window ---- */
static uint32_t res_id(uint32_t v, char* buf, int cap, const char** out) {
    /* MAKEINTRESOURCE ids stay ids; anything else is a guest string pointer. */
    if (v <= 0xFFFF) { *out = MAKEINTRESOURCEA(v); return v; }
    guest_str(v, buf, cap); *out = buf; return v;
}
void imp_LoadIconA(void) {  /* (hInstance, name) */
    char buf[128]; const char* name;
    res_id(ARG(1), buf, sizeof buf, &name);
    HICON h = NULL;
    if (ARG(0)) {
        if (!g_res) g_res = LoadLibraryExA(g_wd_exe, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (g_res) h = LoadIconA(g_res, name);
    } else h = LoadIconA(NULL, name);
    if (!h) h = LoadIconA(NULL, IDI_APPLICATION);
    RET(H32(h)); STDRET(2);
}
void imp_LoadCursorA(void) {  /* (hInstance, name): the game only asks for IDC_ARROW */
    char buf[128]; const char* name;
    res_id(ARG(1), buf, sizeof buf, &name);
    HCURSOR h = LoadCursorA(NULL, ARG(0) ? IDC_ARROW : name);
    RET(H32(h)); STDRET(2);
}

void imp_RegisterClassA(void) {  /* (const WNDCLASSA*): 40 bytes, 32-bit layout */
    uint32_t wc = ARG(0);
    char cls[128];
    guest_str(MEM32(wc + 36), cls, sizeof cls);
    if (g_guest_wndproc && g_guest_wndproc != MEM32(wc + 4))
        fprintf(stderr, "[user] second window class \"%s\": only one guest WndProc is tracked\n", cls);
    g_guest_wndproc = MEM32(wc + 4);
    WNDCLASSA w = {0};
    w.style = MEM32(wc + 0);
    w.lpfnWndProc = host_wndproc;
    w.cbClsExtra = (int)MEM32(wc + 8);
    w.cbWndExtra = (int)MEM32(wc + 12);
    w.hInstance = GetModuleHandleA(NULL);
    w.hIcon = (HICON)HHOST(MEM32(wc + 20));
    w.hCursor = (HCURSOR)HHOST(MEM32(wc + 24));
    w.hbrBackground = (HBRUSH)HHOST(MEM32(wc + 28));
    w.lpszClassName = cls;
    ATOM a = RegisterClassA(&w);
    fprintf(stderr, "[user] RegisterClassA(\"%s\") WndProc=0x%08X -> atom %u\n", cls, g_guest_wndproc, a);
    RET(a); STDRET(1);
}

void imp_CreateWindowExA(void) {  /* (exStyle, cls, name, style, x, y, w, h, parent, menu, inst, param) */
    char cls[128], title[128];
    guest_str(ARG(1), cls, sizeof cls);
    guest_str(ARG(2), title, sizeof title);
    HWND h = CreateWindowExA(ARG(0), cls, title, ARG(3), (int)ARG(4), (int)ARG(5), (int)ARG(6), (int)ARG(7),
                             (HWND)HHOST(ARG(8)), NULL, GetModuleHandleA(NULL), NULL);
    if (h && !g_hwnd) g_hwnd = h;
    fprintf(stderr, "[user] CreateWindowExA(\"%s\", %dx%d, style 0x%X) -> %p\n",
            title, (int)ARG(6), (int)ARG(7), ARG(3), (void*)h);
    RET(H32(h)); STDRET(12);
}
void imp_ShowWindow(void) {
    int cmd = (int)ARG(1);
    if (cmd == SW_HIDE) cmd = SW_SHOW;   /* the CRT's nCmdShow, never meant to hide */
    RET(ShowWindow((HWND)HHOST(ARG(0)), cmd)); STDRET(2);
}
void imp_UpdateWindow(void) { RET(UpdateWindow((HWND)HHOST(ARG(0)))); STDRET(1); }
void imp_GetClientRect(void) { RET(GetClientRect((HWND)HHOST(ARG(0)), (RECT*)PTR(ARG(1)))); STDRET(2); }
void imp_AdjustWindowRectEx(void) { RET(AdjustWindowRectEx((RECT*)PTR(ARG(0)), ARG(1), ARG(2), ARG(3))); STDRET(4); }
void imp_GetSystemMetrics(void) { RET(GetSystemMetrics((int)ARG(0))); STDRET(1); }
void imp_ShowCursor(void) { RET(ShowCursor((BOOL)ARG(0))); STDRET(1); }
void imp_GetDC(void) { RET(H32(GetDC((HWND)HHOST(ARG(0))))); STDRET(1); }
void imp_ReleaseDC(void) { RET(ReleaseDC((HWND)HHOST(ARG(0)), (HDC)HHOST(ARG(1)))); STDRET(2); }
void imp_GetFocus(void) {
    HWND f = g_force_focus && g_hwnd ? g_hwnd : GetFocus();
    RET(H32(f)); STDRET(0);
}

/* ---- keyboard ---- */
void imp_GetAsyncKeyState(void) {  /* (vk) -> SHORT */
    int vk = (int)(ARG(0) & 0xFF);
    SHORT s = GetAsyncKeyState(vk);
    if (script_down(vk)) s = (SHORT)0x8001;
    RET((uint32_t)(int32_t)s); STDRET(1);
}

/* ---- misc ---- */
void imp_CharUpperBuffA(void) { RET(CharUpperBuffA((LPSTR)PTR(ARG(0)), ARG(1))); STDRET(2); }
void imp_MessageBeep(void) { RET(g_wd_quiet ? 1 : MessageBeep(ARG(0))); STDRET(1); }
void imp_MessageBoxA(void) {  /* (hwnd, text, caption, type) */
    char text[512], cap[128];
    guest_str(ARG(1), text, sizeof text);
    guest_str(ARG(2), cap, sizeof cap);
    fprintf(stderr, "[user] MessageBox \"%s\": %s\n", cap, text);
    int r = g_wd_quiet ? IDOK : MessageBoxA(g_hwnd, text, cap, ARG(3));
    RET(r); STDRET(4);
}

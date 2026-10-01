#ifndef WD_INPUT_MAP_H
#define WD_INPUT_MAP_H
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "guest_win32.h"

/* Device-level remapping, the pure part (no SDL, no globals): key names, and
 * the parsers and lookups behind WD_KEYMAP (user.c) and WD_PADMAP /
 * WD_PAD_DIRECTION (winmm.c). One physical input stands in for another; the
 * host knows nothing about game actions. tests/recomp/test_input_map.py
 * compiles this header alone.
 *
 * A key is a virtual-key pair "specific | generic << 8", the shape user.c's
 * vk_of gives a held key: LSHIFT | SHIFT << 8, but plain 'W' or UP alone. */

typedef void (*WdMapBad)(void* ctx, const char* entry, size_t len, const char* why);

static inline int wd_name_is(const char* s, size_t n, const char* name) {
    if (strlen(name) != n) return 0;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)name[i])) return 0;
    return 1;
}

/* Key name -> virtual key, 0 if unknown. WD_KEYS and the remaps share it.
 * Letters and digits by themselves, F1-F24, KP0-KP9, and the names below. */
static inline uint8_t wd_vk_from_name(const char* s, size_t n) {
    static const struct { const char* name; uint8_t vk; } tab[] = {
        {"UP", W32_VK_UP}, {"DOWN", W32_VK_DOWN}, {"LEFT", W32_VK_LEFT}, {"RIGHT", W32_VK_RIGHT},
        {"RETURN", W32_VK_RETURN}, {"ENTER", W32_VK_RETURN}, {"ESC", W32_VK_ESCAPE},
        {"ESCAPE", W32_VK_ESCAPE}, {"SPACE", W32_VK_SPACE}, {"TAB", W32_VK_TAB},
        {"BACK", W32_VK_BACK}, {"BACKSPACE", W32_VK_BACK},
        {"CTRL", W32_VK_CONTROL}, {"SHIFT", W32_VK_SHIFT}, {"ALT", W32_VK_MENU},
        {"LCTRL", W32_VK_LCONTROL}, {"RCTRL", W32_VK_RCONTROL}, {"LSHIFT", W32_VK_LSHIFT},
        {"RSHIFT", W32_VK_RSHIFT}, {"LALT", W32_VK_LMENU}, {"RALT", W32_VK_RMENU},
        {"INSERT", W32_VK_INSERT}, {"DELETE", W32_VK_DELETE}, {"HOME", W32_VK_HOME},
        {"END", W32_VK_END}, {"PAGEUP", W32_VK_PRIOR}, {"PAGEDOWN", W32_VK_NEXT},
        {"MINUS", W32_VK_OEM_MINUS}, {"EQUALS", W32_VK_OEM_PLUS}, {"COMMA", W32_VK_OEM_COMMA},
        {"PERIOD", W32_VK_OEM_PERIOD}, {"SLASH", W32_VK_OEM_2}, {"SEMICOLON", W32_VK_OEM_1},
        {"QUOTE", W32_VK_OEM_7}, {"GRAVE", W32_VK_OEM_3}, {"LBRACKET", W32_VK_OEM_4},
        {"RBRACKET", W32_VK_OEM_6}, {"BACKSLASH", W32_VK_OEM_5},
    };
    if (!n) return 0;
    if (n == 1 && isalnum((unsigned char)s[0])) return (uint8_t)toupper((unsigned char)s[0]);
    if ((s[0] == 'F' || s[0] == 'f') && n >= 2 && n <= 3) {   /* F1 .. F24 */
        unsigned v = 0;
        size_t i = 1;
        for (; i < n && isdigit((unsigned char)s[i]); i++) v = v * 10 + (unsigned)(s[i] - '0');
        if (i == n && v >= 1 && v <= 24) return (uint8_t)(W32_VK_F1 + v - 1);
    }
    if (n == 3 && (s[0] == 'K' || s[0] == 'k') && (s[1] == 'P' || s[1] == 'p') && isdigit((unsigned char)s[2]))
        return (uint8_t)(W32_VK_NUMPAD0 + (s[2] - '0'));
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (wd_name_is(s, n, tab[i].name)) return tab[i].vk;
    return 0;
}

/* A virtual key as a held key reports it: the modifiers carry their generic
 * VK too (a bare CTRL, SHIFT or ALT is the left one). */
static inline uint16_t wd_vk_expand(uint8_t vk) {
    switch (vk) {
    case W32_VK_SHIFT: case W32_VK_LSHIFT: return W32_VK_LSHIFT | W32_VK_SHIFT << 8;
    case W32_VK_RSHIFT: return W32_VK_RSHIFT | W32_VK_SHIFT << 8;
    case W32_VK_CONTROL: case W32_VK_LCONTROL: return W32_VK_LCONTROL | W32_VK_CONTROL << 8;
    case W32_VK_RCONTROL: return W32_VK_RCONTROL | W32_VK_CONTROL << 8;
    case W32_VK_MENU: case W32_VK_LMENU: return W32_VK_LMENU | W32_VK_MENU << 8;
    case W32_VK_RMENU: return W32_VK_RMENU | W32_VK_MENU << 8;
    default: return vk;
    }
}

/* Next "a=b" entry of a comma-separated list, trimmed. Empty entries are
 * skipped. Returns 0 at the end of the list. */
static inline int wd_map_next(const char** spec, const char** e, size_t* len) {
    const char* p = *spec;
    while (p && *p) {
        const char* end = strchr(p, ',');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        *spec = end ? end + 1 : p + n;
        while (n && isspace((unsigned char)*p)) { p++; n--; }
        while (n && isspace((unsigned char)p[n - 1])) n--;
        if (n) { *e = p; *len = n; return 1; }
        p = *spec;
    }
    return 0;
}
/* Split "left=right", both sides trimmed and non-empty. */
static inline int wd_map_pair(const char* e, size_t n, const char** l, size_t* ln, const char** r, size_t* rn) {
    const char* eq = memchr(e, '=', n);
    if (!eq || memchr(eq + 1, '=', n - (size_t)(eq + 1 - e))) return 0;
    *l = e; *ln = (size_t)(eq - e);
    *r = eq + 1; *rn = n - *ln - 1;
    while (*ln && isspace((unsigned char)(*l)[*ln - 1])) (*ln)--;
    while (*rn && isspace((unsigned char)**r)) { (*r)++; (*rn)--; }
    return *ln && *rn;
}

/* ---- WD_KEYMAP=PHYSICAL=GAME,... ---- */
#define WD_KEYMAP_MAX 64
typedef struct { uint8_t from[WD_KEYMAP_MAX], to[WD_KEYMAP_MAX]; int n; } WdKeyMap;

/* Parses into m (cleared first); bad entries are reported and left out.
 * Returns the number of bad entries. */
static inline int wd_keymap_parse(WdKeyMap* m, const char* spec, WdMapBad bad, void* ctx) {
    const char *e, *l, *r;
    size_t n, ln, rn;
    int nbad = 0;
    m->n = 0;
    while (wd_map_next(&spec, &e, &n)) {
        const char* why = NULL;
        uint8_t from = 0, to = 0;
        if (!wd_map_pair(e, n, &l, &ln, &r, &rn)) why = "want PHYSICAL=GAME";
        else if (!(from = wd_vk_from_name(l, ln))) why = "unknown physical key name";
        else if (!(to = wd_vk_from_name(r, rn))) why = "unknown game key name";
        else {
            for (int i = 0; i < m->n; i++) if (m->from[i] == from) why = "physical key already mapped";
            if (m->n == WD_KEYMAP_MAX) why = "too many entries";
        }
        if (why) { nbad++; if (bad) bad(ctx, e, n, why); continue; }
        m->from[m->n] = from; m->to[m->n] = to; m->n++;
    }
    return nbad;
}

/* The key the game sees for a physical key v (a vk_of result, 0 = none). A
 * pair names the specific key (LCTRL) or the generic one (CTRL, either side);
 * the specific pair wins. An unlisted key is itself. */
static inline uint16_t wd_keymap_apply(const WdKeyMap* m, uint16_t v) {
    if (!m->n || !v) return v;
    for (int pass = 0; pass < 2; pass++) {
        uint8_t key = pass ? (uint8_t)(v >> 8) : (uint8_t)v;
        if (!key) continue;
        for (int i = 0; i < m->n; i++)
            if (m->from[i] == key) return wd_vk_expand(m->to[i]);
    }
    return v;
}

/* ---- WD_PAD_DIRECTION=stick|dpad|both ---- */
#define WD_DIR_STICK 1
#define WD_DIR_DPAD  2
static inline int wd_dir_parse(const char* s) {   /* WD_DIR_* bits, 0 = invalid */
    size_t n = s ? strlen(s) : 0;
    return wd_name_is(s, n, "stick") ? WD_DIR_STICK : wd_name_is(s, n, "dpad") ? WD_DIR_DPAD
         : wd_name_is(s, n, "both") ? WD_DIR_STICK | WD_DIR_DPAD : 0;
}

/* ---- WD_PADMAP=BUTTON=TARGET,... ---- */
/* The pad's buttons in the order of the names, SDL south east west north for
 * a b x y. lt and rt are triggers: keys mode treats them as buttons (lt is
 * '3' by default, as the game's DOS layout has it); in winmm mode they are
 * the Z axis, not buttons, and cannot be mapped. */
enum { WD_PB_A, WD_PB_B, WD_PB_X, WD_PB_Y, WD_PB_LB, WD_PB_RB, WD_PB_BACK, WD_PB_START,
       WD_PB_LS, WD_PB_RS, WD_PB_LT, WD_PB_RT, WD_PB_N };
static const char* const wd_pb_names[WD_PB_N] = {
    "a", "b", "x", "y", "lb", "rb", "back", "start", "ls", "rs", "lt", "rt",
};
#define WD_JOY_BUTTONS 32   /* joyGetDevCaps wMaxButtons */

typedef struct {
    uint8_t joy[WD_PB_N];    /* winmm: joystick button 1..32 the game sees, 0 = none */
    uint16_t key[WD_PB_N];   /* keys: the key pressed (specific | generic << 8), 0 = none */
} WdPadMap;

/* The map as shipped: buttons 1..10 A B X Y LB RB Back Start LS RS (WinMM's
 * view of an XInput pad), and the docs/research/running.md key layout. */
static inline void wd_padmap_defaults(WdPadMap* m) {
    static const uint8_t keys[WD_PB_N] = {
        W32_VK_CONTROL, W32_VK_DOWN, W32_VK_MENU, W32_VK_SPACE, '1', '2', W32_VK_RETURN, W32_VK_ESCAPE, 0, 0, '3', 0,
    };
    for (int i = 0; i < WD_PB_N; i++) {
        m->joy[i] = i < 10 ? (uint8_t)(i + 1) : 0;
        m->key[i] = keys[i] ? wd_vk_expand(keys[i]) : 0;
    }
}

/* Applies the entries over the defaults already in m; bad ones are reported
 * and left out. keys_mode picks what TARGET is: a key name, or buttonN.
 * Returns the number of bad entries. */
static inline int wd_padmap_parse(WdPadMap* m, int keys_mode, const char* spec, WdMapBad bad, void* ctx) {
    const char *e, *l, *r;
    size_t n, ln, rn;
    int nbad = 0;
    uint32_t seen = 0;
    while (wd_map_next(&spec, &e, &n)) {
        const char* why = NULL;
        int b = -1;
        unsigned num = 0;
        uint8_t vk = 0;
        if (!wd_map_pair(e, n, &l, &ln, &r, &rn)) why = "want BUTTON=TARGET";
        else {
            for (int i = 0; i < WD_PB_N; i++) if (wd_name_is(l, ln, wd_pb_names[i])) b = i;
            if (b < 0) why = "unknown button name (a b x y lb rb back start ls rs lt rt)";
            else if (seen >> b & 1) why = "button already mapped";
            else if (keys_mode) { if (!(vk = wd_vk_from_name(r, rn))) why = "unknown key name"; }
            else if (b >= WD_PB_LT) why = "lt and rt are the Z axis in winmm mode, not buttons";
            else {
                size_t i = 6;
                if (rn < 7 || rn > 8 || !wd_name_is(r, 6, "button")) why = "want button1 .. button32";
                else {
                    for (; i < rn && isdigit((unsigned char)r[i]); i++) num = num * 10 + (unsigned)(r[i] - '0');
                    if (i != rn || r[6] == '0' || num < 1 || num > WD_JOY_BUTTONS) why = "want button1 .. button32";
                }
            }
        }
        if (why) { nbad++; if (bad) bad(ctx, e, n, why); continue; }
        seen |= 1u << b;
        if (keys_mode) m->key[b] = wd_vk_expand(vk);
        else m->joy[b] = (uint8_t)num;
    }
    return nbad;
}

/* Does the key vk belong to the key that button b presses? (A held modifier
 * answers to its specific and its generic VK.) */
static inline int wd_padmap_key_is(const WdPadMap* m, int b, int vk) {
    return m->key[b] && (vk == (m->key[b] & 0xFF) || vk == m->key[b] >> 8);
}

#endif /* WD_INPUT_MAP_H */

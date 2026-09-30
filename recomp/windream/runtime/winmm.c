/*
 * WINDREAM recompilation - WINMM bridges on SDL3: timer, joystick, CD audio.
 *
 * timeGetTime counts from system start on Windows, so it is never near 0 on
 * a running machine; here it is SDL's millisecond tick plus one minute.
 *
 * Joysticks are SDL gamepads, shown the way WinMM shows an XInput pad on
 * Windows: X/Y the left stick, Z the triggers, R/U the right stick (0..65535,
 * centre 32768), the POV the d-pad, buttons 1..10 A B X Y LB RB Back Start
 * LS RS. The game (JOY_InitPovDevices 0x44091d, JOY_InitDevices 0x440bc2)
 * probes ids 0 and 1 with joyGetPosEx (JOYERR_UNPLUGGED = absent), reads
 * wNumButtons and JOYCAPS_HASPOV, and keeps the X/Y it reads at start as the
 * centre; JOY_PollPov (0x440ad3) and JOY_Poll (0x440d3d) read X, Y, buttons
 * and the 4-way POV (docs/engine.md, "Joystick path"). A pad takes the first
 * free id when it connects; the game only looks for joysticks at start.
 * Assumption, unverified: a diagonal on the 4-way POV reads as its
 * horizontal direction.
 *
 * Sticks and triggers pass through a scaled radial deadzone before the game
 * sees them (host-only: WinMM applied none, and 1998 sticks were calibrated in
 * the control panel). Below the inner radius a stick reads exactly centred,
 * which also keeps the centre the game takes at start clean; past the outer
 * radius it reads full; in between it is rescaled, direction kept.
 *
 * CD audio is emulated. The game drives an MCI "cdaudio" device (CD_OpenAudio
 * 0x4042f1 and friends: OPEN, SET time format TMSF, STATUS number of tracks /
 * mode, PLAY from track n to n+1, STOP, PAUSE, RESUME). There is no CD drive;
 * the tracks are the disc image's raw Red Book files "... (Track NN).bin"
 * (44.1 kHz 16-bit stereo), played through the DirectSound mixer.
 *
 *   WD_PAD=winmm|keys|off  winmm (default): pads are WinMM joysticks (press J
 *               in game). keys: pads press keys instead, with the
 *               docs/running.md layout (stick and d-pad arrows, A Ctrl, X Alt,
 *               Y Space, B Down, LB 1, RB 2, LT 3, Start Esc) plus Back as
 *               Return for the menus; joysticks then read as unplugged.
 *               off: no pads.
 *   WD_DEADZONE=10,95  inner and outer deadzone, percent of full deflection
 *   WD_CD_DIR   directory holding the "(Track NN).bin" files; default: the
 *               parent of the directory holding the EXE (the disc image folder)
 */
#define RECOMP_GENERATED_CODE
#include "host.h"

void imp_timeGetTime(void) { RET((uint32_t)SDL_GetTicks() + 60000u); STDRET(0); }

/* ---- joysticks ---- */
#define JOY_IDS 16   /* joyGetNumDevs on NT: the number of ids, not of devices */
static SDL_Gamepad* g_pad[JOY_IDS];
static enum { PAD_WINMM, PAD_KEYS, PAD_OFF } g_pad_mode;
static SDL_InitState g_joy_init;
static float g_dz_in = 0.10f, g_dz_out = 0.95f;

static void pad_open(SDL_JoystickID id) {
    int free_id = -1;
    for (int i = JOY_IDS - 1; i >= 0; i--) {
        if (g_pad[i] && SDL_GetGamepadID(g_pad[i]) == id) return;
        if (!g_pad[i]) free_id = i;
    }
    if (free_id < 0) return;
    g_pad[free_id] = SDL_OpenGamepad(id);
    if (g_pad[free_id]) fprintf(stderr, "[joy] id %d: %s\n", free_id, SDL_GetGamepadName(g_pad[free_id]));
    else fprintf(stderr, "[joy] cannot open gamepad: %s\n", SDL_GetError());
}

void joy_init(void) {
    if (!SDL_ShouldInit(&g_joy_init)) return;
    const char* m = host_env("WD_PAD");
    g_pad_mode = m && !SDL_strcasecmp(m, "keys") ? PAD_KEYS : m && !SDL_strcasecmp(m, "off") ? PAD_OFF : PAD_WINMM;
    const char* dz = host_env("WD_DEADZONE");
    if (dz) {
        char* end;
        float in = (float)SDL_strtod(dz, &end), out = 95.0f;
        if (*end == ',') out = (float)SDL_strtod(end + 1, NULL);
        if (in >= 0 && in < out && out <= 100) { g_dz_in = in / 100; g_dz_out = out / 100; }
        else fprintf(stderr, "[joy] WD_DEADZONE=%s ignored: want inner,outer with 0 <= inner < outer <= 100\n", dz);
    }
    if (g_pad_mode != PAD_OFF) {
        if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            int n = 0;
            SDL_JoystickID* ids = SDL_GetGamepads(&n);
            for (int i = 0; i < n; i++) pad_open(ids[i]);
            SDL_free(ids);
            fprintf(stderr, "[joy] %d gamepad(s), mode %s, deadzone inner %g%% outer %g%%\n", n,
                    g_pad_mode == PAD_KEYS ? "keys" : "winmm", g_dz_in * 100.0, g_dz_out * 100.0);
        } else fprintf(stderr, "[joy] SDL gamepad: %s\n", SDL_GetError());
    }
    SDL_SetInitialized(&g_joy_init, true);
}

static SDL_Gamepad* pad_at(uint32_t id) {
    joy_init();
    return g_pad_mode == PAD_WINMM && id < JOY_IDS ? g_pad[id] : NULL;
}

static float dz_scale(float m) {
    return m <= g_dz_in ? 0.0f : SDL_min((m - g_dz_in) / (g_dz_out - g_dz_in), 1.0f);
}
/* A stick after the deadzone, -32767..32767 per axis. */
static void stick(SDL_Gamepad* p, SDL_GamepadAxis ax, SDL_GamepadAxis ay, int* x, int* y) {
    float fx = SDL_GetGamepadAxis(p, ax) / 32767.0f, fy = SDL_GetGamepadAxis(p, ay) / 32767.0f;
    float m = SDL_sqrtf(fx * fx + fy * fy), k = m > 0 ? dz_scale(m) / m : 0.0f;
    *x = (int)SDL_clamp(fx * k * 32767.0f, -32767.0f, 32767.0f);
    *y = (int)SDL_clamp(fy * k * 32767.0f, -32767.0f, 32767.0f);
}
/* A trigger after the deadzone, 0..32767. */
static int trigger(SDL_Gamepad* p, SDL_GamepadAxis a) {
    return (int)(dz_scale(SDL_GetGamepadAxis(p, a) / 32767.0f) * 32767.0f);
}

/* WD_PAD=keys */
static const struct { uint8_t vk; SDL_GamepadButton b; } g_keymap[] = {
    {W32_VK_CONTROL, SDL_GAMEPAD_BUTTON_SOUTH}, {W32_VK_LCONTROL, SDL_GAMEPAD_BUTTON_SOUTH},
    {W32_VK_MENU, SDL_GAMEPAD_BUTTON_WEST}, {W32_VK_LMENU, SDL_GAMEPAD_BUTTON_WEST},
    {W32_VK_SPACE, SDL_GAMEPAD_BUTTON_NORTH}, {W32_VK_DOWN, SDL_GAMEPAD_BUTTON_EAST},
    {'1', SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {'2', SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {W32_VK_ESCAPE, SDL_GAMEPAD_BUTTON_START}, {W32_VK_RETURN, SDL_GAMEPAD_BUTTON_BACK},
    {W32_VK_UP, SDL_GAMEPAD_BUTTON_DPAD_UP}, {W32_VK_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {W32_VK_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {W32_VK_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
};
#define STICK_KEY 16384   /* half deflection */

int joy_key_down(int vk) {
    if (g_pad_mode != PAD_KEYS) return 0;
    for (int i = 0; i < JOY_IDS; i++) {
        SDL_Gamepad* p = g_pad[i];
        if (!p) continue;
        for (size_t k = 0; k < sizeof g_keymap / sizeof g_keymap[0]; k++)
            if (g_keymap[k].vk == vk && SDL_GetGamepadButton(p, g_keymap[k].b)) return 1;
        int x, y;
        stick(p, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, &x, &y);
        if ((vk == W32_VK_LEFT && x < -STICK_KEY) || (vk == W32_VK_RIGHT && x > STICK_KEY) ||
            (vk == W32_VK_UP && y < -STICK_KEY) || (vk == W32_VK_DOWN && y > STICK_KEY) ||
            (vk == '3' && trigger(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > STICK_KEY))
            return 1;
    }
    return 0;
}

void joy_event(const SDL_Event* e) {
    switch (e->type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        if (g_pad_mode != PAD_OFF) pad_open(e->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        for (int i = 0; i < JOY_IDS; i++)
            if (g_pad[i] && SDL_GetGamepadID(g_pad[i]) == e->gdevice.which) {
                fprintf(stderr, "[joy] id %d removed\n", i);
                SDL_CloseGamepad(g_pad[i]);
                g_pad[i] = NULL;
            }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (g_pad_mode == PAD_KEYS)
            for (size_t k = 0; k < sizeof g_keymap / sizeof g_keymap[0]; k++)
                if (g_keymap[k].b == e->gbutton.button) host_key_latch(g_keymap[k].vk);
        break;
    default: break;
    }
}

void imp_joyGetNumDevs(void) { joy_init(); RET(JOY_IDS); STDRET(0); }

void imp_joyGetDevCapsA(void) {  /* (id, JOYCAPSA*, size) */
    SDL_Gamepad* p = pad_at(ARG(0));
    uint32_t c = ARG(1);
    if (!p || !c || ARG(2) < W32_JOYCAPSA_SIZE) { RET(W32_JOYERR_PARMS); STDRET(3); return; }
    memset(wd_host_range(c, W32_JOYCAPSA_SIZE, 1), 0, W32_JOYCAPSA_SIZE);
    WD_HOST_WRITE16(c + W32_JC_MID) = SDL_GetGamepadVendor(p);
    WD_HOST_WRITE16(c + W32_JC_PID) = SDL_GetGamepadProduct(p);
    const char* name = SDL_GetGamepadName(p);
    guest_strcpy_out(c + W32_JC_PNAME, 32, name ? name : "Gamepad");
    for (uint32_t i = 0; i < 3; i++) {   /* X Y Z, then R U V: 0..65535 */
        WD_HOST_WRITE32(c + W32_JC_XMIN + 8 * i + 4) = 0xFFFF;
        WD_HOST_WRITE32(c + W32_JC_RMIN + 8 * i + 4) = 0xFFFF;
    }
    WD_HOST_WRITE32(c + W32_JC_NUMBUTTONS) = 10;
    WD_HOST_WRITE32(c + W32_JC_PERIODMIN) = 10;
    WD_HOST_WRITE32(c + W32_JC_PERIODMAX) = 1000;
    WD_HOST_WRITE32(c + W32_JC_CAPS) = W32_JOYCAPS_HASZ | W32_JOYCAPS_HASR | W32_JOYCAPS_HASU | W32_JOYCAPS_HASPOV | W32_JOYCAPS_POV4DIR;
    WD_HOST_WRITE32(c + W32_JC_MAXAXES) = 6;
    WD_HOST_WRITE32(c + W32_JC_NUMAXES) = 5;
    WD_HOST_WRITE32(c + W32_JC_MAXBUTTONS) = 32;
    RET(W32_JOYERR_NOERROR); STDRET(3);
}

void imp_joyGetPosEx(void) {  /* (id, JOYINFOEX*) */
    SDL_Gamepad* p = pad_at(ARG(0));
    uint32_t ji = ARG(1);
    if (!p) { RET(W32_JOYERR_UNPLUGGED); STDRET(2); return; }
    if (!ji || WD_HOST_READ32(ji) != W32_JOYINFOEX_SIZE) { RET(W32_JOYERR_PARMS); STDRET(2); return; }
    uint32_t fl = WD_HOST_READ32(ji + W32_JI_FLAGS);
    int lx, ly, rx, ry;
    stick(p, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, &lx, &ly);
    stick(p, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, &rx, &ry);
    int z = trigger(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) - trigger(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    static uint8_t logged[JOY_IDS];
    if (!logged[ARG(0)]++)   /* the first read is the centre JOY_Init* keep */
        fprintf(stderr, "[joy] id %u first read: left stick raw %d,%d -> %d,%d after the deadzone\n", ARG(0),
                SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX), SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY), lx, ly);
    uint32_t v[6] = {   /* 0..65535, centre 32768 */
        (uint32_t)(lx + 32768), (uint32_t)(ly + 32768), (uint32_t)SDL_clamp(z + 32768, 0, 65535),
        (uint32_t)(ry + 32768), (uint32_t)(rx + 32768), 32768,
    };
    for (uint32_t i = 0; i < 6; i++)
        if (fl & (W32_JOY_RETURNX << i)) WD_HOST_WRITE32(ji + W32_JI_XPOS + 4 * i) = v[i];
    if (fl & W32_JOY_RETURNBUTTONS) {
        static const SDL_GamepadButton order[10] = {
            SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,
            SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
        };
        uint32_t bits = 0, n = 0;
        for (uint32_t i = 0; i < 10; i++)
            if (SDL_GetGamepadButton(p, order[i])) { bits |= 1u << i; n++; }
        WD_HOST_WRITE32(ji + W32_JI_BUTTONS) = bits;
        WD_HOST_WRITE32(ji + W32_JI_BUTTONNUMBER) = n;
    }
    if (fl & W32_JOY_RETURNPOV) {
        int up = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_UP), dn = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        int lf = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_LEFT), rt = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        WD_HOST_WRITE32(ji + W32_JI_POV) = rt && !lf ? 9000 : lf && !rt ? 27000 : up && !dn ? 0 : dn && !up ? 18000 : W32_JOY_POVCENTERED;
    }
    RET(W32_JOYERR_NOERROR); STDRET(2);
}

/* ---- CD audio ---- */
#define CD_DEVICE 1u
static char g_track[100][1024];
static int g_ntracks = -1;
static int g_cur;            /* track playing or paused, 0 = none */
static int g_paused;

static char* last_sep(char* s) {
    char* a = strrchr(s, '/');
    char* b = strrchr(s, '\\');
    return a > b ? a : b;
}

static void cd_scan(void) {
    char dir[1024];
    const char* env = host_env("WD_CD_DIR");
    if (env) SDL_strlcpy(dir, env, sizeof dir);
    else {   /* the directory above the one holding the EXE */
        const char* exe = g_wd_exe ? g_wd_exe : "";
        int absolute = exe[0] == '/' || exe[0] == '\\' || (exe[0] && exe[1] == ':');
        char* cwd = absolute ? NULL : SDL_GetCurrentDirectory();
        SDL_snprintf(dir, sizeof dir, "%s%s", cwd ? cwd : "", exe);
        SDL_free(cwd);
        for (int k = 0; k < 2; k++) { char* s = last_sep(dir); if (s) *s = 0; }
    }
    g_ntracks = 0;
    int n = 0;
    char** names = SDL_GlobDirectory(dir, "*(Track *).bin", SDL_GLOB_CASEINSENSITIVE, &n);
    for (int i = 0; i < n; i++) {
        const char* t = SDL_strstr(names[i], "(Track ");
        int k = t ? SDL_atoi(t + 7) : 0;
        if (k > 0 && k < 100) {
            SDL_snprintf(g_track[k], sizeof g_track[k], "%s/%s", dir, names[i]);
            if (k > g_ntracks) g_ntracks = k;
        }
    }
    SDL_free(names);
    fprintf(stderr, "[cd] %d tracks in %s\n", g_ntracks, dir);
}

static uint32_t cd_mode(void) {
    if (g_cur && g_paused) return W32_MCI_MODE_PAUSE;
    if (g_cur && mixer_cd_playing()) return W32_MCI_MODE_PLAY;
    return W32_MCI_MODE_STOP;
}

void imp_mciSendCommandA(void) {  /* (device, msg, flags, parms): parms use the 32-bit layout */
    uint32_t dev = ARG(0), msg = ARG(1), flags = ARG(2), p = ARG(3);
    uint32_t err = 0;
    if (g_ntracks < 0) cd_scan();
    switch (msg) {
    case W32_MCI_OPEN: {
        char type[32] = "";
        if (flags & W32_MCI_OPEN_TYPE) guest_str(WD_HOST_READ32(p + W32_MCI_OPEN_DEVICETYPE), type, sizeof type);
        if (SDL_strcasecmp(type, "cdaudio")) { err = W32_MCIERR_UNRECOGNIZED_KEYWORD; break; }
        WD_HOST_WRITE32(p + W32_MCI_OPEN_DEVICEID) = CD_DEVICE;
        break;
    }
    case W32_MCI_CLOSE: mixer_cd_play(NULL, 0); g_cur = 0; break;
    case W32_MCI_SET:
        if (flags & W32_MCI_SET_DOOR_OPEN) { mixer_cd_play(NULL, 0); g_cur = 0; }
        break;
    case W32_MCI_STATUS: {
        if (!(flags & W32_MCI_STATUS_ITEM)) { err = W32_MCIERR_MISSING_PARAMETER; break; }
        uint32_t* ret = (uint32_t*)wd_host_range(p + W32_MCI_STATUS_RETURN, 4, 1);
        switch (WD_HOST_READ32(p + W32_MCI_STATUS_ITEMOFF)) {
        case W32_MCI_STATUS_NUMBER_OF_TRACKS: *ret = (uint32_t)g_ntracks; break;
        case W32_MCI_STATUS_MODE: *ret = cd_mode(); break;
        case W32_MCI_STATUS_CURRENT_TRACK: *ret = (uint32_t)g_cur; break;
        case W32_MCI_STATUS_MEDIA_PRESENT: case W32_MCI_STATUS_READY: *ret = 1; break;
        default: *ret = 0; break;
        }
        break;
    }
    case W32_MCI_PLAY: {
        int n = flags & W32_MCI_FROM ? (int)(WD_HOST_READ32(p + W32_MCI_PLAY_FROM) & 0xFF) : g_cur;
        if (n < 1 || n > g_ntracks || !g_track[n][0]) { err = W32_MCIERR_OUTOFRANGE; break; }
        mixer_cd_play(g_track[n], n);
        g_cur = n; g_paused = 0;
        break;
    }
    case W32_MCI_STOP: mixer_cd_play(NULL, 0); g_cur = 0; g_paused = 0; break;
    case W32_MCI_PAUSE: if (g_cur) { mixer_cd_pause(1); g_paused = 1; } break;
    case W32_MCI_RESUME: if (g_cur) { mixer_cd_pause(0); g_paused = 0; } break;
    case W32_MCI_SEEK: break;
    default:
        fprintf(stderr, "[cd] unhandled MCI command 0x%X flags 0x%X\n", msg, flags);
        break;
    }
    if (dev != CD_DEVICE && msg != W32_MCI_OPEN && !err) err = W32_MCIERR_INVALID_DEVICE_ID;
    RET(err); STDRET(4);
}

void imp_mciGetErrorStringA(void) {  /* (err, buf, len) -> known */
    static const struct { uint32_t err; const char* text; } tab[] = {
        {W32_MCIERR_INVALID_DEVICE_ID, "Invalid device ID."},
        {W32_MCIERR_UNRECOGNIZED_KEYWORD, "The driver cannot recognize the specified command parameter."},
        {W32_MCIERR_OUTOFRANGE, "The specified parameter is out of range for the specified command."},
        {W32_MCIERR_MISSING_PARAMETER, "The specified command requires a parameter."},
    };
    const char* s = NULL;
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (tab[i].err == ARG(0)) s = tab[i].text;
    guest_strcpy_out(ARG(1), ARG(2), s ? s : "Unknown MCI error.");
    RET(s != NULL); STDRET(3);
}

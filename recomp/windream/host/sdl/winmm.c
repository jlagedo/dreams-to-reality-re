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
 * and the 4-way POV (docs/research/engine.md, "Joystick path"). A pad takes the first
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
 * the tracks are the disc image's raw Red Book audio (44.1 kHz 16-bit stereo),
 * played through the DirectSound mixer. In disc mode (WD_DISC1 and WD_DISC2,
 * files.c) they are the active disc's, as its .cue lists them: a track plays
 * from its INDEX 01, after the pregap, for its own length, whether the image
 * has one .bin per track or one for all; a change of the active disc stops
 * the music and the device then has the other disc's tracks. Otherwise they
 * are the "... (Track NN).bin" files of one directory (WD_CD_DIR), each played
 * whole. The number of tracks is the disc's, data track included, as a drive
 * reports it (12 on disc 1, 14 on disc 2); a data track does not play.
 * MCI_PLAY with no start position and no current track succeeds and plays
 * nothing: CD_PromptSwap ends with that call (MGM 0x23, CD_ResumeAudio), and
 * an error there is shown to the player as an "MCI Error" box. What a real
 * drive answers is unverified. MCI_PLAY of any track succeeds and plays
 * nothing when the device has no audio track at all (an .iso image, which has
 * no music), for the same reason; with audio tracks, a track that is not one
 * of them is still MCIERR_OUTOFRANGE.
 *
 *   WD_PAD=winmm|keys|off  winmm (default): pads are WinMM joysticks (press J
 *               in game). keys: pads press keys instead, with the
 *               docs/research/running.md layout (stick and d-pad arrows, A Ctrl, X Alt,
 *               Y Space, B Down, LB 1, RB 2, LT 3, Start Esc) plus Back as
 *               Return for the menus; joysticks then read as unplugged.
 *               off: no pads.
 *   WD_PAD_DIRECTION=stick|dpad|both  which control gives direction.
 *               winmm: stick (default) the left stick drives the X/Y axes and
 *               the d-pad the POV hat; dpad the d-pad drives X/Y at full
 *               deflection, the stick does nothing and the POV reads centred;
 *               both either drives X/Y (per axis, the d-pad wins when pressed;
 *               POV as with stick). keys: both (default) the stick and the
 *               d-pad press the arrow keys; stick only the stick; dpad only the d-pad.
 *   WD_PADMAP=a=button5,start=button1  BUTTON=TARGET pairs over the default
 *               layout. BUTTON: a b x y (SDL south east west north), lb rb
 *               back start ls rs, and in keys mode lt rt (the triggers read as
 *               buttons past half deflection; in winmm mode they are the Z
 *               axis, not buttons, and are rejected). TARGET: winmm
 *               button1..button32, the joystick button the game sees (the
 *               default is a b x y lb rb back start ls rs = 1..10, and
 *               wNumButtons grows to the highest one named); keys a key name
 *               as WD_KEYMAP in user.c (default as in WD_PAD=keys: a Ctrl, b
 *               Down, x Alt, y Space, lb 1, rb 2, lt 3, back Return, start
 *               Esc; ls rs rt none). Unlisted buttons keep their default; a
 *               bad entry is logged and skipped. The d-pad and stick are
 *               directions (WD_PAD_DIRECTION), not mapped buttons.
 *   WD_DEADZONE=10,95  inner and outer deadzone, percent of full deflection
 *   WD_CD_DIR   directory holding the "(Track NN).bin" files; default: the
 *               parent of the directory holding the EXE (the disc image
 *               folder). Not used in disc mode.
 */
#define RECOMP_GENERATED_CODE
#include "host.h"
#include "input_map.h"
#include "disc.h"
#include "render_live.h"

void imp_timeGetTime(void) {
    wd_render_display_point();   /* display interpolation's frames while the game computes */
    RET((uint32_t)SDL_GetTicks() + 60000u); STDRET(0);
}

/* ---- joysticks ---- */
#define JOY_IDS 16   /* joyGetNumDevs on NT: the number of ids, not of devices */
static SDL_Gamepad* g_pad[JOY_IDS];
static enum { PAD_WINMM, PAD_KEYS, PAD_OFF } g_pad_mode;
static SDL_InitState g_joy_init;
static float g_dz_in = 0.10f, g_dz_out = 0.95f;
static WdPadMap g_padmap;              /* WD_PADMAP over the defaults: one table for both modes */
static int g_dir;                      /* WD_DIR_* bits: the controls that give direction */
static uint32_t g_joy_nbuttons = 10;   /* wNumButtons: the default ten, or up to the highest mapped */

static void padmap_bad(void* ctx, const char* e, size_t n, const char* why) {
    (void)ctx;
    fprintf(stderr, "[joy] WD_PADMAP entry \"%.*s\" ignored: %s\n", (int)n, e, why);
}

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
    /* direction source and button table; unset gives the shipped behaviour */
    g_dir = g_pad_mode == PAD_KEYS ? WD_DIR_STICK | WD_DIR_DPAD : WD_DIR_STICK;
    wd_padmap_defaults(&g_padmap);
    const char* dir = host_env("WD_PAD_DIRECTION");
    const char* pm = host_env("WD_PADMAP");
    if (g_pad_mode != PAD_OFF && dir) {
        if (wd_dir_parse(dir)) g_dir = wd_dir_parse(dir);
        else fprintf(stderr, "[joy] WD_PAD_DIRECTION=%s ignored: want stick, dpad or both\n", dir);
    }
    if (g_pad_mode != PAD_OFF && pm) wd_padmap_parse(&g_padmap, g_pad_mode == PAD_KEYS, pm, padmap_bad, NULL);
    for (int i = 0; i < WD_PB_N; i++)
        if (g_padmap.joy[i] > g_joy_nbuttons) g_joy_nbuttons = g_padmap.joy[i];
    if (g_pad_mode != PAD_OFF && (dir || pm)) {   /* the effective tables */
        char t[256];
        int n = 0;
        for (int i = 0; i < WD_PB_N; i++) {
            uint16_t k = g_padmap.key[i];
            if (g_pad_mode == PAD_KEYS) {
                if (k) n += snprintf(t + n, sizeof t - (size_t)n, " %s=vk0x%02X", wd_pb_names[i], k & 0xFF);
                else n += snprintf(t + n, sizeof t - (size_t)n, " %s=none", wd_pb_names[i]);
            } else if (g_padmap.joy[i]) n += snprintf(t + n, sizeof t - (size_t)n, " %s=button%u", wd_pb_names[i], g_padmap.joy[i]);
        }
        fprintf(stderr, "[joy] direction %s%s, buttons:%s\n", g_dir & WD_DIR_STICK ? "stick" : "",
                g_dir & WD_DIR_DPAD ? (g_dir & WD_DIR_STICK ? "+dpad" : "dpad") : "", t);
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

/* The mappable buttons (WD_PB_*, input_map.h) and what reads them. The
 * triggers are no SDL buttons: a trigger counts as one past half deflection. */
static const SDL_GamepadButton g_pb_sdl[WD_PB_N] = {
    SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,
    SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_INVALID,
};
#define STICK_KEY 16384   /* half deflection */
static int pb_down(SDL_Gamepad* p, int b) {
    if (b == WD_PB_LT) return trigger(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > STICK_KEY;
    if (b == WD_PB_RT) return trigger(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > STICK_KEY;
    return SDL_GetGamepadButton(p, g_pb_sdl[b]);
}

/* WD_PAD=keys: the d-pad's arrows (the stick's are in joy_key_down) */
static const struct { uint8_t vk; SDL_GamepadButton b; } g_dpad_keys[] = {
    {W32_VK_UP, SDL_GAMEPAD_BUTTON_DPAD_UP}, {W32_VK_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {W32_VK_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {W32_VK_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
};

int joy_key_down(int vk) {
    if (g_pad_mode != PAD_KEYS) return 0;
    for (int i = 0; i < JOY_IDS; i++) {
        SDL_Gamepad* p = g_pad[i];
        if (!p) continue;
        for (int b = 0; b < WD_PB_N; b++)
            if (wd_padmap_key_is(&g_padmap, b, vk) && pb_down(p, b)) return 1;
        if (g_dir & WD_DIR_DPAD)
            for (size_t k = 0; k < sizeof g_dpad_keys / sizeof g_dpad_keys[0]; k++)
                if (g_dpad_keys[k].vk == vk && SDL_GetGamepadButton(p, g_dpad_keys[k].b)) return 1;
        if (g_dir & WD_DIR_STICK) {
            int x, y;
            stick(p, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, &x, &y);
            if ((vk == W32_VK_LEFT && x < -STICK_KEY) || (vk == W32_VK_RIGHT && x > STICK_KEY) ||
                (vk == W32_VK_UP && y < -STICK_KEY) || (vk == W32_VK_DOWN && y > STICK_KEY))
                return 1;
        }
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
        if (g_pad_mode == PAD_KEYS) {   /* a tap shorter than a frame still counts */
            for (int b = 0; b < WD_PB_N; b++)
                if (g_pb_sdl[b] == e->gbutton.button && g_padmap.key[b]) {
                    host_key_latch(g_padmap.key[b] & 0xFF);
                    if (g_padmap.key[b] >> 8) host_key_latch(g_padmap.key[b] >> 8);
                }
            if (g_dir & WD_DIR_DPAD)
                for (size_t k = 0; k < sizeof g_dpad_keys / sizeof g_dpad_keys[0]; k++)
                    if (g_dpad_keys[k].b == e->gbutton.button) host_key_latch(g_dpad_keys[k].vk);
        }
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
    WD_HOST_WRITE32(c + W32_JC_NUMBUTTONS) = g_joy_nbuttons;
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
    int lx = 0, ly = 0, rx, ry;
    int up = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_UP), dn = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    int lf = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_LEFT), rt = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    if (g_dir & WD_DIR_STICK) stick(p, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, &lx, &ly);
    if (g_dir & WD_DIR_DPAD) {   /* full deflection; per axis the d-pad wins over the stick */
        if (rt != lf) lx = rt ? 32767 : -32767;
        if (dn != up) ly = dn ? 32767 : -32767;
    }
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
    if (fl & W32_JOY_RETURNBUTTONS) {   /* button b is the game's button g_padmap.joy[b] */
        uint32_t bits = 0, n = 0;
        for (int b = 0; b < WD_PB_LT; b++)
            if (g_padmap.joy[b] && pb_down(p, b)) bits |= 1u << (g_padmap.joy[b] - 1);
        for (uint32_t i = 0; i < 32; i++) n += bits >> i & 1;
        WD_HOST_WRITE32(ji + W32_JI_BUTTONS) = bits;
        WD_HOST_WRITE32(ji + W32_JI_BUTTONNUMBER) = n;
    }
    if (fl & W32_JOY_RETURNPOV)   /* a d-pad that drives the axes alone is not also the hat */
        WD_HOST_WRITE32(ji + W32_JI_POV) = g_dir == WD_DIR_DPAD ? W32_JOY_POVCENTERED
            : rt && !lf ? 9000 : lf && !rt ? 27000 : up && !dn ? 0 : dn && !up ? 18000 : W32_JOY_POVCENTERED;
    RET(W32_JOYERR_NOERROR); STDRET(2);
}

/* ---- CD audio ---- */
#define CD_DEVICE 1u
static char g_track[100][1024];   /* legacy mode: the track files */
static int g_ntracks = -1;        /* -1: not looked at yet, or the active disc changed */
static int g_cd_disc;             /* disc mode: the disc the device holds (1, 2), else 0 */
static int g_cur;                 /* track playing or paused, 0 = none */
static int g_paused;

static char* last_sep(char* s) {
    char* a = strrchr(s, '/');
    char* b = strrchr(s, '\\');
    return a > b ? a : b;
}

/* files.c switched the active disc: the music stops, and the next MCI call
 * finds the new disc in the drive. */
static void cd_disc_changed(void) {
    mixer_cd_play(NULL, 0, 0, 0, 0);
    g_cur = 0; g_paused = 0;
    g_ntracks = -1;
}

static void cd_scan(void) {
    char dir[1024];
    struct Disc* disc = files_active_disc(&g_cd_disc, cd_disc_changed);
    if (disc) {
        g_ntracks = disc_track_count(disc);
        fprintf(stderr, "[cd] %d tracks on disc %d\n", g_ntracks, g_cd_disc);
        return;
    }
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

/* Start track n. 0 if the disc has no such audio track. */
static int cd_play(int n) {
    DiscTrack track;
    struct Disc* disc = files_active_disc(NULL, NULL);
    if (disc) {
        if (!disc_track(disc, n, &track) || !track.is_audio) return 0;
        mixer_cd_play(track.path, track.offset, track.length, n, g_cd_disc);
        return 1;
    }
    if (n < 1 || n > g_ntracks || !g_track[n][0]) return 0;
    mixer_cd_play(g_track[n], 0, 0, n, 0);
    return 1;
}

/* Does the device hold an audio track at all? A bare .iso has none. */
static int cd_has_audio(void) {
    DiscTrack track;
    struct Disc* disc = files_active_disc(NULL, NULL);
    for (int n = 1; n <= g_ntracks && n < 100; n++)
        if (disc ? disc_track(disc, n, &track) && track.is_audio : g_track[n][0] != 0) return 1;
    return 0;
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
    case W32_MCI_CLOSE: mixer_cd_play(NULL, 0, 0, 0, 0); g_cur = 0; break;
    case W32_MCI_SET:
        if (flags & W32_MCI_SET_DOOR_OPEN) { mixer_cd_play(NULL, 0, 0, 0, 0); g_cur = 0; }
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
        if (!(flags & W32_MCI_FROM) && !g_cur) break;   /* nothing to resume: success, silent */
        if (!cd_play(n)) {   /* no such track; on a disc without music (.iso): success, silent */
            static int said;
            if (cd_has_audio()) err = W32_MCIERR_OUTOFRANGE;
            else if (!said++) fprintf(stderr, "[cd] no audio tracks: play track %d and later ones play nothing\n", n);
            break;
        }
        g_cur = n; g_paused = 0;
        break;
    }
    case W32_MCI_STOP: mixer_cd_play(NULL, 0, 0, 0, 0); g_cur = 0; g_paused = 0; break;
    case W32_MCI_PAUSE: if (g_cur) { mixer_cd_pause(1); g_paused = 1; } break;
    case W32_MCI_RESUME: if (g_cur) { mixer_cd_pause(0); g_paused = 0; } break;
    case W32_MCI_SEEK: break;
    default:
        fprintf(stderr, "[cd] unhandled MCI command 0x%X flags 0x%X\n", msg, flags);
        break;
    }
    if (dev != CD_DEVICE && msg != W32_MCI_OPEN && !err) err = W32_MCIERR_INVALID_DEVICE_ID;
    wd_devtools_cd(g_cur, g_cd_disc, cd_mode());
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

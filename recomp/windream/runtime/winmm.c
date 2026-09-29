/*
 * WINDREAM recompilation - WINMM bridges: timer, joystick, CD audio.
 *
 * timeGetTime and the joystick API take no pointers that differ between 32 and
 * 64 bits (JOYCAPSA and JOYINFOEX are flat), so they pass straight through.
 *
 * CD audio is emulated. The game drives an MCI "cdaudio" device (CD_OpenAudio
 * 0x4042f1 and friends: OPEN, SET time format TMSF, STATUS number of tracks /
 * mode, PLAY from track n to n+1, STOP, PAUSE, RESUME). There is no CD drive;
 * the tracks are the disc image's raw Red Book files "... (Track NN).bin"
 * (44.1 kHz 16-bit stereo), played through the DirectSound mixer.
 *
 *   WD_CD_DIR   directory holding the "(Track NN).bin" files; default: the
 *               parent of the directory holding the EXE (the disc image folder)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#define RECOMP_GENERATED_CODE
#include "imports.h"

void imp_timeGetTime(void) { RET(timeGetTime()); STDRET(0); }

void imp_joyGetNumDevs(void) { RET(joyGetNumDevs()); STDRET(0); }
void imp_joyGetDevCapsA(void) { RET(joyGetDevCapsA(ARG(0), (LPJOYCAPSA)PTR(ARG(1)), ARG(2))); STDRET(3); }
void imp_joyGetPosEx(void) { RET(joyGetPosEx(ARG(0), (LPJOYINFOEX)PTR(ARG(1)))); STDRET(2); }

/* ---- CD audio ---- */
#define CD_DEVICE 1u
static char g_track[100][MAX_PATH];
static int g_ntracks = -1;
static int g_cur;            /* track playing or paused, 0 = none */
static int g_paused;

static void cd_scan(void) {
    char dir[MAX_PATH], pat[MAX_PATH + 16];
    const char* env = getenv("WD_CD_DIR");
    if (env) strcpy(dir, env);
    else {
        GetFullPathNameA(g_wd_exe, MAX_PATH, dir, NULL);
        for (int k = 0; k < 2; k++) { char* s = strrchr(dir, '\\'); if (s) *s = 0; }
    }
    g_ntracks = 0;
    WIN32_FIND_DATAA fd;
    sprintf(pat, "%s\\*(Track *).bin", dir);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const char* t = strstr(fd.cFileName, "(Track ");
            int n = t ? atoi(t + 7) : 0;
            if (n > 0 && n < 100) {
                sprintf(g_track[n], "%s\\%s", dir, fd.cFileName);
                if (n > g_ntracks) g_ntracks = n;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    fprintf(stderr, "[cd] %d tracks in %s\n", g_ntracks, dir);
}

static uint32_t cd_mode(void) {
    if (g_cur && g_paused) return MCI_MODE_PAUSE;
    if (g_cur && mixer_cd_playing()) return MCI_MODE_PLAY;
    return MCI_MODE_STOP;
}

void imp_mciSendCommandA(void) {  /* (device, msg, flags, parms): parms use the 32-bit layout */
    uint32_t dev = ARG(0), msg = ARG(1), flags = ARG(2), p = ARG(3);
    uint32_t err = 0;
    if (g_ntracks < 0) cd_scan();
    switch (msg) {
    case MCI_OPEN: {
        char type[32] = "";
        if (flags & MCI_OPEN_TYPE) guest_str(MEM32(p + 8), type, sizeof type);
        if (_stricmp(type, "cdaudio")) { err = MCIERR_UNRECOGNIZED_KEYWORD; break; }
        MEM32(p + 4) = CD_DEVICE;
        break;
    }
    case MCI_CLOSE: mixer_cd_play(NULL, 0); g_cur = 0; break;
    case MCI_SET:
        if (flags & MCI_SET_DOOR_OPEN) { mixer_cd_play(NULL, 0); g_cur = 0; }
        break;
    case MCI_STATUS:
        if (!(flags & MCI_STATUS_ITEM)) { err = MCIERR_MISSING_PARAMETER; break; }
        switch (MEM32(p + 8)) {
        case MCI_STATUS_NUMBER_OF_TRACKS: MEM32(p + 4) = (uint32_t)g_ntracks; break;
        case MCI_STATUS_MODE: MEM32(p + 4) = cd_mode(); break;
        case MCI_STATUS_CURRENT_TRACK: MEM32(p + 4) = (uint32_t)g_cur; break;
        case MCI_STATUS_MEDIA_PRESENT: case MCI_STATUS_READY: MEM32(p + 4) = 1; break;
        default: MEM32(p + 4) = 0; break;
        }
        break;
    case MCI_PLAY: {
        int n = flags & MCI_FROM ? (int)(MEM32(p + 4) & 0xFF) : g_cur;
        if (n < 1 || n > g_ntracks || !g_track[n][0]) { err = MCIERR_OUTOFRANGE; break; }
        mixer_cd_play(g_track[n], n);
        g_cur = n; g_paused = 0;
        break;
    }
    case MCI_STOP: mixer_cd_play(NULL, 0); g_cur = 0; g_paused = 0; break;
    case MCI_PAUSE: if (g_cur) { mixer_cd_pause(1); g_paused = 1; } break;
    case MCI_RESUME: if (g_cur) { mixer_cd_pause(0); g_paused = 0; } break;
    case MCI_SEEK: break;
    default:
        fprintf(stderr, "[cd] unhandled MCI command 0x%X flags 0x%X\n", msg, flags);
        break;
    }
    if (dev != CD_DEVICE && msg != MCI_OPEN && !err) err = MCIERR_INVALID_DEVICE_ID;
    RET(err); STDRET(4);
}
void imp_mciGetErrorStringA(void) {  /* (err, buf, len) */
    RET(mciGetErrorStringA(ARG(0), (LPSTR)PTR(ARG(1)), ARG(2))); STDRET(3);
}

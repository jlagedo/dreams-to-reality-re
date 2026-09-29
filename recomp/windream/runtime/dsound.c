/*
 * WINDREAM recompilation - DirectSound, emulated, plus the audio mixer.
 *
 * DSOUND_Init (0x4463e9) creates IDirectSound, a 22,050 Hz 16-bit stereo
 * primary buffer and secondary buffers (five 11 kHz 16-bit mono voices, a
 * 22 kHz stereo and an 11 kHz 8-bit mono channel); the game then Locks and
 * fills them, Plays / Stops them, and polls GetStatus / GetCurrentPosition.
 *
 * The objects are fake COM objects in guest memory ({vtbl, index}); methods are
 * host functions reached through synthetic VAs (wd_com_vtable). Buffer memory
 * is a guest allocation, so Lock hands the game real pointers. A mixer thread
 * resamples every playing buffer, plus the CD audio stream (winmm.c), into a
 * 44.1 kHz stereo waveOut stream. With no audio device the mixer still runs on
 * a timer, so play cursors advance and the game's polling behaves.
 *
 *   WD_NOSOUND=1   DirectSoundCreate fails (DSERR_NODRIVER): the game runs mute
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <process.h>
#define RECOMP_GENERATED_CODE
#include "imports.h"

#define DS_OK                0u
#define DSERR_INVALIDCALL    0x88780032u
#define DSERR_NODRIVER       0x88780078u
#define DSERR_ALREADYINIT    0x88780082u
#define E_NOINTERFACE_       0x80004002u
#define DSBCAPS_PRIMARY      0x1u
#define DSBPLAY_LOOPING      0x1u
#define DSBLOCK_FROMWRITE    0x1u
#define DSBLOCK_ENTIRE       0x2u

#define OUT_RATE   44100
#define OUT_FRAMES 1024
#define OUT_NBUF   4
#define MAX_BUF    32

typedef struct {
    int used, primary, refs;
    uint32_t obj, data, size, flags;
    int rate, ch, bits, align;
    int playing, looping;
    double pos;                 /* frames */
    int32_t vol, pan;           /* hundredths of a dB */
    uint32_t freq;              /* 0 = the format's rate */
} Buf;

static Buf g_buf[MAX_BUF];
static CRITICAL_SECTION g_cs;
static uint32_t g_ds_vtbl, g_dsb_vtbl;
static HANDLE g_mixer;
static FILE* g_cd;
static int g_cd_paused;

static Buf* buf_of(uint32_t obj) {
    uint32_t i = MEM32(obj + 4);
    return i < MAX_BUF && g_buf[i].used && g_buf[i].obj == obj ? &g_buf[i] : NULL;
}
static void set_format(Buf* b, uint32_t wfx) {
    if (!wfx) return;
    b->ch = MEM16(wfx + 2) ? MEM16(wfx + 2) : 1;
    b->rate = (int)MEM32(wfx + 4);
    b->bits = MEM16(wfx + 14) ? MEM16(wfx + 14) : 16;
    b->align = b->ch * b->bits / 8;
}
static double db_gain(int32_t hundredths) { return hundredths <= -10000 ? 0.0 : pow(10.0, hundredths / 2000.0); }

/* ---- mixer ---- */
static void mix(int16_t* out, int frames) {
    static int32_t acc[OUT_FRAMES * 2];
    memset(acc, 0, sizeof(int32_t) * (size_t)frames * 2);
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < MAX_BUF; i++) {
        Buf* b = &g_buf[i];
        if (!b->used || b->primary || !b->playing || !b->size || !b->align) continue;
        uint32_t nframes = b->size / (uint32_t)b->align;
        double step = (double)(b->freq ? b->freq : (uint32_t)b->rate) / OUT_RATE;
        double g = db_gain(b->vol);
        double gl = g * (b->pan > 0 ? db_gain(-b->pan) : 1.0), gr = g * (b->pan < 0 ? db_gain(b->pan) : 1.0);
        const uint8_t* src = (const uint8_t*)PTR(b->data);
        for (int f = 0; f < frames; f++) {
            uint32_t k = (uint32_t)b->pos;
            if (k >= nframes) {
                if (b->looping) { b->pos -= nframes; k = (uint32_t)b->pos; if (k >= nframes) b->pos = k = 0; }
                else { b->playing = 0; b->pos = 0; break; }
            }
            const uint8_t* s = src + k * (uint32_t)b->align;
            int l, r;
            if (b->bits == 8) { l = (s[0] - 128) << 8; r = b->ch > 1 ? (s[1] - 128) << 8 : l; }
            else { l = ((const int16_t*)s)[0]; r = b->ch > 1 ? ((const int16_t*)s)[1] : l; }
            acc[f * 2] += (int32_t)(l * gl);
            acc[f * 2 + 1] += (int32_t)(r * gr);
            b->pos += step;
        }
    }
    if (g_cd && !g_cd_paused) {
        static int16_t cd[OUT_FRAMES * 2];
        size_t got = fread(cd, 4, (size_t)frames, g_cd);
        for (size_t f = 0; f < got; f++) { acc[f * 2] += cd[f * 2]; acc[f * 2 + 1] += cd[f * 2 + 1]; }
        if (got < (size_t)frames) { fclose(g_cd); g_cd = NULL; }
    }
    LeaveCriticalSection(&g_cs);
    for (int i = 0; i < frames * 2; i++) {
        int32_t v = acc[i];
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

static unsigned __stdcall mixer_thread(void* unused) {
    (void)unused;
    static int16_t pcm[OUT_NBUF][OUT_FRAMES * 2];
    WAVEHDR hdr[OUT_NBUF];
    HWAVEOUT wo = NULL;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    WAVEFORMATEX wf = {WAVE_FORMAT_PCM, 2, OUT_RATE, OUT_RATE * 4, 4, 16, 0};
    if (waveOutOpen(&wo, WAVE_MAPPER, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        fprintf(stderr, "[dsound] no audio device: mixing silently on a timer\n");
        for (;;) { Sleep(OUT_FRAMES * 1000 / OUT_RATE); mix(pcm[0], OUT_FRAMES); }
    }
    fprintf(stderr, "[dsound] mixer: waveOut %d Hz stereo, %d x %d frames\n", OUT_RATE, OUT_NBUF, OUT_FRAMES);
    for (int i = 0; i < OUT_NBUF; i++) {
        memset(&hdr[i], 0, sizeof hdr[i]);
        hdr[i].lpData = (LPSTR)pcm[i];
        hdr[i].dwBufferLength = OUT_FRAMES * 4;
        waveOutPrepareHeader(wo, &hdr[i], sizeof hdr[i]);
        mix(pcm[i], OUT_FRAMES);
        waveOutWrite(wo, &hdr[i], sizeof hdr[i]);
    }
    for (;;) {
        WaitForSingleObject(ev, 100);
        for (int i = 0; i < OUT_NBUF; i++)
            if (hdr[i].dwFlags & WHDR_DONE) {
                mix(pcm[i], OUT_FRAMES);
                waveOutWrite(wo, &hdr[i], sizeof hdr[i]);
            }
    }
}

static void mixer_start(void) {
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    BOOL pending;
    InitOnceBeginInitialize(&once, 0, &pending, NULL);
    if (pending) {
        InitializeCriticalSection(&g_cs);
        g_mixer = (HANDLE)_beginthreadex(NULL, 0, mixer_thread, NULL, 0, NULL);
        InitOnceComplete(&once, 0, NULL);
    }
}

void mixer_cd_play(const char* path, int track) {
    mixer_start();
    FILE* f = path ? fopen(path, "rb") : NULL;
    if (path) fprintf(stderr, "[cd] play track %d%s\n", track, f ? "" : " (cannot open)");
    EnterCriticalSection(&g_cs);
    if (g_cd) fclose(g_cd);
    g_cd = f; g_cd_paused = 0;
    LeaveCriticalSection(&g_cs);
}
void mixer_cd_pause(int paused) { mixer_start(); g_cd_paused = paused; }
int mixer_cd_playing(void) { return g_cd != NULL; }

/* ---- IUnknown ---- */
static void m_QueryInterface(void) { if (ARG(2)) MEM32(ARG(2)) = 0; RET(E_NOINTERFACE_); STDRET(3); }
static void m_AddRef(void) {
    Buf* b = buf_of(ARG(0));
    RET(b ? ++b->refs : 1); STDRET(1);
}
static void m_Release(void) {
    Buf* b = buf_of(ARG(0));
    uint32_t left = 0;
    if (b) {
        EnterCriticalSection(&g_cs);
        left = (uint32_t)--b->refs;
        if (!left) { if (b->data) vm_free(b->data, 0, MEM_RELEASE); b->used = 0; }
        LeaveCriticalSection(&g_cs);
    }
    RET(left); STDRET(1);
}

/* ---- IDirectSound ---- */
static void ds_CreateSoundBuffer(void) {  /* (this, desc, ppBuf, unk) */
    uint32_t desc = ARG(1);
    uint32_t flags = MEM32(desc + 4), bytes = MEM32(desc + 8), wfx = MEM32(desc + 16);
    EnterCriticalSection(&g_cs);
    int i = 0;
    while (i < MAX_BUF && g_buf[i].used) i++;
    if (i == MAX_BUF) { LeaveCriticalSection(&g_cs); RET(DSERR_INVALIDCALL); STDRET(4); return; }
    Buf* b = &g_buf[i];
    memset(b, 0, sizeof *b);
    b->used = 1; b->refs = 1; b->flags = flags;
    b->primary = (flags & DSBCAPS_PRIMARY) != 0;
    b->ch = 2; b->rate = 22050; b->bits = 16; b->align = 4;
    set_format(b, wfx);
    if (!b->primary && bytes) {
        b->size = bytes;
        b->data = vm_alloc(0, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    if (!b->obj) { b->obj = shim_alloc(8, 16); }
    MEM32(b->obj) = g_dsb_vtbl;
    MEM32(b->obj + 4) = (uint32_t)i;
    LeaveCriticalSection(&g_cs);
    MEM32(ARG(2)) = b->obj;
    fprintf(stderr, "[dsound] CreateSoundBuffer #%d %s %u bytes, %d Hz %d-bit %s\n", i,
            b->primary ? "primary" : "secondary", bytes, b->rate, b->bits, b->ch > 1 ? "stereo" : "mono");
    RET(DS_OK); STDRET(4);
}
static void ds_GetCaps(void) { if (ARG(1)) memset((uint8_t*)PTR(ARG(1)) + 4, 0, MEM32(ARG(1)) > 4 ? MEM32(ARG(1)) - 4 : 0); RET(DS_OK); STDRET(2); }
static void ds_Duplicate(void) { RET(DSERR_INVALIDCALL); STDRET(3); }
static void ds_SetCooperativeLevel(void) { RET(DS_OK); STDRET(3); }
static void ds_Compact(void) { RET(DS_OK); STDRET(1); }
static void ds_GetSpeakerConfig(void) { if (ARG(1)) MEM32(ARG(1)) = 4; /* stereo */ RET(DS_OK); STDRET(2); }
static void ds_SetSpeakerConfig(void) { RET(DS_OK); STDRET(2); }
static void ds_Initialize(void) { RET(DSERR_ALREADYINIT); STDRET(2); }

/* ---- IDirectSoundBuffer ---- */
#define BUF_OR_FAIL(n) Buf* b = buf_of(ARG(0)); if (!b) { RET(DSERR_INVALIDCALL); STDRET(n); return; }

static uint32_t play_cursor(const Buf* b) { return (uint32_t)b->pos * (uint32_t)b->align; }

static void b_GetCaps(void) {  /* (this, DSBCAPS*) */
    BUF_OR_FAIL(2);
    uint32_t c = ARG(1);
    MEM32(c + 4) = b->flags; MEM32(c + 8) = b->size; MEM32(c + 12) = 0; MEM32(c + 16) = 0;
    RET(DS_OK); STDRET(2);
}
static void b_GetCurrentPosition(void) {  /* (this, *play, *write) */
    BUF_OR_FAIL(3);
    EnterCriticalSection(&g_cs);
    uint32_t p = b->size ? play_cursor(b) % b->size : 0;
    uint32_t w = b->playing && b->size ? (p + (uint32_t)(b->rate / 50 * b->align)) % b->size : p;
    LeaveCriticalSection(&g_cs);
    if (ARG(1)) MEM32(ARG(1)) = p;
    if (ARG(2)) MEM32(ARG(2)) = w;
    RET(DS_OK); STDRET(3);
}
static void b_GetFormat(void) {  /* (this, wfx, size, *written) */
    BUF_OR_FAIL(4);
    uint32_t w = ARG(1);
    if (w && ARG(2) >= 16) {
        MEM16(w) = 1; MEM16(w + 2) = (uint16_t)b->ch; MEM32(w + 4) = (uint32_t)b->rate;
        MEM32(w + 8) = (uint32_t)(b->rate * b->align); MEM16(w + 12) = (uint16_t)b->align; MEM16(w + 14) = (uint16_t)b->bits;
        if (ARG(2) >= 18) MEM16(w + 16) = 0;
    }
    if (ARG(3)) MEM32(ARG(3)) = 18;
    RET(DS_OK); STDRET(4);
}
static void b_GetVolume(void) { BUF_OR_FAIL(2); MEM32(ARG(1)) = (uint32_t)b->vol; RET(DS_OK); STDRET(2); }
static void b_GetPan(void) { BUF_OR_FAIL(2); MEM32(ARG(1)) = (uint32_t)b->pan; RET(DS_OK); STDRET(2); }
static void b_GetFrequency(void) { BUF_OR_FAIL(2); MEM32(ARG(1)) = b->freq ? b->freq : (uint32_t)b->rate; RET(DS_OK); STDRET(2); }
static void b_GetStatus(void) {
    BUF_OR_FAIL(2);
    MEM32(ARG(1)) = (b->playing ? 1u : 0u) | (b->playing && b->looping ? 4u : 0u);
    RET(DS_OK); STDRET(2);
}
static void b_Initialize(void) { RET(DSERR_ALREADYINIT); STDRET(3); }
static void b_Lock(void) {  /* (this, offset, bytes, *p1, *n1, *p2, *n2, flags) */
    BUF_OR_FAIL(8);
    if (!b->size) { RET(DSERR_INVALIDCALL); STDRET(8); return; }
    uint32_t off = ARG(1), n = ARG(2), fl = ARG(7);
    if (fl & DSBLOCK_FROMWRITE) off = play_cursor(b) % b->size;
    if (fl & DSBLOCK_ENTIRE) n = b->size;
    off %= b->size;
    if (n > b->size) n = b->size;
    uint32_t n1 = n < b->size - off ? n : b->size - off, n2 = n - n1;
    MEM32(ARG(3)) = b->data + off;
    MEM32(ARG(4)) = n1;
    if (ARG(5)) MEM32(ARG(5)) = n2 ? b->data : 0;
    if (ARG(6)) MEM32(ARG(6)) = n2;
    RET(DS_OK); STDRET(8);
}
static void b_Play(void) {  /* (this, reserved, priority, flags) */
    BUF_OR_FAIL(4);
    EnterCriticalSection(&g_cs);
    b->playing = 1; b->looping = (ARG(3) & DSBPLAY_LOOPING) != 0;
    LeaveCriticalSection(&g_cs);
    RET(DS_OK); STDRET(4);
}
static void b_SetCurrentPosition(void) {
    BUF_OR_FAIL(2);
    EnterCriticalSection(&g_cs);
    if (b->align) b->pos = (double)(ARG(1) / (uint32_t)b->align);
    LeaveCriticalSection(&g_cs);
    RET(DS_OK); STDRET(2);
}
static void b_SetFormat(void) {
    BUF_OR_FAIL(2);
    EnterCriticalSection(&g_cs); set_format(b, ARG(1)); LeaveCriticalSection(&g_cs);
    RET(DS_OK); STDRET(2);
}
static void b_SetVolume(void) { BUF_OR_FAIL(2); b->vol = (int32_t)ARG(1); RET(DS_OK); STDRET(2); }
static void b_SetPan(void) { BUF_OR_FAIL(2); b->pan = (int32_t)ARG(1); RET(DS_OK); STDRET(2); }
static void b_SetFrequency(void) { BUF_OR_FAIL(2); b->freq = ARG(1); RET(DS_OK); STDRET(2); }
static void b_Stop(void) {
    BUF_OR_FAIL(1);
    EnterCriticalSection(&g_cs); b->playing = 0; LeaveCriticalSection(&g_cs);
    RET(DS_OK); STDRET(1);
}
static void b_Unlock(void) { RET(DS_OK); STDRET(5); }
static void b_Restore(void) { RET(DS_OK); STDRET(1); }

void imp_DirectSoundCreate(void) {  /* (guid, ppDS, outer) */
    if (getenv("WD_NOSOUND")) {
        fprintf(stderr, "[dsound] WD_NOSOUND: DirectSoundCreate -> DSERR_NODRIVER\n");
        if (ARG(1)) MEM32(ARG(1)) = 0;
        RET(DSERR_NODRIVER); STDRET(3); return;
    }
    mixer_start();
    if (!g_ds_vtbl) {
        static const recomp_func_t ds[] = {
            m_QueryInterface, m_AddRef, m_Release, ds_CreateSoundBuffer, ds_GetCaps, ds_Duplicate,
            ds_SetCooperativeLevel, ds_Compact, ds_GetSpeakerConfig, ds_SetSpeakerConfig, ds_Initialize,
        };
        static const recomp_func_t dsb[] = {
            m_QueryInterface, m_AddRef, m_Release, b_GetCaps, b_GetCurrentPosition, b_GetFormat,
            b_GetVolume, b_GetPan, b_GetFrequency, b_GetStatus, b_Initialize, b_Lock, b_Play,
            b_SetCurrentPosition, b_SetFormat, b_SetVolume, b_SetPan, b_SetFrequency, b_Stop,
            b_Unlock, b_Restore,
        };
        g_ds_vtbl = wd_com_vtable(ds, (int)(sizeof ds / sizeof ds[0]));
        g_dsb_vtbl = wd_com_vtable(dsb, (int)(sizeof dsb / sizeof dsb[0]));
    }
    uint32_t obj = shim_alloc(8, 16);
    MEM32(obj) = g_ds_vtbl;
    MEM32(obj + 4) = 0xFFFFFFFFu;   /* not a buffer index */
    MEM32(ARG(1)) = obj;
    fprintf(stderr, "[dsound] DirectSoundCreate -> 0x%08X\n", obj);
    RET(DS_OK); STDRET(3);
}

/* DirectDraw is only used by the WINDREAM default mode; this build lifts
 * GDIDREAM.EXE (GDI window). Fail cleanly so a DirectDraw run reports it. */
void imp_DirectDrawCreate(void) {
    fprintf(stderr, "[ddraw] DirectDrawCreate: not emulated (use the GDI build)\n");
    if (ARG(1)) MEM32(ARG(1)) = 0;
    RET(0x80004005u); STDRET(3);
}

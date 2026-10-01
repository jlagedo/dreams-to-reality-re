/*
 * WINDREAM recompilation - GDI32 bridges on SDL3: the GDI presentation path.
 *
 * GDI_CreateDIB (0x445c84) makes a 16-bit (565, BI_BITFIELDS) top-down DIB
 * section, selects it into a memory DC and renders straight into its bits;
 * GDI_Present (0x4458bb) StretchBlts that DC, source rectangle
 * g_videoWidth x g_videoHeight, onto the window's DC.
 *
 * Every GDI object is fake: the DIB's pixels are a guest allocation, and the
 * bitmap, the memory DCs, the window DC and the stock objects are tagged
 * handles. A StretchBlt from a DIB onto the window DC uploads the source
 * rectangle to an SDL texture and presents it scaled to the window with its
 * aspect ratio kept (letterboxed); the destination rectangle is not used.
 *
 *   WD_FILTER=pixelart|nearest|linear   scaling filter (default pixelart:
 *                      nearest sampling without uneven pixel sizes)
 *   WD_FPS=25          cap presents per second, 0 = uncapped
 *   WD_SNAP="60,300"   write snap_<present>_<ms>.bmp after those presents
 *   WD_SNAP_MS=4000    ... and every 4 s
 *   WD_CRASH_AT=N      fault at present N (tests the crash report)
 */
#define RECOMP_GENERATED_CODE
#include "host.h"
#include "render_live.h"

#define FAKE_TAG   0x7E000000u
#define FAKE_MASK  0xFF000000u
#define FAKE_DC    0x00100000u
#define FAKE_BMP   0x00200000u
#define FAKE_STOCK 0x00400000u
#define FAKE_WDC   0x7E800000u   /* the window's DC (GetDC) */
#define FAKE_OLD   0x7E3FFFFFu   /* the "default bitmap" a fresh memory DC holds */
#define MAX_OBJ    16

typedef struct {
    int used, w, h, topdown, bpp;
    uint32_t bits, size, pitch;
    SDL_PixelFormat fmt;
    wd_surface_id surface;
} Dib;
typedef struct { int used; uint32_t sel; } MemDC;
static Dib g_dib[MAX_OBJ];
static MemDC g_dc[MAX_OBJ];
static uint32_t g_presents;
static uint64_t g_dib_generation;

static int is_fake(uint32_t h) { return (h & FAKE_MASK) == FAKE_TAG; }
static Dib* dib_of(uint32_t h) {
    uint32_t i = h & 0xFFFF;
    return is_fake(h) && (h & FAKE_BMP) && i < MAX_OBJ && g_dib[i].used ? &g_dib[i] : NULL;
}
static MemDC* dc_of(uint32_t h) {
    uint32_t i = h & 0xFFFF;
    return is_fake(h) && (h & FAKE_DC) && i < MAX_OBJ && g_dc[i].used ? &g_dc[i] : NULL;
}

void imp_GetDC(void) { RET(FAKE_WDC); STDRET(1); }
void imp_ReleaseDC(void) { RET(1); STDRET(2); }
void imp_CreateCompatibleDC(void) {  /* (hdc) */
    for (uint32_t i = 0; i < MAX_OBJ; i++)
        if (!g_dc[i].used) { g_dc[i].used = 1; g_dc[i].sel = FAKE_OLD; RET(FAKE_TAG | FAKE_DC | i); STDRET(1); return; }
    RET(0); STDRET(1);
}
void imp_DeleteDC(void) {
    MemDC* d = dc_of(ARG(0));
    if (d) d->used = 0;
    RET(1); STDRET(1);
}

/* 16-bit BI_RGB is 555; BI_BITFIELDS names its masks. */
static SDL_PixelFormat dib_format(int bpp, uint32_t comp, uint32_t gmask) {
    switch (bpp) {
    case 16: return comp == W32_BI_BITFIELDS && gmask == 0x07E0 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_XRGB1555;
    case 24: return SDL_PIXELFORMAT_BGR24;
    case 32: return SDL_PIXELFORMAT_XRGB8888;
    default: return SDL_PIXELFORMAT_UNKNOWN;
    }
}

void imp_CreateDIBSection(void) {  /* (hdc, pbmi, usage, ppvBits, hSection, offset) */
    uint32_t bi = ARG(1);
    for (uint32_t i = 0; i < MAX_OBJ; i++) {
        if (g_dib[i].used) continue;
        Dib* d = &g_dib[i];
        memset(d, 0, sizeof *d);
        int32_t hh = (int32_t)WD_HOST_READ32(bi + W32_BIH_HEIGHT);
        uint32_t comp = WD_HOST_READ32(bi + W32_BIH_COMPRESSION);
        d->w = (int32_t)WD_HOST_READ32(bi + W32_BIH_WIDTH);
        d->h = hh < 0 ? -hh : hh;
        d->topdown = hh < 0;
        d->bpp = WD_HOST_READ16(bi + W32_BIH_BITCOUNT);
        d->fmt = dib_format(d->bpp, comp, comp == W32_BI_BITFIELDS ? WD_HOST_READ32(bi + WD_HOST_READ32(bi) + 4) : 0);
        d->pitch = (uint32_t)(((d->w * d->bpp + 31) / 32) * 4);
        d->size = d->pitch * (uint32_t)d->h;
        d->bits = vm_alloc(0, d->size, W32_MEM_COMMIT | W32_MEM_RESERVE, W32_PAGE_READWRITE);
        if (!d->bits) break;
        d->used = 1;
        if (d->bpp == 16) {
            wd_surface_desc surface = {d->bits, d->size, (uint32_t)d->w, (uint32_t)d->h,
                d->pitch, d->fmt == SDL_PIXELFORMAT_RGB565 ? WD_SURFACE_565 : WD_SURFACE_555,
                ++g_dib_generation};
            d->surface = wd_surface_register(&surface);
            if (!d->surface) {
                fprintf(stderr, "[gdi] cannot register DIB surface\n");
                vm_free(d->bits, 0, W32_MEM_RELEASE);
                d->used = 0;
                break;
            }
            wd_render_bind_surface(d->bits,d->size,d->w,d->h,(int)d->pitch,
                d->fmt == SDL_PIXELFORMAT_RGB565 ? 0 : 1,1);
        }
        if (ARG(3)) WD_HOST_WRITE32(ARG(3)) = d->bits;
        fprintf(stderr, "[gdi] CreateDIBSection %dx%d %d bpp%s -> bits 0x%08X (%s)\n", d->w, hh, d->bpp,
                comp == W32_BI_BITFIELDS ? " bitfields" : "", d->bits, SDL_GetPixelFormatName(d->fmt));
        RET(FAKE_TAG | FAKE_BMP | i); STDRET(6);
        return;
    }
    if (ARG(3)) WD_HOST_WRITE32(ARG(3)) = 0;
    RET(0); STDRET(6);
}

void imp_SelectObject(void) {  /* (hdc, obj) -> previous */
    MemDC* d = dc_of(ARG(0));
    if (d) { uint32_t old = d->sel; d->sel = ARG(1); RET(old); STDRET(2); return; }
    RET(FAKE_OLD); STDRET(2);
}
void imp_DeleteObject(void) {
    Dib* d = dib_of(ARG(0));
    if (d) {
        wd_render_forget_surface(d->bits);
        if (d->surface) wd_surface_unregister(d->surface);
        vm_free(d->bits, 0, W32_MEM_RELEASE); d->used = 0;
    }
    RET(1); STDRET(1);
}
void imp_GetStockObject(void) { RET(FAKE_TAG | FAKE_STOCK | (ARG(0) & 0xFF)); STDRET(1); }
void imp_SetBkMode(void) { RET(W32_OPAQUE); STDRET(2); }

/* ---- snapshots: the DIB as a 24-bit BMP, for checking a run without watching it ---- */
static void snap(const Dib* d) {
    if(wd_render_requested()) {
        char path[64];SDL_snprintf(path,sizeof path,"snap_%05u_%06ums.png",g_presents,host_elapsed_ms());
        wd_render_capture(path);return;
    }
    WD_AUDIT_MEMORY(0x004458BBu, d->bits, d->size, 0);
    char name[64];
    SDL_snprintf(name, sizeof name, "snap_%05u_%06ums.bmp", g_presents, host_elapsed_ms());
    SDL_Surface* s = SDL_CreateSurfaceFrom(d->w, d->h, d->fmt, PTR(d->bits), (int)d->pitch);
    SDL_Surface* c = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_BGR24) : NULL;
    if (c && !d->topdown) SDL_FlipSurface(c, SDL_FLIP_VERTICAL);
    if (c && SDL_SaveBMP(c, name)) fprintf(stderr, "[gdi] wrote %s\n", name);
    else fprintf(stderr, "[gdi] snapshot %s: %s\n", name, SDL_GetError());
    SDL_DestroySurface(c);
    SDL_DestroySurface(s);
}

static void present_hook(const Dib* d) {
    static int nsnaps = -1;
    static uint32_t snaps[16];
    if (nsnaps < 0) {
        nsnaps = 0;
        const char* s = host_env("WD_SNAP");
        while (s && *s && nsnaps < 16) { snaps[nsnaps++] = (uint32_t)strtoul(s, (char**)&s, 10); if (*s == ',') s++; else break; }
    }
    static uint32_t every = 0xFFFFFFFFu, next;
    if (every == 0xFFFFFFFFu) { const char* s = host_env("WD_SNAP_MS"); every = s ? (uint32_t)atoi(s) : 0; next = every; }
    static uint32_t fps = 0xFFFFFFFFu;
    static uint64_t last;
    if (fps == 0xFFFFFFFFu) { const char* s = host_env("WD_FPS"); fps = s ? (uint32_t)atoi(s) : 25; }
    if (fps) {   /* WD_FPS: presents per second, default 25, 0 = uncapped (1998 machines ran at 15-30) */
        uint64_t slot = SDL_NS_PER_SECOND / fps, now = SDL_GetTicksNS();
        if (now - last < slot) SDL_DelayPrecise(slot - (now - last));
        last = SDL_GetTicksNS();
    }
    g_presents++;
    if (g_presents == 1) fprintf(stderr, "[gdi] first present at %u ms\n", host_elapsed_ms());
    for (int i = 0; i < nsnaps; i++) if (snaps[i] == g_presents) snap(d);
    if (every && host_elapsed_ms() >= next) { snap(d); next = host_elapsed_ms() + every; }
    static long crash_at = -1;
    if (crash_at < 0) { const char* s = host_env("WD_CRASH_AT"); crash_at = s ? atol(s) : 0; }
    if (crash_at && g_presents == (uint32_t)crash_at) *(volatile uint32_t*)PTR(0x25) = 0;
}

/* ---- presenting through SDL ---- */
static SDL_Texture* g_tex;
static int g_log_w, g_log_h;

static SDL_ScaleMode scale_mode(void) {
    const char* f = host_env("WD_FILTER");
    if (f && !SDL_strcasecmp(f, "linear")) return SDL_SCALEMODE_LINEAR;
    if (f && !SDL_strcasecmp(f, "nearest")) return SDL_SCALEMODE_NEAREST;
    return SDL_SCALEMODE_PIXELART;
}

static void upload(const Dib* d) {
    WD_AUDIT_MEMORY(0x004458BBu, d->bits, d->size, 0);
    const uint8_t* bits = (const uint8_t*)PTR(d->bits);
    if (d->topdown) { SDL_UpdateTexture(g_tex, NULL, bits, (int)d->pitch); return; }
    void* px;
    int pitch;
    if (!SDL_LockTexture(g_tex, NULL, &px, &pitch)) return;
    size_t row = (size_t)d->w * (size_t)(d->bpp / 8);
    for (int y = 0; y < d->h; y++)
        memcpy((uint8_t*)px + (size_t)y * (size_t)pitch, bits + (size_t)(d->h - 1 - y) * d->pitch, row);
    SDL_UnlockTexture(g_tex);
}

static void present(const Dib* d, int sx, int sy, int sw, int sh) {
    SDL_Renderer* r = host_renderer();
    if (!r || d->fmt == SDL_PIXELFORMAT_UNKNOWN) return;
    if (!g_tex || g_tex->w != d->w || g_tex->h != d->h || g_tex->format != d->fmt) {
        if (g_tex) SDL_DestroyTexture(g_tex);
        g_tex = SDL_CreateTexture(r, d->fmt, SDL_TEXTUREACCESS_STREAMING, d->w, d->h);
        if (!g_tex) { fprintf(stderr, "[gdi] SDL texture: %s\n", SDL_GetError()); return; }
        if (!SDL_SetTextureScaleMode(g_tex, scale_mode())) SDL_SetTextureScaleMode(g_tex, SDL_SCALEMODE_NEAREST);
    }
    if (sw <= 0 || sh <= 0) { sx = sy = 0; sw = d->w; sh = d->h; }
    if (sw != g_log_w || sh != g_log_h) {
        SDL_SetRenderLogicalPresentation(r, sw, sh, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        g_log_w = sw; g_log_h = sh;
    }
    upload(d);
    SDL_FRect src = {(float)sx, (float)sy, (float)sw, (float)sh};
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_RenderTexture(r, g_tex, &src, NULL);
    SDL_RenderPresent(r);
}

void imp_StretchBlt(void) {  /* (dst, x, y, w, h, src, sx, sy, sw, sh, rop) */
    MemDC* s = dc_of(ARG(5));
    Dib* d = s ? dib_of(s->sel) : NULL;
    int r = 0;
    if (d && ARG(0) == FAKE_WDC) {
        host_pump();
        if(wd_render_requested())wd_render_begin_present(d->bits);
        else present(d, (int)ARG(6), (int)ARG(7), (int)ARG(8), (int)ARG(9));
        present_hook(d);
        if(wd_render_requested())wd_render_end_present();
        r = 1;
    } else {
        static int warned;
        if (!warned++) fprintf(stderr, "[gdi] StretchBlt 0x%08X -> 0x%08X: only DIB to window is emulated\n", ARG(5), ARG(0));
    }
    RET(r); STDRET(11);
}

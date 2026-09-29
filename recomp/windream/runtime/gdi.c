/*
 * WINDREAM recompilation - GDI32 bridges: the GDI presentation path.
 *
 * GDI_CreateDIB (0x445c84) makes a 16-bit (565, BI_BITFIELDS) top-down DIB
 * section, selects it into a memory DC, and renders straight into its bits;
 * GDI_Present (0x4458bb) StretchBlts that DC to the window's client area.
 *
 * The bits must be guest memory, so the DIB section is emulated: its pixels are
 * a guest allocation, the bitmap and memory DC are fake handles, and StretchBlt
 * from the fake DC becomes StretchDIBits from the guest pixels. Real DCs (the
 * window's, from GetDC) and stock objects pass through as real handles.
 *
 *   WD_SNAP="60,300"   write snap_<present>.bmp after those presents
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define RECOMP_GENERATED_CODE
#include "imports.h"

#define FAKE_TAG   0x7E000000u
#define FAKE_MASK  0xFF000000u
#define FAKE_DC    0x00100000u
#define FAKE_BMP   0x00200000u
#define FAKE_OLD   0x7E3FFFFFu   /* the "default bitmap" a fresh memory DC holds */
#define MAX_OBJ    16

typedef struct { int used; uint32_t bits, size; struct { BITMAPINFOHEADER h; DWORD extra[256]; } bmi; } Dib;
typedef struct { int used; uint32_t sel; } MemDC;
static Dib g_dib[MAX_OBJ];
static MemDC g_dc[MAX_OBJ];
static uint32_t g_presents;
static uint32_t g_snaps[16];
static int g_nsnaps = -1;

static int is_fake(uint32_t h) { return (h & FAKE_MASK) == FAKE_TAG; }
static Dib* dib_of(uint32_t h) {
    uint32_t i = h & 0xFFFF;
    return is_fake(h) && (h & FAKE_BMP) && i < MAX_OBJ && g_dib[i].used ? &g_dib[i] : NULL;
}
static MemDC* dc_of(uint32_t h) {
    uint32_t i = h & 0xFFFF;
    return is_fake(h) && (h & FAKE_DC) && i < MAX_OBJ && g_dc[i].used ? &g_dc[i] : NULL;
}

void imp_CreateCompatibleDC(void) {  /* (hdc) */
    for (uint32_t i = 0; i < MAX_OBJ; i++)
        if (!g_dc[i].used) { g_dc[i].used = 1; g_dc[i].sel = FAKE_OLD; RET(FAKE_TAG | FAKE_DC | i); STDRET(1); return; }
    RET(0); STDRET(1);
}
void imp_DeleteDC(void) {
    MemDC* d = dc_of(ARG(0));
    if (d) d->used = 0; else if (!is_fake(ARG(0))) DeleteDC((HDC)HHOST(ARG(0)));
    RET(1); STDRET(1);
}

void imp_CreateDIBSection(void) {  /* (hdc, pbmi, usage, ppvBits, hSection, offset) */
    uint32_t bi = ARG(1);
    for (uint32_t i = 0; i < MAX_OBJ; i++) {
        if (g_dib[i].used) continue;
        Dib* d = &g_dib[i];
        memset(d, 0, sizeof *d);
        uint32_t hsz = MEM32(bi);
        if (hsz > sizeof d->bmi) hsz = sizeof d->bmi;
        memcpy(&d->bmi.h, PTR(bi), hsz);
        int w = d->bmi.h.biWidth, h = d->bmi.h.biHeight < 0 ? -d->bmi.h.biHeight : d->bmi.h.biHeight;
        int bpp = d->bmi.h.biBitCount;
        uint32_t extra = d->bmi.h.biCompression == BI_BITFIELDS ? 3 : bpp <= 8 ? (d->bmi.h.biClrUsed ? d->bmi.h.biClrUsed : 1u << bpp) : 0;
        memcpy(d->bmi.extra, PTR(bi + MEM32(bi)), extra * 4);
        d->size = (uint32_t)(((w * bpp + 31) / 32) * 4 * h);
        d->bits = vm_alloc(0, d->size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!d->bits) break;
        d->used = 1;
        if (ARG(3)) MEM32(ARG(3)) = d->bits;
        fprintf(stderr, "[gdi] CreateDIBSection %dx%d %d bpp%s -> bits 0x%08X\n", w, d->bmi.h.biHeight, bpp,
                d->bmi.h.biCompression == BI_BITFIELDS ? " bitfields" : "", d->bits);
        RET(FAKE_TAG | FAKE_BMP | i); STDRET(6);
        return;
    }
    if (ARG(3)) MEM32(ARG(3)) = 0;
    RET(0); STDRET(6);
}

void imp_SelectObject(void) {  /* (hdc, obj) -> previous */
    MemDC* d = dc_of(ARG(0));
    if (d) { uint32_t old = d->sel; d->sel = ARG(1); RET(old); STDRET(2); return; }
    if (is_fake(ARG(0)) || is_fake(ARG(1))) { RET(0); STDRET(2); return; }
    RET(H32(SelectObject((HDC)HHOST(ARG(0)), HHOST(ARG(1))))); STDRET(2);
}
void imp_DeleteObject(void) {
    Dib* d = dib_of(ARG(0));
    if (d) { vm_free(d->bits, 0, MEM_RELEASE); d->used = 0; RET(1); STDRET(1); return; }
    RET(is_fake(ARG(0)) ? 1 : DeleteObject(HHOST(ARG(0)))); STDRET(1);
}
void imp_GetStockObject(void) { RET(H32(GetStockObject((int)ARG(0)))); STDRET(1); }
void imp_SetBkMode(void) {
    RET(is_fake(ARG(0)) ? OPAQUE : SetBkMode((HDC)HHOST(ARG(0)), (int)ARG(1))); STDRET(2);
}

/* ---- snapshots: the DIB as a 24-bit BMP, for checking a run without watching it ---- */
static void snap(const Dib* d) {
    int w = d->bmi.h.biWidth, h = d->bmi.h.biHeight < 0 ? -d->bmi.h.biHeight : d->bmi.h.biHeight;
    if (d->bmi.h.biBitCount != 16) return;
    char name[64];
    sprintf(name, "snap_%05u_%06ums.bmp", g_presents, host_elapsed_ms());
    FILE* f = fopen(name, "wb");
    if (!f) return;
    uint32_t row = (uint32_t)(w * 3 + 3) & ~3u, img = row * (uint32_t)h;
    BITMAPFILEHEADER fh = {0x4D42, (DWORD)(sizeof fh + sizeof(BITMAPINFOHEADER) + img), 0, 0, sizeof fh + sizeof(BITMAPINFOHEADER)};
    BITMAPINFOHEADER ih = {sizeof ih, w, -h, 1, 24, BI_RGB, img, 0, 0, 0, 0};
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&ih, sizeof ih, 1, f);
    uint32_t gm = d->bmi.h.biCompression == BI_BITFIELDS ? d->bmi.extra[1] : 0x03E0;
    int g6 = gm == 0x07E0;
    uint8_t* line = (uint8_t*)calloc(row, 1);
    for (int y = 0; y < h; y++) {
        int sy = d->bmi.h.biHeight < 0 ? y : h - 1 - y;
        const uint16_t* src = (const uint16_t*)PTR(d->bits + (uint32_t)(sy * ((w * 2 + 3) & ~3)));
        for (int x = 0; x < w; x++) {
            uint16_t p = src[x];
            uint8_t r = g6 ? (p >> 11) & 31 : (p >> 10) & 31, g = g6 ? (p >> 5) & 63 : (p >> 5) & 31, b = p & 31;
            line[x * 3 + 0] = (uint8_t)(b << 3 | b >> 2);
            line[x * 3 + 1] = g6 ? (uint8_t)(g << 2 | g >> 4) : (uint8_t)(g << 3 | g >> 2);
            line[x * 3 + 2] = (uint8_t)(r << 3 | r >> 2);
        }
        fwrite(line, row, 1, f);
    }
    free(line);
    fclose(f);
    fprintf(stderr, "[gdi] wrote %s\n", name);
}

static void present_hook(const Dib* d) {
    if (g_nsnaps < 0) {
        g_nsnaps = 0;
        const char* s = getenv("WD_SNAP");
        while (s && *s && g_nsnaps < 16) { g_snaps[g_nsnaps++] = (uint32_t)strtoul(s, (char**)&s, 10); if (*s == ',') s++; else break; }
    }
    static uint32_t every = 0xFFFFFFFFu, next;
    if (every == 0xFFFFFFFFu) { const char* s = getenv("WD_SNAP_MS"); every = s ? (uint32_t)atoi(s) : 0; next = every; }
    static uint32_t fps = 0xFFFFFFFFu, last;
    if (fps == 0xFFFFFFFFu) { const char* s = getenv("WD_FPS"); fps = s ? (uint32_t)atoi(s) : 25; }
    if (fps) {   /* WD_FPS: cap presents per second, default 25, 0 = uncapped (1998 machines ran at 15-30) */
        uint32_t slot = 1000 / fps, now = host_elapsed_ms();
        if (now - last < slot) Sleep(slot - (now - last));
        last = host_elapsed_ms();
    }
    g_presents++;
    if (g_presents == 1) fprintf(stderr, "[gdi] first present at %u ms\n", host_elapsed_ms());
    for (int i = 0; i < g_nsnaps; i++) if (g_snaps[i] == g_presents) snap(d);
    if (every && host_elapsed_ms() >= next) { snap(d); next = host_elapsed_ms() + every; }
    static long crash_at = -1;   /* WD_CRASH_AT=N: fault at present N (tests the crash report) */
    if (crash_at < 0) { const char* s = getenv("WD_CRASH_AT"); crash_at = s ? atol(s) : 0; }
    if (crash_at && g_presents == (uint32_t)crash_at) *(volatile uint32_t*)PTR(0x25) = 0;
}

void imp_StretchBlt(void) {  /* (dst, x, y, w, h, src, sx, sy, sw, sh, rop) */
    MemDC* s = dc_of(ARG(5));
    Dib* d = s ? dib_of(s->sel) : NULL;
    int r = 0;
    if (d && !is_fake(ARG(0))) {
        HDC dst = (HDC)HHOST(ARG(0));
        SetStretchBltMode(dst, COLORONCOLOR);
        r = StretchDIBits(dst, (int)ARG(1), (int)ARG(2), (int)ARG(3), (int)ARG(4),
                          (int)ARG(6), (int)ARG(7), (int)ARG(8), (int)ARG(9),
                          PTR(d->bits), (const BITMAPINFO*)&d->bmi, DIB_RGB_COLORS, ARG(10)) != 0;
        present_hook(d);
    } else if (!s && !is_fake(ARG(0))) {
        r = StretchBlt((HDC)HHOST(ARG(0)), (int)ARG(1), (int)ARG(2), (int)ARG(3), (int)ARG(4),
                       (HDC)HHOST(ARG(5)), (int)ARG(6), (int)ARG(7), (int)ARG(8), (int)ARG(9), ARG(10));
    }
    RET(r); STDRET(11);
}

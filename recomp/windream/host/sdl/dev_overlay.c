/*
 * WINDREAM recompilation - CPU-drawn editor and tool pixels under the direct
 * renderer (docs/specs/008-editor-restoration/spec.md, phase D). Host-only.
 *
 * The direct renderer replaces the game's pixel-producing leaves (sprites,
 * glyphs, fills, copies, C3D_Line_), so the game frame lives on the GPU and
 * the guest memory at the frame pointer 0x5e549c is not shown. Some Develop
 * draws still write that memory with the CPU:
 *   - the editor's sliders: the DOS sprite blit the host ported for 0x402406
 *     (editor_menu.c sprite_blit), inside WorksEdit_ (0x44d46d);
 *   - Cryo's profiler bar and text: Display_Info_Timer_ (0x4997f8) stores
 *     pixels, info_timer_text_ prints through C3D_Print_ (0x4655b0);
 *   - the collision views' centre dots: C3D_Pixel_ (0x465ea4), called by
 *     Display_Collision_Sphere_ (0x45ea00) between its lines.
 * A scope around each of these host call sites (dev_overlay_begin/end) fills
 * the guest frame with a marker colour first, lets the draw run unchanged,
 * then hands every pixel that no longer holds the marker to the renderer as
 * a raw draw at that point of the frame (wd_render_cpu_pixels), over what
 * the GPU drew before it. A CPU pixel written with exactly the marker colour
 * (RGB565 0x0821, a near-black none of these draws uses) is lost.
 *
 * When the scope serves a TGA capture (SaveImage_ 0x4479c7 reads the guest
 * frame: WorksEdit_'s first block with the editor on, dev_tools.c with it
 * off), the GPU frame is read back into guest memory instead
 * (wd_render_materialize) and the comparison is against that copy.
 *
 * Under the software renderer the frame is the guest memory and every call
 * here does nothing.
 */
#include <stdlib.h>
#include <string.h>
#define RECOMP_GENERATED_CODE
#include "host.h"
#include "render_live.h"

#define FRAME_BUFFER 0x005E549Cu
#define FRAME_WIDTH  0x0049D9FCu
#define FRAME_HEIGHT 0x0049DA00u
#define MARKER       0x0821u

static int g_depth;
static uint32_t g_fb;
static int g_w, g_h;
static uint16_t* g_base;    /* the materialized frame; NULL: the marker */
static size_t g_base_cap;
static uint32_t* g_packed;
static size_t g_packed_cap;

static int frame(uint32_t* fb, int* w, int* h) {
    *fb = WD_HOST_READ32(FRAME_BUFFER);
    *w = (int)WD_HOST_READ32(FRAME_WIDTH);
    *h = (int)WD_HOST_READ32(FRAME_HEIGHT);
    return *fb && *w > 0 && *h > 0 && *w <= 4096 && *h <= 4096 && wd_render_requested() &&
           wd_render_surface_owned(*fb);
}

/* The GPU frame into guest memory for a CPU reader outside a scope (dev_tools.c's
 * capture with the editor off). */
int dev_overlay_materialize(void) {
    uint32_t fb;
    int w, h;
    if (!frame(&fb, &w, &h)) return 0;
    return wd_render_materialize(fb);
}

void dev_overlay_begin(int materialize) {
    if (g_depth++) return;
    g_fb = 0;
    uint32_t fb;
    int w, h;
    if (!frame(&fb, &w, &h)) return;
    size_t n = (size_t)w * (size_t)h;
    uint16_t* px = (uint16_t*)wd_host_range(fb, n * 2u, 1);
    if (materialize && wd_render_materialize(fb)) {
        if (g_base_cap < n) {
            free(g_base);
            g_base = (uint16_t*)malloc(n * 2u);
            g_base_cap = g_base ? n : 0;
        }
        if (!g_base) return;
        memcpy(g_base, px, n * 2u);
    } else {
        if (g_base_cap) g_base_cap = 0, free(g_base), g_base = NULL;
        for (size_t i = 0; i < n; i++) px[i] = MARKER;
    }
    g_fb = fb;
    g_w = w;
    g_h = h;
}

void dev_overlay_end(void) {
    if (g_depth <= 0 || --g_depth || !g_fb) return;
    uint32_t fb = g_fb;
    g_fb = 0;
    if (WD_HOST_READ32(FRAME_BUFFER) != fb || (int)WD_HOST_READ32(FRAME_WIDTH) != g_w ||
        (int)WD_HOST_READ32(FRAME_HEIGHT) != g_h)
        return;   /* the frame moved under the draw (a mode change): nothing to hand over */
    const uint16_t* px = (const uint16_t*)wd_host_range(fb, (size_t)g_w * (size_t)g_h * 2u, 0);
    int x0 = g_w, y0 = g_h, x1 = -1, y1 = -1;
    for (int y = 0; y < g_h; y++) {
        const uint16_t* row = px + (size_t)y * (size_t)g_w;
        const uint16_t* base = g_base ? g_base + (size_t)y * (size_t)g_w : NULL;
        for (int x = 0; x < g_w; x++)
            if (row[x] != (base ? base[x] : MARKER)) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                y1 = y;
            }
    }
    if (x1 < 0) return;
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    size_t n = (size_t)w * (size_t)h;
    if (g_packed_cap < n) {
        free(g_packed);
        g_packed = (uint32_t*)malloc(n * 4u);
        g_packed_cap = g_packed ? n : 0;
    }
    if (!g_packed) return;
    for (int y = 0; y < h; y++) {
        const uint16_t* row = px + (size_t)(y0 + y) * (size_t)g_w + x0;
        const uint16_t* base = g_base ? g_base + (size_t)(y0 + y) * (size_t)g_w + x0 : NULL;
        uint32_t* out = g_packed + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++)
            out[x] = row[x] != (base ? base[x] : MARKER) ? (uint32_t)row[x] | (64u << 16) : 0u;
    }
    wd_render_cpu_pixels(fb, x0, y0, w, h, g_packed);
}

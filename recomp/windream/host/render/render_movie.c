/* HNM5 palette conversion stays lifted. Only its output goes to a CPU codec
 * buffer before GPU upload; this never reads or rasterizes the scene. */
#define RECOMP_GENERATED_CODE
#include "imports.h"
#include "guest_win32.h"
#include "render_fatal.h"
#include "render_live.h"
#include "render_movie.h"

static uint32_t movie_buffer;
enum { MOVIE_BUFFER_BYTES = 800 * 600 * 2 };

void wd_render_movie_shutdown(void) {
    if (movie_buffer) {
        if (!vm_free(movie_buffer, 0, W32_MEM_RELEASE))
            wd_render_fatal("cannot release HNM5 codec buffer");
        movie_buffer = 0;
    }
}

void wd_render_hnm5(void) {
    recomp_func_t original = recomp_lookup_reference(0x42665a);
    if (!original) wd_render_fatal("missing reference function 0042665a");
    uint32_t destination = WD_HOST_READ32(0x5e549c);
    uint32_t width = WD_HOST_READ32(0x49d9fc), height = WD_HOST_READ32(0x49da00);
    int x = 0, y = 0, draw_width = (int)width, draw_height = (int)height;
    if (width == 800 && height == 600) {
        // Mode 0 leaves all four surrounding borders untouched.
        x = 80; y = 60; draw_width = 640; draw_height = 480;
    } else if (!((width == 640 && (height == 400 || height == 480)) ||
                 (width == 320 && (height == 400 || height == 200)))) {
        // The original passes mode -1, whose helper returns without drawing.
        original();
        return;
    }
    if (!wd_render_surface_owned(destination)) {
        original();
        return;
    }
    if (!movie_buffer) {
        movie_buffer = vm_alloc(0, MOVIE_BUFFER_BYTES, W32_MEM_COMMIT | W32_MEM_RESERVE,
                               W32_PAGE_READWRITE);
        if (!movie_buffer)
            wd_render_fatal("cannot allocate HNM5 codec buffer");
    }
    // No guest callback lies in this closure: wrapper -> __CHK + 454de3.
    // Preserve the actual wrapper's ABI, source versions and loop metadata.
    WD_HOST_WRITE32(0x5e549c) = movie_buffer;
    original(); // consumes the existing guest return address exactly once
    WD_HOST_WRITE32(0x5e549c) = destination;
    wd_render_movie_upload(movie_buffer + (uint32_t)y * width * 2 + (uint32_t)x * 2,
                           destination, x, y, draw_width, draw_height, (int)width * 2);
}

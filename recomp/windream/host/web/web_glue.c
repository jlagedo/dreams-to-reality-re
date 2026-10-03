/*
 * WINDREAM recompilation - the browser build's glue to the page (Emscripten only).
 *
 * The page (recomp/web) owns the DOM and the canvas; the host runs the guest on
 * a pthread (PROXY_TO_PTHREAD), so everything that touches the page goes to
 * the main thread through MAIN_THREAD_EM_ASM. The contract is
 * recomp/web/CONTRACT.md: Module.onDreamsStatus(kind, text), kind "boot",
 * "running", "fatal" or "exit"; the page works without it.
 */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include "imports.h"

void wd_web_status(const char* kind, const char* text) {
    fprintf(stderr, "[web] status %s: %s\n", kind, text ? text : "");
    fflush(stderr);
    /* The code is one parenthesised expression so its commas stay in one macro argument. */
    MAIN_THREAD_EM_ASM((function(kind, text) {
        if (typeof Module !== 'undefined' && typeof Module.onDreamsStatus === 'function') {
            try { Module.onDreamsStatus(kind, text); } catch (e) { console.error('onDreamsStatus', e); }
        }
    })(UTF8ToString($0), UTF8ToString($1)), kind, text ? text : "");
}

/* After a swap: hand the finished frame to the page. The guest thread has one
 * canvas, the one pre.js registered under the page canvas's id. cmd is the
 * pthread runtime's "call a Module handler on the main thread" message
 * (CMD_CALL_HANDLER), which CMake reads from the SDK's libpthread.js. */
#ifndef WD_WEB_CALL_HANDLER
#error "WD_WEB_CALL_HANDLER is not defined (CMakeLists.txt reads it from the Emscripten SDK)"
#endif
EM_JS(void, wd_web_present_js, (int cmd), {
    var all = GL.offscreenCanvases, rec = null;
    for (var id in all) { rec = all[id]; break; }
    var oc = rec && (rec.offscreenCanvas || rec.canvas);
    if (!oc) return;
    var bmp = oc.transferToImageBitmap();
    postMessage({ cmd: cmd, handler: 'dreamsFrame', args: [bmp] }, [bmp]);
});
static void wd_web_present(void) { wd_web_present_js(WD_WEB_CALL_HANDLER); }

#ifdef WD_WEB_CAPTURE
/* ---- frame capture for the verification tools ----
 * recomp/windream/verify/render_web_capture.py takes the browser's own frame
 * the way render_parity.py takes the Windows build's, in the same order: the
 * scene inputs of a 3D frame, that frame as presented, then the committed guest
 * memory. The page calls Module._wd_web_capture(); the guest thread works
 * through it at its presents, so the game does not advance in between:
 *   1 asked  -> arm the scene capture of the next 3D frame (/capture/frame.wds)
 *   2 armed  -> at the present of the frame whose scene was captured: read its
 *               pixels before the swap (/capture/frame.rgba: width, height,
 *               RGBA rows top first), swap, then write memory (/capture/frame.wdmi)
 *   3 done, 4 failed (Module._wd_web_capture_state()). Off with WD_WEB_CAPTURE=OFF. */
#include <sys/stat.h>
#include <GLES3/gl3.h>
#include "render_live.h"

static volatile int g_capture;

EMSCRIPTEN_KEEPALIVE int wd_web_capture(void) {
    if (g_capture == 1 || g_capture == 2) return 0;
    g_capture = 1;
    return 1;
}
EMSCRIPTEN_KEEPALIVE int wd_web_capture_state(void) { return g_capture; }

static int capture_pixels(SDL_Window* window) {
    int w = 0, h = 0;
    if (!SDL_GetWindowSizeInPixels(window, &w, &h) || w <= 0 || h <= 0) return 0;
    size_t row = (size_t)w * 4;
    uint8_t* pixels = malloc(row * (size_t)h + row);
    if (!pixels) return 0;
    /* sokol caches GL state: put back what is changed for the read. */
    GLint previous = 0, alignment = 4;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previous);
    uint8_t* spare = pixels + row * (size_t)h;
    for (int y = 0; y < h / 2; y++) {   /* GL rows are bottom first */
        memcpy(spare, pixels + row * y, row);
        memcpy(pixels + row * y, pixels + row * (h - 1 - y), row);
        memcpy(pixels + row * (h - 1 - y), spare, row);
    }
    FILE* f = fopen("/capture/frame.rgba", "wb");
    uint32_t size[2] = { (uint32_t)w, (uint32_t)h };
    int ok = f && fwrite(size, sizeof size, 1, f) == 1 && fwrite(pixels, row, (size_t)h, f) == (size_t)h;
    if (f) ok = fclose(f) == 0 && ok;
    free(pixels);
    return ok;
}

/* Before the swap: returns 1 when this present ends the captured frame. */
static int capture_before_swap(SDL_Window* window) {
    if (g_capture == 1) {
        if (!wd_render_requested()) { g_capture = 4; return 0; }
        mkdir("/capture", 0777);
        wd_render_scene_capture_next("/capture/frame.wds");
        g_capture = 2;
        return 0;
    }
    if (g_capture != 2 || wd_render_scene_capture_pending()) return 0;
    if (!capture_pixels(window)) { g_capture = 4; return 0; }
    return 1;
}
static void capture_after_swap(void) {
    uint32_t runs = 0, bytes = 0;
    int ok = vm_write_image("/capture/frame.wdmi", wd_render_scene_capture_root(), &runs, &bytes);
    fprintf(stderr, "[web] capture: frame, scene and memory (%u runs, %u bytes)%s\n", runs, bytes,
            ok ? "" : " FAILED");
    g_capture = ok ? 3 : 4;
}
#endif

/* Every SDL_GL_SwapWindow (the direct renderer's present and SDL_Renderer's
 * own) goes through here: the linker wraps the symbol (CMakeLists.txt). */
extern bool __real_SDL_GL_SwapWindow(SDL_Window* window);
bool __wrap_SDL_GL_SwapWindow(SDL_Window* window) {
#ifdef WD_WEB_CAPTURE
    int captured = capture_before_swap(window);
#endif
    bool ok = __real_SDL_GL_SwapWindow(window);
    wd_web_present();
#ifdef WD_WEB_CAPTURE
    if (captured) capture_after_swap();
#endif
    return ok;
}

/* ---- unattended boot of a pack without the intro movie ----
 * The demo pack cuts INTRO.HNM and GENERIC.HNM: with the intro absent the game
 * waits at a black screen for ESC, and the menu (without its movie) for RETURN on
 * "New game" (recomp/web/NOTES-demo.md). The host presses those keys itself,
 * driven by what the guest opens rather than by time, as game_nav.boot_into does:
 *   1  intro.hnm failed to open   -> ESC until generic.hnm is opened
 *   2  generic.hnm opened         -> RETURN until dreams.dat is opened again
 *   3  done (also when the intro opened: then the player skips it)
 * WD_WEB_AUTOSKIP=0 turns it off. Keys are reported through GetAsyncKeyState as
 * 150 ms pulses (user.c). */
static volatile int g_boot_stage;
static int g_boot_off;

static int ends_with_nocase(const char* s, const char* tail) {
    size_t n = strlen(s), t = strlen(tail);
    return n >= t && !strcasecmp(s + n - t, tail);
}
void wd_web_boot_note(const char* rel, int ok) {
    static int init;
    if (!init) { init = 1; const char* e = getenv("WD_WEB_AUTOSKIP"); g_boot_off = e && *e == '0'; }
    if (g_boot_off) return;
    int stage = g_boot_stage;
    if (stage == 0 && !ok && ends_with_nocase(rel, "hnm\\intro.hnm")) g_boot_stage = 1;
    else if (stage == 1 && ends_with_nocase(rel, "hnm\\generic.hnm")) g_boot_stage = 2;
    else if (stage == 2 && !strcasecmp(rel, "dreams.dat")) g_boot_stage = 3;
    else if (stage == 0 && ok && ends_with_nocase(rel, "hnm\\intro.hnm")) g_boot_stage = 3;
    if (g_boot_stage != stage) fprintf(stderr, "[web] boot assist: stage %d -> %d (%s)\n", stage, g_boot_stage, rel);
}
int wd_web_boot_key(int vk) {
    int stage = g_boot_stage;
    if (stage != 1 && stage != 2) return 0;
    if (vk != (stage == 1 ? 0x1B : 0x0D)) return 0;
    return (SDL_GetTicks() / 150) % 2 == 0;
}

/* Arguments of the form NAME=VALUE (createDreams({arguments: ["WD_RENDERER=software"]}))
 * become environment variables, for the host's WD_* options: the page can pass
 * them without relying on Module.ENV reaching the guest thread. */
void wd_web_args_to_env(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        const char* eq = strchr(argv[i], '=');
        if (!eq || strncmp(argv[i], "WD_", 3)) continue;
        char name[64];
        size_t n = (size_t)(eq - argv[i]);
        if (n >= sizeof name) continue;
        memcpy(name, argv[i], n);
        name[n] = 0;
        setenv(name, eq + 1, 1);
        fprintf(stderr, "[web] %s=%s\n", name, eq + 1);
    }
}
#endif

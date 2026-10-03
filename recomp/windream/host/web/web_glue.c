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

/* Every SDL_GL_SwapWindow (the direct renderer's present and SDL_Renderer's
 * own) goes through here: the linker wraps the symbol (CMakeLists.txt). */
extern bool __real_SDL_GL_SwapWindow(SDL_Window* window);
bool __wrap_SDL_GL_SwapWindow(SDL_Window* window) {
    bool ok = __real_SDL_GL_SwapWindow(window);
    wd_web_present();
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

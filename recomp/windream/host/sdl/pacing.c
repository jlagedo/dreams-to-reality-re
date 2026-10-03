/*
 * WINDREAM recompilation - frame pacing and the optional fixed step.
 *
 * GAME_TickFrame (0x416d45) sets the next frame's delta Δt (0x5e5388, in
 * 30 Hz frames) from the 200 Hz counter: Δt = 0.15 * ticks. SYS_UpdateTimer
 * drops the leftover milliseconds at every update, so frames of the same
 * length count 6 to 9 ticks (measured at the 25 fps cap: 0.9 to 1.35), and
 * the physics, whose position step and damping are per frame, is tuned for
 * one Δt only (docs/research/engine.md, "The fixed step").
 *
 *   WD_FPS=25         retail pacing (default): presents at least 1/25 s apart
 *   WD_FIXED_STEP=1   presents on a 30 Hz grid and Δt = 1.0 in every gameplay
 *                     frame, the step the engine's own demo recorder forces
 *                     (0x49d34a == 0). WD_FPS is ignored. A frame that runs
 *                     late is presented at once and the grid catches up by at
 *                     most one period; a longer stall (a level load, a pause)
 *                     starts a new grid. Game time then runs slow rather than
 *                     taking a larger step.
 *
 * The fixed step is a departure from retail: retail's Δt follows the
 * machine. The render's display interpolation (render_live.cpp) runs inside
 * this grid.
 */
#define RECOMP_GENERATED_CODE
#include <stdio.h>
#include <stdlib.h>
#include "host.h"
#include "pacing.h"

#define STEP_NS (SDL_NS_PER_SECOND / 30)

static int g_fixed = -1;
static uint64_t g_deadline;   /* when the present in progress is due */

int wd_fixed_step(void) {
    if (g_fixed < 0) {
        const char* s = host_env("WD_FIXED_STEP");
        g_fixed = s && atoi(s) != 0;
        if (g_fixed) fprintf(stderr, "[pacing] fixed step: 30 Hz presents, frame delta 1.0\n");
    }
    return g_fixed;
}

uint64_t wd_pacing_period_ns(void) { return STEP_NS; }
uint64_t wd_pacing_deadline_ns(void) { return wd_fixed_step() ? g_deadline : 0; }

void wd_pacing_wait(void) {
    if (wd_fixed_step()) {
        uint64_t now = SDL_GetTicksNS();
        if (!g_deadline) g_deadline = now;
        if (now < g_deadline) SDL_DelayPrecise(g_deadline - now);
        now = SDL_GetTicksNS();
        g_deadline += STEP_NS;
        if (now > g_deadline) g_deadline = now + STEP_NS;   /* a period or more late: a new grid */
        return;
    }
    static uint32_t fps = 0xFFFFFFFFu;
    static uint64_t last;
    if (fps == 0xFFFFFFFFu) { const char* s = host_env("WD_FPS"); fps = s ? (uint32_t)atoi(s) : 25; }
    if (fps) {   /* WD_FPS: presents per second, default 25, 0 = uncapped (1998 machines ran at 15-30) */
        uint64_t slot = SDL_NS_PER_SECOND / fps, now = SDL_GetTicksNS();
        if (now - last < slot) SDL_DelayPrecise(slot - (now - last));
        last = SDL_GetTicksNS();
    }
}

/* After GAME_TickFrame's delta block (0x4170a6-0x4172a2) on every path: its
 * last word, the demo recorder's Δt = 1.0, is the one this repeats. The
 * debug override 0x4a4758 == 1 (Δt = 2.0, spec 005) is left alone. */
void wd_frame_delta(void) {
    if (!wd_fixed_step() || WD_HOST_READ32(0x004A4758u) == 1) return;
    WD_HOST_WRITE32(0x005E5388u) = 0;
    WD_HOST_WRITE32(0x005E538Cu) = 0x3FF00000u;   /* 1.0 */
}

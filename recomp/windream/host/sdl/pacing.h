/*
 * WINDREAM recompilation - frame pacing and the optional fixed step (pacing.c).
 */
#ifndef WD_PACING_H
#define WD_PACING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WD_FIXED_STEP: presents on a 30 Hz grid, Δt = 1.0 every gameplay frame. */
int wd_fixed_step(void);
/* The grid's period, and when the present in progress is due (0 before the
 * first present or without the fixed step). */
uint64_t wd_pacing_period_ns(void);
uint64_t wd_pacing_deadline_ns(void);
/* gdi.c, each present: sleep to the WD_FPS cap or to the grid, then advance. */
void wd_pacing_wait(void);
/* lift.py CALLS, at 0x4172a2 in GAME_TickFrame after the frame delta. */
void wd_frame_delta(void);

#ifdef __cplusplus
}
#endif

#endif /* WD_PACING_H */

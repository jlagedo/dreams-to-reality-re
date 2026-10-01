/*
 * WINDREAM recompilation - development control channel: the hooks the host calls.
 *
 * This header is all the host knows of recomp/windream/devtools. A build made
 * with the CMake option WD_DEVTOOLS (on for development builds, forced off by
 * WD_RELEASE) defines WD_DEVTOOLS and links the directory; any other build,
 * and every script that compiles host files on its own, gets the empty inline
 * functions below, so a call site is one line with no #ifdef.
 *
 * Even when compiled in, nothing happens unless WD_CTL is set when the host
 * starts: every hook returns at once. See devtools.c for the channel itself
 * and recomp/README.md, "Development control channel".
 *
 * To remove the feature: delete this directory, the include of this header in
 * host/sdl/host.h, the seven wd_devtools_* calls in host/sdl (user.c three,
 * gdi.c, files.c, winmm.c and dsound.c one each) and the WD_DEVTOOLS block in
 * CMakeLists.txt; then debug/wdctl.py, run.py's --ctl, release.py's check and
 * tests/recomp/test_devtools.py, which only exist for it, and the MCP server
 * built on wdctl: debug/wd_mcp.py, .mcp.json at the repository root and
 * tests/recomp/test_wd_mcp.py.
 */
#ifndef WD_DEVTOOLS_H
#define WD_DEVTOOLS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WD_DEVTOOLS

/* user.c, end of host_init: read WD_CTL and start listening. */
void wd_devtools_init(void);
/* user.c, host_pump (main thread, where SDL events are drained and scripted
 * keys fire): run queued commands, finish waits, and hold the guest here
 * while paused. */
void wd_devtools_pump(void);
/* user.c, GetAsyncKeyState: is this virtual key held by a `key` command?
 * Beside script_down, so an injected key is a game key like a WD_KEYS one. */
int wd_devtools_key_down(int vk);
/* gdi.c, after each present: count the frame. Returns the path of a requested
 * screenshot for the host's own snapshot code to write, else NULL. */
const char* wd_devtools_frame(int width, int height);
/* files.c, CreateFileA: one open, as the "[files] open" log line has it
 * (guest: path below the guest's C:\; host: where it resolved). Any thread. */
void wd_devtools_file_open(const char* guest, const char* host, int write, int ok);
/* winmm.c, after each MCI command: the CD device's state (mode: the guest's
 * MCI_MODE_* value; disc 0 outside disc mode). */
void wd_devtools_cd(int track, int disc, uint32_t mode);
/* dsound.c, end of mix: the mixed output, interleaved 16-bit stereo, the
 * samples that go to the audio device (or nowhere when muted). Mixer thread. */
void wd_devtools_audio(const int16_t* pcm, int frames, int rate);

#else

static inline void wd_devtools_init(void) {}
static inline void wd_devtools_pump(void) {}
static inline int wd_devtools_key_down(int vk) { (void)vk; return 0; }
static inline const char* wd_devtools_frame(int width, int height) { (void)width; (void)height; return 0; }
static inline void wd_devtools_file_open(const char* guest, const char* host, int write, int ok) {
    (void)guest; (void)host; (void)write; (void)ok;
}
static inline void wd_devtools_cd(int track, int disc, uint32_t mode) { (void)track; (void)disc; (void)mode; }
static inline void wd_devtools_audio(const int16_t* pcm, int frames, int rate) { (void)pcm; (void)frames; (void)rate; }

#endif

#ifdef __cplusplus
}
#endif

#endif /* WD_DEVTOOLS_H */

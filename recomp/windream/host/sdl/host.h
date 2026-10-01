/*
 * WINDREAM recompilation - the SDL3 host shared by the USER32, GDI32, WINMM
 * and DirectSound bridges (user.c, gdi.c, winmm.c, dsound.c).
 *
 * The guest still sees Win32: one window, fake handles, polled keys, WinMM
 * joysticks, DirectSound buffers. Everything behind those calls is SDL, and
 * all window, event and gamepad calls run on the game's main thread (the host
 * main thread), which is the thread that pumps messages in the original too.
 */
#ifndef WD_HOST_H
#define WD_HOST_H

#include <SDL3/SDL.h>
#include "imports.h"
#include "guest_win32.h"
#include "../../devtools/devtools.h"   /* development control channel hooks; empty unless built with WD_DEVTOOLS */

/* A file or find object behind a guest handle. Another thread may close the
 * handle while this one uses the object, so users hold a reference
 * (handle_acquire, files_release) and the last one frees it. */
typedef struct { SDL_AtomicInt refs; } WdObject;

/* Fake USER handles. GDI's fake handles (gdi.c) use tag 0x7E000000. */
#define WD_HWND_MAIN    0x7D000100u
#define WD_HICON        0x7D000200u
#define WD_HCURSOR      0x7D000300u

/* user.c */
SDL_Window*   host_window(void);        /* NULL until CreateWindowExA */
SDL_Renderer* host_renderer(void);
void          host_pump(void);          /* drain SDL events into key state and the message queue */
void          host_key_latch(int vk);   /* GetAsyncKeyState's "pressed since last call" bit */
const char*   host_env(const char* name);   /* getenv, "" treated as unset */

/* winmm.c: joysticks are SDL gamepads */
void joy_init(void);
void joy_event(const SDL_Event* e);
int  joy_key_down(int vk);              /* WD_PAD=keys: the pad held as keys */

#endif /* WD_HOST_H */

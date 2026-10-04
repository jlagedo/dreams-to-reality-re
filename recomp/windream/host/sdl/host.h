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

/* The launch mode (docs/specs/008-editor-restoration/spec.md, phase M), read
 * once from WD_MODE: "dev" is Develop (Cryo's developer tools and their host
 * bindings live), "edited" is Play edits (the developer folder's game, tools
 * off), anything else Play (the shipped game, every Develop binding inert). */
typedef enum { WD_MODE_PLAY, WD_MODE_DEVELOP, WD_MODE_EDITED } WdMode;
WdMode        host_mode(void);
#define       host_develop() (host_mode() == WD_MODE_DEVELOP)

/* editor_menu.c: in Develop, the July Dreams Editor menu from
 * resources/editor-tree.tsv beside the executable (spec 008 phase 1). */
void editor_menu_install(void);
void editor_frame_begin(void);  /* around the inserted editor call (user.c wd_editor_frame) */
void editor_frame_end(void);

/* editor_bank.c: in Develop, the 150 project records as a bank in guest
 * memory, unpacked from DREAMS.DAT in memory, and the retail functions that
 * walk it replaced (spec 008 phase 4). For the pickers (phase 3): */
void     editor_bank_install(void);              /* host_mode_install */
void     editor_bank_frame_begin(void);          /* editor_frame_begin: bind 0x661d94 to the level's record */
void     editor_bank_frame_end(void);            /* editor_frame_end: the message */
uint32_t editor_bank_record(int slot);           /* guest VA of record slot (0..149), 0 without a bank */
uint32_t editor_bank_find(const char* name);     /* guest VA of the first record so named, or 0 */
int      editor_bank_list(void);                 /* the project list rebuilt; its count (also 0x661d5c) */
uint32_t editor_bank_list_names(void);           /* guest VA: 150 x 16-byte names, meshes (+0x60c) at +0x960 */
int      editor_bank_list_slot(int row);         /* the record slot of list row, or -1 */
int      editor_bank_write_disk(const char* why); /* phase 5: a dirty bank into the developer folder (DREAMS.DAT, EDITOR.DAT); 1 when written */

/* editor_pickers.c: in Develop, the editor's pickers (the file lists and the
 * objet, link, link-adventure, box and project pages) on host tables, never
 * on retail's one-byte tables (spec 008 phase 3). */
void     editor_pickers_install(void);           /* host_mode_install */
void     editor_pickers_frame_begin(void);       /* editor_frame_begin: a page not drawn last frame has opened */
uint32_t editor_pickers_table(int which);        /* guest VA of a host list table (editor_pickers.c L_*) */

/* dev_keys.c: the keyboard of Develop, DOS keys (spec 008 phase 2). host_pump
 * hands every key and text event to dev_keys_take in Develop and calls
 * dev_keys_flush once the pump has drained; a key the host does not consume
 * goes on to `pass`, the Play path. */
void     dev_keys_init(void (*pass)(const SDL_KeyboardEvent*));
int      dev_keys_take(const SDL_Event* e);      /* 1: buffered until dev_keys_flush (Develop only) */
void     dev_keys_flush(void);
void     dev_keys_release_all(void);             /* focus lost */
int      dev_keys_editor_letters(void);          /* Develop with the editor on: WD_KEYMAP is suspended */
void     dev_keys_start(SDL_Window* window);     /* text input on, Develop only */
void     dev_keys_print(void);                   /* the key list on the console */
void     dev_keys_frame(void);                   /* wd_editor_frame: the developer keys' work */
uint16_t dev_keys_editor_enter(void);            /* around the editor call: its character in 0x626fd8 */
void     dev_keys_editor_leave(uint16_t game);
size_t   dev_keys_count(void);                   /* the key list (launcher/dev_keys.h) */
const char* dev_keys_entry(size_t i, int column);   /* 0 the key, 1 what it does */

/* dev_tools.c: Cryo's developer tools of Develop (spec 008 phase 7), keyed by
 * dev_keys.c and served from wd_editor_frame. */
enum { DEV_TOOL_CAPTURE_EVERY = 1, DEV_TOOL_CAPTURE_ONE, DEV_TOOL_GIVE_ALL, DEV_TOOL_COLLISION, DEV_TOOL_PROFILER,
       DEV_TOOL_CONSOLE, DEV_TOOL_SAVE_PAGE, DEV_TOOL_RECORD, DEV_TOOL_REPLAY };
void dev_tools_install(void);                    /* host_mode_install: DATA\TGA in the folder */
void dev_tools_request(int tool);
void dev_tools_frame_begin(void);                /* wd_editor_frame, before the editor call */
void dev_tools_frame_end(void);                  /* after it: the overlays */
int  dev_tools_mouse_relative(void);             /* the free camera wants relative mouse mode */
void dev_tools_pump(void);                       /* the PeekMessageA bridge, Develop: menu-time work */
void dev_tools_say(const char* fmt, ...);        /* stderr and about 3 s at the frame's foot */

/* dev_overlay.c: CPU-drawn Develop pixels under the direct renderer (spec 008
 * phase D); nothing under the software renderer. */
void dev_overlay_begin(int materialize);         /* materialize: a TGA capture reads the frame */
void dev_overlay_end(void);
int  dev_overlay_materialize(void);              /* the GPU frame into guest memory */

/* dev_save_page.c: Cryo's Save page on keypad 0 (spec 008 phase 7). */
void dev_save_page_run(void (*say)(const char* fmt, ...));   /* from wd_editor_frame; returns when the menu closes */
int  dev_save_page_typing(void);                 /* the page is open: rule 6 */
void dev_save_page_type(uint8_t c);              /* typed ASCII for its title */
void dev_save_page_pump(void);                   /* the PeekMessageA bridge: one character per menu frame */

/* save_guard.c: GAME_LoadGame refuses a save it would crash on, every mode (spec 008 B1 "Saves"). */
void save_guard_install(void);                   /* host_mode_install */

/* files.c: the guest's standard-handle writes, also to Develop's console window */
extern void (*wd_std_tee)(const void* data, uint32_t n);

/* winmm.c: joysticks are SDL gamepads */
void joy_init(void);
void joy_event(const SDL_Event* e);
int  joy_key_down(int vk);              /* WD_PAD=keys: the pad held as keys */

#endif /* WD_HOST_H */

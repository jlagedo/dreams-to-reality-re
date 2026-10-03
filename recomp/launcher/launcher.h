/*
 * Launcher - disc setup and port settings, in a small window (or none).
 *
 * One entry point. It reads dreams.ini, validates the two disc images, shows
 * the launcher window and returns either "the user quit" or the list of WD_*
 * NAME=VALUE pairs the host reads from its environment (recomp/README.md lists
 * the names and value formats). Nothing else crosses the boundary: the
 * launcher never sets an environment variable, never starts the game, and
 * includes nothing from the host or the renderers. It depends on SDL3, Dear
 * ImGui and the disc library (recomp/disc) only.
 *
 * Strings are UTF-8, in and out. On Windows that means argv must be UTF-8
 * too: use SDL's entry point (SDL_main.h) rather than the ANSI main.
 *
 * Graphics: the window, an SDL_Renderer and the ImGui context are created and
 * destroyed inside launcher_run; nothing is left behind for the game's own
 * window. SDL is not shut down: launcher_run initialises the video and gamepad
 * subsystems it needs and releases exactly those (SDL_QuitSubSystem), never
 * SDL_Quit, so the caller may use SDL afterwards.
 *
 * Files: dreams.ini and userdata/ are beside the executable (SDL_GetBasePath),
 * or in SDL_GetPrefPath("", "DreamsToReality") when that folder is not
 * writable. Test only: the environment variable DREAMS_LAUNCHER_HOME replaces
 * the executable's folder (same writability rule); DREAMS_LAUNCHER_SHOT=
 * <file.bmp> makes the window save one of its first frames there and quit
 * (DREAMS_LAUNCHER_SHOT_TAB=display|keyboard|gamepad picks the tab);
 * DREAMS_LAUNCHER_VERBOSE=1 prints each disc's status line to stderr in --play.
 *
 * Test only: DREAMS_LAUNCHER_SCRIPT=<file> drives the window from a text script
 * (testscript.h/.cpp; unset, nothing of it exists). The window then ignores the
 * real keyboard, mouse and gamepad and takes only what the script pushes into
 * SDL's event queue, so it needs no OS focus. A failing step prints
 * "script:<line>: <message>" to stderr, closes the window and makes launcher_run
 * return -1 with that text in out->error; a script that runs out of steps while
 * the window is open fails with "script ended, window still open", and one whose
 * window closes before its last step with "the window closed before this step ran".
 * tests/recomp/test_launcher_ui.py is the user. One step per line; '#' starts a
 * comment line; words are separated by blanks, "double quotes" group (no escapes:
 * a path keeps its backslashes). After each step the script waits `settle` frames
 * (3), so a following `expect` sees its effect.
 *
 *   wait N                    N more frames       settle N     frames after each step
 *   key K [K...]              press and release (K = A, 5, F3, UP, RETURN, TAB, ESC, SPACE,
 *                             CTRL, SHIFT, KP3, ... or an SDL name; "SHIFT+TAB" holds the modifiers)
 *   keydown K / keyup K       a key held across other steps (modifiers count: Ctrl+click)
 *   text STRING               SDL_EVENT_TEXT_INPUT (typing into a field)
 *   click ID [fx [fy]]        mouse move, press, release on the widget's last drawn rectangle
 *                             (at fx,fy of it, default the centre): real hit-testing
 *   move ID | move X Y        pointer only
 *   tab display|keyboard|gamepad   click that tab
 *   drop PATH [X Y | @ID]     SDL's drop sequence (begin, position, file, complete); default
 *                             the window's top left (row 1); @disc2.row drops on that row
 *   dialog ROW PATH           what the file dialog reports (the callback SDL_ShowOpenFileDialog
 *                             calls), ROW 1 or 2; PATH "" is a cancelled dialog
 *   dialog-thread ROW PATH [MS]   the same from another thread after MS milliseconds
 *   dialog-error ROW          a failed dialog (NULL list)
 *   pad attach|detach         an SDL virtual gamepad (the only one the window reads)
 *   pad down|up|press B       B = a b x y back start ls rs lb rb up down left right
 *   shot FILE.bmp             save the frame just drawn
 *   expect NAME [OP] VALUE    OP = (equal, default) != ~ (contains) !~ ; mismatch fails the run
 *   print NAME                a fact to stderr, for writing scripts
 *   close | end               WINDOW_CLOSE_REQUESTED     sdlquit    SDL_EVENT_QUIT
 *
 * While a script runs, the Browse buttons open no OS dialog: they only disable themselves until
 * a `dialog` step answers, and record the folder the dialog would have started in.
 *
 * Widget ids (click, move, drop @): disc1.row disc2.row (drop only) browse1 browse2
 *   tab.display tab.keyboard tab.gamepad  play quit open_folder
 *   display: renderer renderer.software renderer.gpu  fullscreen  scale  filter
 *            filter.pixelart filter.nearest filter.linear  smooth  smooth_camera  fps  mute
 *   keyboard: key.wasd key.reset  key.<GAME>.capture key.<GAME>.reset  (GAME = UP DOWN LEFT RIGHT
 *            CTRL ALT SPACE 1 2 3 ESC F1.. ; rows scrolled out of view are refused)
 *   gamepad: pad.mode pad.mode.game|keys|off  pad.dir pad.dir.stick|dpad|both  pad.dzin pad.dzout
 *            pad.reset  pad.btn.<a b x y lb rb back start ls rs lt rt>  and, when its list is open,
 *            pad.btn.<b>.button<N> (game mode) or pad.btn.<b>.<KEY> (keys mode)
 *
 * Facts (expect, print): disc1 disc2 (status line)  disc1.status disc2.status (NotSet NotFound
 *   NotDisc WrongDisc WrongEdition Found FoundNoMusic)  disc1.path disc2.path  notice  save_error
 *   play (enabled|disabled)  blocker  conflict (yes|no)  conflict.text  conflict.rows  keymap
 *   key.<GAME> (physical key)  capture (none or the game key waiting for a key)  picking
 *   dialog.row  dialog.start  tab  focus (a widget id or none)  var.WD_RENDERER ... var.WD_PADMAP
 *   (what the port settings would emit now, "" if left out)  nav.keyboard nav.gamepad (ImGui
 *   config flags)  gamepad.seen (ImGui's SDL backend has a gamepad).
 */
#ifndef DREAMS_LAUNCHER_H
#define DREAMS_LAUNCHER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;   /* "WD_RENDERER" */
    const char* value;  /* "direct" */
} LauncherVar;

typedef struct {
    int play;              /* 1 = start the game with vars, 0 = the user quit or there is an error */
    int count;
    LauncherVar* vars;     /* WD_* pairs; valid until launcher_free. Only set when play == 1 */
    char error[512];       /* UTF-8; set when --play found the discs invalid or the window could not open */
} LauncherResult;

/*
 * Options understood in argv (anything else is ignored: the host has its own):
 *   --disc1 PATH   --disc2 PATH   disc images (.cue, .iso or a directory) for this run, not saved
 *   --data DIR     user data directory for this run, not saved
 *   --play         no window: validate, and return play = 1 with the pairs, or play = 0 with
 *                  error filled. A successful --play also saves dreams.ini (as Play does)
 * "--opt=value" works as well as "--opt value". argv[0] is skipped.
 *
 * Returns 1 (play), 0 (the user quit) or -1 (error text in out->error). out is
 * always filled and must be given to launcher_free, whatever the return value.
 *
 * Emitted names, each left out when its setting is at the host's default:
 *   WD_DISC1 WD_DISC2 WD_DATA_DIR (always) WD_RENDERER WD_FULLSCREEN WD_SCALE
 *   WD_FILTER WD_FPS WD_MUTE WD_FIXED_STEP WD_INTERPOLATE WD_SMOOTH_CAMERA WD_PAD
 *   WD_DEADZONE WD_PAD_DIRECTION WD_KEYMAP WD_PADMAP
 */
int launcher_run(int argc, char** argv, LauncherResult* out);

/* Releases what launcher_run allocated. Accepts a result that was never filled
 * only if it was zeroed first; safe to call twice. */
void launcher_free(LauncherResult* out);

#ifdef __cplusplus
}
#endif

#endif

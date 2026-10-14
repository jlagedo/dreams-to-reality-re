/*
 * WINDREAM recompilation - what each launch mode changes in the game
 * (docs/specs/008-editor-restoration/spec.md, phase M).
 *
 * Every mode gets the save guard (save_guard.c: GAME_LoadGame refuses a save
 * it would crash on or load wrongly; spec 008 B1 "Saves"), and before it
 * every mode gets the port's start settings (host_port_defaults: full screen,
 * real shadow); beyond them, Play (the default) changes nothing here. Develop and Play edits are the
 * developer folder's game; Develop also reconnects Cryo's developer tools,
 * whose host bindings live where their inputs are (user.c: the keypad, the
 * editor's mouse feed, the inserted editor call). Host-only structure: every
 * replacement below is a whole retail function swapped for host code through
 * the replacement table (lift/replacements.py HOST_ENTRIES, wd_try_replace),
 * never an instruction change.
 *
 * Develop:
 *   CD_OpenAudio (0x4042f1) returns 0 without opening the MCI cdaudio device:
 *   no CD music, as in the developers' build, whose ACD_Init_ (July DOS
 *   0x42600) is xor eax,eax; ret. Every retail music path then stays silent:
 *   the device id 0x49d1b0 stays 0 and bit 0x200 of 0x626f80 is never set,
 *   which the MCI wrappers and the message cases test. Failing the open with
 *   an MCI error instead would make the game show its modal "MCI Error" box
 *   (CD_ShowMciError 0x404278) at every start.
 *
 *   The Dreams Editor gets the July menu (editor_menu.c, phase 1), and the
 *   keyboard is DOS keys (dev_keys.c, phase 2), whose list is printed here;
 *   the project records are a bank in guest memory (editor_bank.c, phase 4),
 *   and the pickers page host tables (editor_pickers.c, phase 3). Cryo's
 *   developer tools are dev_tools.c and dev_save_page.c (phase 7).
 */
#include <stdio.h>
#include <string.h>
#define RECOMP_GENERATED_CODE
#include "host.h"
#include "render_boundary.h"

/* CD_OpenAudio: __watcall, no arguments, EAX = 1 opened / 0 not; a plain RET. */
static void cd_open_audio_none(void) {
    g_eax = 0;
    g_esp += 4;
}

/* Two Options-page settings start the port's way, not retail's (port
 * defaults; the Options page and the hotkeys still switch both in game, and
 * neither is kept by a save or a file, so retail starts the same way every
 * time):
 *
 *   "Cinemascope / full screen" is the dword 0x49d9f8 (F11 off, F12 on): set,
 *   the 3D view is h*3/4 tall between black bands (VID_SetResolution
 *   0x41592c, GAME_DrawFrame 0x423f60). The retail image starts it at 1; the
 *   port starts in full screen, WD_CINEMASCOPE=1 keeps the retail start.
 *
 *   "Real / 2D shadow" is the dword 0x4a3168 (also the hotkey at 0x415b5f):
 *   PHYS_InitEntity (0x43d7b3) gives Duncan the rendered silhouette
 *   (ENT_RenderShadowTexture 0x43eb67) instead of the OMBRE blob. The retail
 *   image starts it at 0; the port starts with the real shadow,
 *   WD_REAL_SHADOW=0 keeps the blob. Not in the browser build, whose WebGL
 *   path for the shadow mask has not been checked.
 *
 * Both are initialised data, so a write before the entry point stays. Written
 * before WD_POKE, which can still set either, and only into the game's image:
 * the same host runs the difftest programs. The image is the game when the
 * Options page's two toggles (0x430dcb, 0x430e0f: xor byte ptr [flag], 1)
 * are there. */
static int game_image(void) {
    static const uint8_t toggles[2][7] = {
        {0x80, 0x35, 0x68, 0x31, 0x4a, 0x00, 0x01},
        {0x80, 0x35, 0xf8, 0xd9, 0x49, 0x00, 0x01},
    };
    const uint32_t at[2] = {0x430dcbu, 0x430e0fu};
    if (WD_IMAGE_BASE + wd_image_span() < 0x4a3168u + 4u) return 0;
    for (int i = 0; i < 2; i++)
        if (memcmp(wd_host_range(at[i], 7, 0), toggles[i], 7)) return 0;
    return 1;
}

static uint32_t setting(const char* name, uint32_t fallback) {
    const char* v = host_env(name);
    return v ? strcmp(v, "0") != 0 : fallback;
}

void host_port_defaults(void) {
    if (!game_image()) return;
    const uint32_t cinemascope = setting("WD_CINEMASCOPE", 0);
#ifdef __EMSCRIPTEN__
    const uint32_t real_shadow = setting("WD_REAL_SHADOW", 0);
#else
    const uint32_t real_shadow = setting("WD_REAL_SHADOW", 1);
#endif
    WD_HOST_WRITE32(0x49d9f8u) = cinemascope;
    WD_HOST_WRITE32(0x4a3168u) = real_shadow;
    fprintf(stderr, "[mode] at start: cinemascope %s, %s shadow\n", cinemascope ? "on" : "off",
            real_shadow ? "real" : "2D");
}

void host_mode_install(void) {
    save_guard_install();   /* every mode (spec 008 B1 "Saves") */
    if (host_mode() != WD_MODE_DEVELOP) return;
    if (!wd_install_replacement(0x004042F1u, cd_open_audio_none))
        fprintf(stderr, "[mode] WARNING: cannot replace CD_OpenAudio (0x4042f1)\n");
    else
        fprintf(stderr, "[mode] Develop: CD_OpenAudio replaced (no CD music)\n");
    editor_menu_install();
    editor_bank_install();
    editor_pickers_install();
    dev_tools_install();
    dev_keys_print();   /* the key list on the console (spec 008 B1 "Where the keys are shown") */
}

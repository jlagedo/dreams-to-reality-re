/*
 * WINDREAM recompilation - what each launch mode changes in the game
 * (docs/specs/008-editor-restoration/spec.md, phase M).
 *
 * Every mode gets the save guard (save_guard.c: GAME_LoadGame refuses a save
 * it would crash on or load wrongly; spec 008 B1 "Saves"); beyond it, Play
 * (the default) changes nothing here. Develop and Play edits are the
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
#define RECOMP_GENERATED_CODE
#include "host.h"
#include "render_boundary.h"

/* CD_OpenAudio: __watcall, no arguments, EAX = 1 opened / 0 not; a plain RET. */
static void cd_open_audio_none(void) {
    g_eax = 0;
    g_esp += 4;
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

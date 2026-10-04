/*
 * The keys of Develop (docs/specs/008-editor-restoration/spec.md, phase 2), as
 * one list for the two places that show them: the launcher's Develop tab
 * (ui.cpp) and the console when Develop starts (host/sdl/dev_keys.c). Plain
 * data, C and C++. Cryo's in-game F10 key help stays retail.
 *
 * Keys are typed characters, as in the DOS build: Shift+a is A, and the
 * AZERTY keyboard is Cryo's. Each pair is the key, then what it does.
 */
#ifndef DREAMS_DEV_KEYS_H
#define DREAMS_DEV_KEYS_H

static const char* const wd_dev_keys[][2] = {
    {"!", "open and close the editor (Shift+1 on QWERTY; keypad 5 too)"},
    {"A Z E R T", "editor on: create a project, objet, link, box, event"},
    {"Q S D F G", "editor on: load a project, objet, link, box, event"},
    {"W X C V B", "editor on: save the project, objet, link, box, event"},
    {"1 to 6", "editor on: intro movie, mesh, exit target, path point add, drop, symbol"},
    {"0", "editor on: put the player's position into the visible rows"},
    {"? .", "editor on: copy, paste a project"},
    {"/ and Ctrl+Shift+2", "editor on: copy, paste an objet (paste is also the AZERTY key with the section sign)"},
    {": and Ctrl+Shift+3", "editor on: copy, paste a link (! only opens and closes the editor)"},
    {"% and Ctrl+Shift+4", "editor on: copy, paste a box (paste is also the AZERTY micro sign)"},
    {"Space, Esc", "editor on, on a list page: confirm, cancel (the game does not see them)"},
    {"F10", "editor on: write the levels to the folder (off: the game's key help)"},
    {"-", "free-fly camera: mouse turns, buttons fly, PgUp PgDn Home End set the speed (editor off)"},
    {"9", "overhead camera"},
    {"8 / keypad 1", "frame rate and memory readout"},
    {"keypad 2, keypad 3", "object HUD, collision view (hold Backspace)"},
    {"keypad 4", "the flag of 6: capture every frame, frame step pinned at 2.0"},
    {"6, 7", "capture every frame (step 2.0), one frame, to DATA\\TGA (editor off for 6; numbered from 0000 each session, over older files)"},
    {"A (editor off)", "HUD on and off"},
    {"D (editor off)", "dialogue test"},
    {"H", "the level's movie again"},
    {"r, R (editor off)", "record a demo / stop and write DATA\\REPLAY.BIN (then the title); replay it, Space stops"},
    {"e f l v", "render classes (software renderer); Shift+L is the game's Load page"},
    {"AZERTY u grave", "the game's object page"},
    {"keypad 0", "Cryo's Save page: save anywhere with a title, into an empty slot, else the oldest unprotected"},
    {"keypad 6 7 8 9", "give all items, collision views, profiler (frame; 3D render, entities, collision), console window"},
    {"arrows", "walk; with the editor on the WASD keys are editor letters"},
    {"F1 to F6", "resolution; also closes the editor"},
    {"Caps Lock", "turns plain letters into editor commands, as in DOS"},
};

#define WD_DEV_KEY_COUNT (sizeof wd_dev_keys / sizeof wd_dev_keys[0])

#endif /* DREAMS_DEV_KEYS_H */

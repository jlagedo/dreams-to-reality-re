#pragma once

#include "port/save_game.h"

#include <array>
#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

struct MenuCorner {
    int bank = -1;
    int slot = -1;
    int x = 0;
    int y = 0;
};

struct BootMenuState {
    int selected = 0;  // 0x4a2ef9
    int submenu = 0;   // 0x4a2eed: 1 while the Options page is open
    bool dirty = true;
};

// Options globals shared by the boot and in-game menus. Initial values are the
// executable's data section; no retail path reads or writes them to a file.
struct FrontEndOptions {
    int selected = 0;          // 0x4a157b, never reset when the page opens
    uint8_t real_shadow = 0;   // 0x4a3168: read by PHYS_InitEntity at level load
    uint8_t manual_fight = 0;  // 0x49d9f4: read by ENT_TickPlayerControl
    uint8_t volume_max = 1;    // 0x4a157f
    uint8_t cinemascope = 1;   // 0x49d9f8: GAME_DrawFrame blanks h/8 bands
    float level_transition = 0.0f; // 0x5e5480, armed to 10.0 by two toggles
};

// One TEXT_Print(x, y, font, dim, text) request on the 640x480 canvas. A dim
// row has each RGB565 pixel halved, (c & 0xF7DE) >> 1 (0x401c75).
struct MenuTextLine {
    int x = 0;
    int y = 0;
    int font = 0;
    bool dim = false;
    std::string text;
};

// Save-slot page state (MENU_InitSaveSlotSelect, MENU_DrawSaveSlots and
// MENU_HandleSaveSlotInput). Only load mode (0x4a2f41 = 0) is reachable from
// the boot menu.
struct SaveSlotPage {
    int selected = 0;        // 0x4a2f3d
    bool finished = false;   // 0x4a2f35
    bool blinking = false;   // 0x4a2f49
    int blink_ticks = 0;     // 0x4a2f4d
    bool cancelled = false;  // 0x4a2f55
    int cursor_phase = 1;    // 0x4a2f39
    int status_code = 0;     // 0x4a2ef5, written but never read on the boot menu
    std::array<char, SaveIndex::name_size> backup{}; // 0x626f30
    // Host-only: the last confirm reached GAME_LoadGame, which is not ported.
    bool restore_unavailable = false;
};

// State MENU_Tick reaches for the Load page. `root` replaces the retail
// X:\CRYO\DREAMS\ install root; GAME_LoadIndex fills `index` at startup.
struct FrontEndSaves {
    std::filesystem::path root;
    SaveIndex index;
    SaveIcon icon;
    SaveSlotPage page;
};

enum class MenuAction { none, up, down, left, right, confirm, cancel };
enum class MenuChoice { none, new_game, load_game, options, quit };

// The retail menu resolves four inactive and four active INTERF sprites and
// places them in a resolution-scaled 2x2 grid.
bool MENU_PlaceCornerIcons(int width, int height,
                           std::array<MenuCorner, 8>& corners);

// Front-end slice of MENU_Tick, called once per pump iteration (also with no
// action). Confirming Load or Options opens submenu 1 for the selected item;
// its page then replaces the main-menu handlers until it exits.
// master_volume_request is the value the options page passes to
// DSOUND_SetMasterVolume, or -1. The Load page's lines for this tick are left
// in `save_lines`.
MenuChoice MENU_Tick(BootMenuState& state, FrontEndOptions& options,
                     FrontEndSaves& saves, MenuAction action, int& sound_id,
                     int& master_volume_request,
                     std::vector<MenuTextLine>* save_lines = nullptr);
std::string_view MENU_SelectedLabel(const BootMenuState& state);

// MENU_DrawOptionsPage (0x430a45): four rows at x=(W/2-30)/sx+100/sx and
// y={100,130,160,190}/sy in font 0; only the selected row is undimmed.
std::array<MenuTextLine, 4> MENU_DrawOptionsPage(const FrontEndOptions& options,
                                                 int width = 640, int scale_x = 1,
                                                 int scale_y = 1);

// MENU_InitSaveSlotSelect (0x437aa2). Mode 0 selects the newest slot (slot 0
// when all are empty, first on ties); mode 1 the newest unprotected one.
// Returns 1 for a protected selection and 2 otherwise.
int MENU_InitSaveSlotSelect(FrontEndSaves& saves, int mode);

// MENU_DrawSaveSlots (0x437c01), load mode: ten font-1 rows at
// x=(W-250)/sx (protected rows 9/sx further left) and y=i*(20/sy)+2*(50/sy).
// While a finished load blinks it counts ticks and requests sound 10 after 30.
std::vector<MenuTextLine> MENU_DrawSaveSlots(FrontEndSaves& saves, int mode,
                                             int& sound_id, int width = 640,
                                             int scale_x = 1, int scale_y = 1);

// MENU_HandleSaveSlotInput (0x4373a7), load mode. Returns the status code.
int MENU_HandleSaveSlotInput(FrontEndSaves& saves, MenuAction action, int& sound_id);

// MENU_HandleOptionsInput (0x430cd3). Up/down clamp with sound 10, confirm
// toggles the row, cancel clears submenu_state (the pointer passed in EAX).
void MENU_HandleOptionsInput(FrontEndOptions& options, MenuAction action,
                             int& submenu_state, int& sound_id,
                             int& master_volume_request);

} // namespace od::port

#include "port/boot_menu.h"

#include "port/sprite.h"

#include <cstdio>
#include <cstring>

namespace od::port {

bool MENU_PlaceCornerIcons(int width, int height,
                           std::array<MenuCorner, 8>& corners) {
    if (width <= 0 || height <= 0) return false;
    constexpr std::array<std::string_view, 8> names{
        "UpLfNA", "UpRgNA", "DnLfNA", "DnRgNA",
        "UpLf", "UpRg", "DnLf", "DnRg"
    };
    const int sx = width < 401 ? 2 : 1;
    const int sy = height < 400 ? 2 : 1;
    for (size_t i = 0; i < corners.size(); ++i) {
        const IconLookup lookup = ICON_FindByName(names[i]);
        if (lookup.bank < 0 || lookup.slot < 0) return false;
        const int cell = static_cast<int>(i % 4);
        corners[i] = {lookup.bank, lookup.slot,
                      50 / sx + (cell % 2) * (64 / sx),
                      85 / sy + (cell / 2) * (64 / sy)};
    }
    return true;
}

MenuChoice MENU_Tick(BootMenuState& state, FrontEndOptions& options,
                     FrontEndSaves& saves, MenuAction action, int& sound_id,
                     int& master_volume_request,
                     std::vector<MenuTextLine>* save_lines) {
    sound_id = -1;
    master_volume_request = -1;
    if (save_lines) save_lines->clear();
    if (state.selected < 0 || state.selected > 3) state.selected = 0;
    // 0x436305-0x436379: draw the slots, then handle input until the page
    // finishes; a finished load waits for its blink before leaving.
    if (state.submenu == 1 && state.selected == 1) {
        int draw_sound = -1;
        std::vector<MenuTextLine> lines = MENU_DrawSaveSlots(saves, 0, draw_sound);
        if (save_lines) *save_lines = std::move(lines);
        sound_id = draw_sound;
        if (!saves.page.finished) {
            int input_sound = -1;
            saves.page.status_code = MENU_HandleSaveSlotInput(saves, action, input_sound);
            if (input_sound >= 0) sound_id = input_sound;
        } else if (!saves.page.blinking) {
            const bool loaded = !saves.page.cancelled;
            saves.page.status_code = 0;
            state.submenu = 0;
            state.dirty = true;
            // 0x626f00/0x4a2ee9: leave the selection loop with a loaded game.
            if (loaded) return MenuChoice::load_game;
        }
        state.dirty = true;
        return MenuChoice::none;
    }
    // 0x436385/0x43638f: the Options page replaces the state-0 handlers
    // (Esc-to-quit, confirm and grid navigation) while it is open.
    if (state.submenu == 1 && state.selected == 2) {
        MENU_HandleOptionsInput(options, action, state.submenu, sound_id,
                                master_volume_request);
        state.dirty = true;
        return MenuChoice::none;
    }
    if (action == MenuAction::cancel) {
        state.selected = 3;
        state.dirty = true;
        sound_id = 10;
        return MenuChoice::quit;
    }
    if (action == MenuAction::confirm) {
        sound_id = 10;
        switch (state.selected) {
        case 0: return MenuChoice::new_game;
        case 1:
            state.submenu = 1; // 0x43616a-0x436180; 0x4a2ef1 = 0
            state.dirty = true;
            MENU_InitSaveSlotSelect(saves, 0);
            return MenuChoice::none;
        case 2:
            state.submenu = 1; // 0x436187
            state.dirty = true;
            return MenuChoice::options;
        default: return MenuChoice::quit;
        }
    }
    constexpr std::array<int, 4> up{2, 0, 3, 1};
    constexpr std::array<int, 4> down{1, 3, 0, 2};
    constexpr std::array<int, 4> left{2, 0, 3, 1};
    constexpr std::array<int, 4> right{1, 3, 0, 2};
    int next = state.selected;
    switch (action) {
    case MenuAction::up: next = up[next]; break;
    case MenuAction::down: next = down[next]; break;
    case MenuAction::left: next = left[next]; break;
    case MenuAction::right: next = right[next]; break;
    default: break;
    }
    if (next != state.selected) {
        state.selected = next;
        state.dirty = true;
        sound_id = 9;
    }
    return MenuChoice::none;
}

std::array<MenuTextLine, 4> MENU_DrawOptionsPage(const FrontEndOptions& options,
                                                 int width, int scale_x,
                                                 int scale_y) {
    if (scale_x < 1) scale_x = 1;
    if (scale_y < 1) scale_y = 1;
    // 0x430ba8/0x430c06/0x430c64/0x430cc2: x = 100/sx + (W/2 - 30)/sx.
    const int x = 100 / scale_x + (width / 2 - 30) / scale_x;
    const std::array<std::string_view, 4> text{
        options.real_shadow == 1 ? "Real shadow" : "2D shadow",
        options.manual_fight == 1 ? "Manual fight " : "Automatic fight ",
        options.volume_max == 1 ? "Volume MAX" : "Volume MIN",
        options.cinemascope == 1 ? "Cinemascope" : "Full screen"};
    std::array<MenuTextLine, 4> lines{};
    for (int row = 0; row < 4; ++row) {
        auto& line = lines[static_cast<size_t>(row)];
        line.x = x;
        line.y = (100 + 30 * row) / scale_y;
        line.font = 0;
        line.dim = row != options.selected;
        line.text = std::string(text[static_cast<size_t>(row)]);
    }
    return lines;
}

int MENU_InitSaveSlotSelect(FrontEndSaves& saves, int mode) {
    const SaveIndex& index = saves.index;
    SaveSlotPage& page = saves.page;
    int best = 0;
    int slot = 0;
    for (int i = 0; i < 10; ++i) {
        const auto at = static_cast<size_t>(i);
        // Strict > (JLE at 0x437b6c): ties keep the first slot.
        if ((mode == 0 || index.status[at] == 0) && index.recency[at] > best) {
            best = index.recency[at];
            slot = i;
        }
    }
    // Retail leaves the save-mode index uninitialized when no unprotected slot
    // is newer than 0; the port keeps slot 0 there.
    page.selected = slot;
    if (mode != 0) page.backup = index.names[static_cast<size_t>(slot)];
    const int result = index.status[static_cast<size_t>(slot)] != 0 ? 1 : 2;
    GAME_LoadSaveIcon(saves.root, index, index.names[static_cast<size_t>(slot)].data(),
                      saves.icon);
    page.finished = false;
    page.blinking = false;
    page.cursor_phase = 1;
    page.cancelled = false;
    page.restore_unavailable = false;
    return result;
}

std::vector<MenuTextLine> MENU_DrawSaveSlots(FrontEndSaves& saves, int mode,
                                             int& sound_id, int width,
                                             int scale_x, int scale_y) {
    sound_id = -1;
    SaveSlotPage& page = saves.page;
    const SaveIndex& index = saves.index;
    std::vector<MenuTextLine> lines;
    if (scale_x < 1) scale_x = 1;
    if (scale_y < 1) scale_y = 1;
    if (mode != 0) return lines; // Save mode (in-game only) is not ported.
    if (page.finished && !page.blinking) return lines; // Draws nothing.
    if (page.finished) {
        // 0x438909: count the success blink and request sound 10 after 30.
        if (++page.blink_ticks > 30) {
            sound_id = 10;
            page.blinking = false;
        }
    }
    const int row_height = 20 / scale_y;
    const int x = (width - 250) / scale_x;
    for (int i = 0; i < 10; ++i) {
        const auto at = static_cast<size_t>(i);
        const bool empty = index.empty(at);
        const bool guarded = !empty && index.status[at] != 0;
        char text[64]{};
        const std::string name = index.name(at);
        if (empty)
            std::snprintf(text, sizeof(text), i == 9 ? "%d. Empty" : "%d.  Empty", i + 1);
        else if (!guarded)
            std::snprintf(text, sizeof(text), i == 9 ? "%d. %s" : "%d.  %s", i + 1,
                          name.c_str());
        else
            std::snprintf(text, sizeof(text), i == 9 ? "*%d. %s" : "*%d.  %s", i + 1,
                          name.c_str());
        bool dim = i != page.selected;
        // During the blink empty rows stay dark; the selected row is bright
        // only on even counts (0x4389a8-0x4389bf).
        if (page.finished) dim = empty || i != page.selected || (page.blink_ticks & 1) != 0;
        MenuTextLine line;
        line.x = guarded ? x - 9 / scale_x : x;
        line.y = i * row_height + 2 * (50 / scale_y);
        line.font = 1;
        line.dim = dim;
        line.text = text;
        lines.push_back(std::move(line));
    }
    return lines;
}

int MENU_HandleSaveSlotInput(FrontEndSaves& saves, MenuAction action, int& sound_id) {
    sound_id = -1;
    SaveSlotPage& page = saves.page;
    const SaveIndex& index = saves.index;
    int code = 0;
    if (action == MenuAction::cancel) { // 0x4373d4
        page.finished = true;
        page.blinking = false;
        page.cancelled = true;
        page.restore_unavailable = false;
        sound_id = 10;
        return code;
    }
    // Tab (protect toggle) is only updated by the in-game menu.
    if (action == MenuAction::up || action == MenuAction::down) {
        int next = page.selected + (action == MenuAction::up ? -1 : 1);
        // Sound 9 plays only when the selection is clamped.
        if (next < 0) { next = 0; sound_id = 9; }
        if (next > 9) { next = 9; sound_id = 9; }
        page.selected = next;
        const auto at = static_cast<size_t>(next);
        code = index.status[at] != 0 ? 1 : 2;
        GAME_LoadSaveIcon(saves.root, index, index.names[at].data(), saves.icon);
        page.backup = index.names[at];
        page.cursor_phase = 1;
        page.restore_unavailable = false;
        return code;
    }
    if (action == MenuAction::confirm) { // Enter (0x4a2f5d) or Space (0x49d30e).
        const auto at = static_cast<size_t>(page.selected);
        if (index.empty(at)) return code; // No sound and no state change.
        // GAME_LoadGame (0x40f94a) is not ported: take its failure branch,
        // code 8, which leaves the page open and is silent on the boot menu.
        page.restore_unavailable = true;
        code = 8;
    }
    return code;
}

void MENU_HandleOptionsInput(FrontEndOptions& options, MenuAction action,
                             int& submenu_state, int& sound_id,
                             int& master_volume_request) {
    sound_id = -1;
    master_volume_request = -1;
    switch (action) {
    case MenuAction::up:
        // Clamped; sound 10 plays even when the row cannot move.
        options.selected = options.selected - 1 < 0 ? 0 : options.selected - 1;
        sound_id = 10;
        break;
    case MenuAction::down:
        options.selected = options.selected + 1 > 3 ? 3 : options.selected + 1;
        sound_id = 10;
        break;
    case MenuAction::confirm: // Enter (0x4a2f5d) or Space (0x49d30e); silent.
        switch (options.selected) {
        case 0:
            options.real_shadow ^= 1;
            options.level_transition = 10.0f;
            break;
        case 1: options.manual_fight ^= 1; break;
        case 2:
            options.volume_max ^= 1;
            master_volume_request = options.volume_max == 1 ? 100 : 40;
            break;
        case 3:
            options.cinemascope ^= 1;
            options.level_transition = 10.0f;
            break;
        default: break;
        }
        break;
    case MenuAction::cancel: submenu_state = 0; break; // Esc, silent.
    default: break;
    }
}

std::string_view MENU_SelectedLabel(const BootMenuState& state) {
    constexpr std::array<std::string_view, 4> labels{
        "NEW GAME", "LOAD A GAME", "OPTIONS", "QUIT"
    };
    return labels[state.selected >= 0 && state.selected < 4 ? state.selected : 0];
}

} // namespace od::port

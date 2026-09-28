#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace od::port {

struct MenuCorner {
    int bank = -1;
    int slot = -1;
    int x = 0;
    int y = 0;
};

struct BootMenuState {
    int selected = 0;
    bool dirty = true;
};

enum class MenuAction { none, up, down, left, right, confirm, cancel };
enum class MenuChoice { none, new_game, load_game, options, quit };

// The retail menu resolves four inactive and four active INTERF sprites and
// places them in a resolution-scaled 2x2 grid.
bool MENU_PlaceCornerIcons(int width, int height,
                           std::array<MenuCorner, 8>& corners);

// Front-end slice of MENU_Tick. The platform adapter supplies press/repeat
// actions; the save/options controllers and game handoff are still separate.
MenuChoice MENU_Tick(BootMenuState& state, MenuAction action, int& sound_id);
std::string_view MENU_SelectedLabel(const BootMenuState& state);

} // namespace od::port

#include "port/boot_menu.h"

#include "port/sprite.h"

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

MenuChoice MENU_Tick(BootMenuState& state, MenuAction action, int& sound_id) {
    sound_id = -1;
    if (state.selected < 0 || state.selected > 3) state.selected = 0;
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
        case 1: return MenuChoice::load_game;
        case 2: return MenuChoice::options;
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

std::string_view MENU_SelectedLabel(const BootMenuState& state) {
    constexpr std::array<std::string_view, 4> labels{
        "NEW GAME", "LOAD A GAME", "OPTIONS", "QUIT"
    };
    return labels[state.selected >= 0 && state.selected < 4 ? state.selected : 0];
}

} // namespace od::port

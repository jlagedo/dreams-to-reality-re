#include "port/boot_menu.h"

#include <array>
#include <iostream>

int main() {
    std::array<od::port::MenuCorner, 8> corners{};
    if (!od::port::MENU_PlaceCornerIcons(640, 480, corners) ||
        corners[0].bank != 4 || corners[0].x != 50 || corners[0].y != 85 ||
        corners[1].x != 114 || corners[2].y != 149 ||
        corners[4].slot != 2) {
        std::cerr << "retail INTERF corner lookup or 640x480 placement changed\n";
        return 1;
    }
    if (!od::port::MENU_PlaceCornerIcons(320, 200, corners) ||
        corners[0].x != 25 || corners[0].y != 42 ||
        corners[1].x != 57 || corners[2].y != 74) {
        std::cerr << "retail low-resolution menu placement changed\n";
        return 1;
    }
    od::port::BootMenuState state;
    int sound = -1;
    using A = od::port::MenuAction;
    if (od::port::MENU_Tick(state, A::right, sound) != od::port::MenuChoice::none ||
        state.selected != 1 || sound != 9 ||
        od::port::MENU_Tick(state, A::confirm, sound) !=
            od::port::MenuChoice::load_game || sound != 10 ||
        od::port::MENU_Tick(state, A::right, sound) !=
            od::port::MenuChoice::none || state.selected != 3 ||
        od::port::MENU_Tick(state, A::left, sound) !=
            od::port::MenuChoice::none || state.selected != 1 ||
        od::port::MENU_Tick(state, A::cancel, sound) !=
            od::port::MenuChoice::quit || state.selected != 3) {
        std::cerr << "retail main-menu choice graph or action result changed\n";
        return 1;
    }
    return 0;
}

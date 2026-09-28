#include "audio/audio_output.h"
#include "port/boot_menu.h"
#include "port/save_game.h"
#include "port/sound.h"

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "line " << __LINE__ << ": " #expr "\n"; return false; } } while (0)

namespace {

using A = od::port::MenuAction;
using Choice = od::port::MenuChoice;

struct Menu {
    od::port::BootMenuState state;
    od::port::FrontEndOptions options;
    od::port::FrontEndSaves saves;
    std::vector<od::port::MenuTextLine> lines;
    int sound = -1;
    int volume = -1;
    Choice tick(A action) {
        return od::port::MENU_Tick(state, options, saves, action, sound, volume, &lines);
    }
};

void put32(std::vector<uint8_t>& bytes, size_t at, int32_t value) {
    for (int i = 0; i < 4; ++i)
        bytes[at + static_cast<size_t>(i)] =
            static_cast<uint8_t>(static_cast<uint32_t>(value) >> (8 * i));
}

bool test_corners() {
    std::array<od::port::MenuCorner, 8> corners{};
    CHECK(od::port::MENU_PlaceCornerIcons(640, 480, corners));
    CHECK(corners[0].bank == 4 && corners[0].x == 50 && corners[0].y == 85);
    CHECK(corners[1].x == 114 && corners[2].y == 149 && corners[4].slot == 2);
    CHECK(od::port::MENU_PlaceCornerIcons(320, 200, corners));
    CHECK(corners[0].x == 25 && corners[0].y == 42);
    CHECK(corners[1].x == 57 && corners[2].y == 74);
    return true;
}

bool test_main_grid() {
    Menu menu;
    CHECK(menu.tick(A::right) == Choice::none && menu.state.selected == 1 && menu.sound == 9);
    CHECK(menu.tick(A::right) == Choice::none && menu.state.selected == 3);
    CHECK(menu.tick(A::left) == Choice::none && menu.state.selected == 1);
    CHECK(menu.tick(A::none) == Choice::none && menu.sound == -1);
    CHECK(menu.tick(A::cancel) == Choice::quit && menu.state.selected == 3 && menu.sound == 10);
    Menu start;
    CHECK(start.tick(A::confirm) == Choice::new_game && start.sound == 10);
    return true;
}

bool test_options_page() {
    Menu menu;
    menu.state.selected = 2;
    CHECK(menu.tick(A::confirm) == Choice::options && menu.sound == 10);
    CHECK(menu.state.submenu == 1);
    // Retail data-section defaults and 640x480 layout.
    auto lines = od::port::MENU_DrawOptionsPage(menu.options);
    CHECK(lines[0].text == "2D shadow" && lines[1].text == "Automatic fight ");
    CHECK(lines[2].text == "Volume MAX" && lines[3].text == "Cinemascope");
    for (int row = 0; row < 4; ++row) {
        CHECK(lines[static_cast<size_t>(row)].x == 390);
        CHECK(lines[static_cast<size_t>(row)].y == 100 + 30 * row);
        CHECK(lines[static_cast<size_t>(row)].font == 0);
        CHECK(lines[static_cast<size_t>(row)].dim == (row != 0));
    }
    // Up at the first row still plays sound 10; confirm is silent.
    CHECK(menu.tick(A::up) == Choice::none && menu.options.selected == 0 && menu.sound == 10);
    CHECK(menu.tick(A::confirm) == Choice::none && menu.sound == -1);
    CHECK(menu.options.real_shadow == 1 && menu.options.level_transition == 10.0f);
    CHECK(od::port::MENU_DrawOptionsPage(menu.options)[0].text == "Real shadow");
    menu.tick(A::down);
    menu.tick(A::down);
    CHECK(menu.tick(A::confirm) == Choice::none && menu.volume == 40);
    CHECK(menu.tick(A::confirm) == Choice::none && menu.volume == 100);
    menu.tick(A::down);
    CHECK(menu.tick(A::down) == Choice::none && menu.options.selected == 3 && menu.sound == 10);
    menu.tick(A::confirm);
    CHECK(od::port::MENU_DrawOptionsPage(menu.options)[3].text == "Full screen");
    // Grid navigation and Esc-to-quit are replaced while the page is open.
    CHECK(menu.tick(A::left) == Choice::none && menu.state.selected == 2);
    CHECK(menu.tick(A::cancel) == Choice::none && menu.state.submenu == 0 && menu.sound == -1);
    CHECK(menu.tick(A::cancel) == Choice::quit);
    // Low-resolution divisors.
    lines = od::port::MENU_DrawOptionsPage(menu.options, 640, 2, 2);
    CHECK(lines[0].x == 50 + 145 && lines[1].y == 65);
    return true;
}

bool test_save_index(const std::filesystem::path& base) {
    const auto game = base / "data" / "game";
    std::filesystem::create_directories(game);
    std::vector<uint8_t> bytes(340);
    const char* names[] = {"Hamam island", "Hamam Pool", "Inside hamam"};
    for (int slot = 0; slot < 3; ++slot) {
        std::memcpy(bytes.data() + slot * 22, names[slot], std::strlen(names[slot]));
        put32(bytes, 0x104 + static_cast<size_t>(slot) * 4, slot + 1);
    }
    put32(bytes, 0x0dc + 1 * 4, 1); // Slot 2 protected.
    const int32_t files[] = {0, 2, 1};
    for (int slot = 0; slot < 10; ++slot)
        put32(bytes, 0x12c + static_cast<size_t>(slot) * 4, slot < 3 ? files[slot] : -1);
    {
        std::ofstream out(game / "game.dat", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        std::vector<uint8_t> icon(0x2000, 0);
        icon[0] = 0x1f; // First pixel pure blue in RGB565.
        std::ofstream ico(game / "game1.ico", std::ios::binary);
        ico.write(reinterpret_cast<const char*>(icon.data()),
                  static_cast<std::streamsize>(icon.size()));
    }
    Menu menu;
    menu.saves.root = base;
    std::string error;
    CHECK(od::port::GAME_LoadIndex(base, menu.saves.index, error) && error.empty());
    CHECK(menu.saves.index.name(2) == "Inside hamam" && menu.saves.index.recency[2] == 3);
    CHECK(menu.saves.index.file_number[1] == 2 && menu.saves.index.empty(3));

    // Load: newest slot (3) is selected and its icon read as a side effect.
    menu.state.selected = 1;
    CHECK(menu.tick(A::confirm) == Choice::none && menu.sound == 10);
    CHECK(menu.state.submenu == 1 && menu.saves.page.selected == 2);
    CHECK(menu.saves.icon.loaded && menu.saves.icon.pixels[0] == 0x1f);
    CHECK(menu.tick(A::none) == Choice::none && menu.lines.size() == 10);
    CHECK(menu.lines[0].text == "1.  Hamam island" && menu.lines[0].x == 390);
    CHECK(menu.lines[1].text == "*2.  Hamam Pool" && menu.lines[1].x == 381);
    CHECK(menu.lines[3].text == "4.  Empty" && menu.lines[9].text == "10. Empty");
    CHECK(menu.lines[9].y == 280 && menu.lines[0].font == 1);
    CHECK(!menu.lines[2].dim && menu.lines[0].dim);
    // Moves are silent; sound 9 plays only at a clamp.
    CHECK(menu.tick(A::up) == Choice::none && menu.saves.page.selected == 1 && menu.sound == -1);
    menu.tick(A::up);
    CHECK(menu.tick(A::up) == Choice::none && menu.saves.page.selected == 0 && menu.sound == 9);
    // Confirming an empty slot does nothing; a saved one reaches the
    // unported GAME_LoadGame and keeps the page open (status 8).
    for (int i = 0; i < 5; ++i) menu.tick(A::down);
    CHECK(menu.saves.page.selected == 5);
    CHECK(menu.tick(A::confirm) == Choice::none && !menu.saves.page.restore_unavailable);
    for (int i = 0; i < 5; ++i) menu.tick(A::up);
    CHECK(menu.tick(A::confirm) == Choice::none && menu.saves.page.restore_unavailable);
    CHECK(menu.saves.page.status_code == 8 && menu.state.submenu == 1);
    // Esc plays sound 10, and the next tick returns to the grid.
    CHECK(menu.tick(A::cancel) == Choice::none && menu.sound == 10);
    CHECK(menu.saves.page.finished && menu.saves.page.cancelled);
    CHECK(menu.tick(A::none) == Choice::none && menu.state.submenu == 0);
    CHECK(menu.lines.empty() && menu.state.selected == 1);

    // Missing index: ten empty slots, and file numbers stay -1.
    od::port::SaveIndex empty;
    CHECK(!od::port::GAME_LoadIndex(base / "none", empty, error));
    CHECK(empty.empty(0) && empty.file_number[9] == -1);
    return true;
}

bool test_success_blink() {
    // The finished-load blink the port reaches once GAME_LoadGame exists.
    od::port::FrontEndSaves saves;
    std::memcpy(saves.index.names[0].data(), "Slot", 4);
    saves.page.finished = true;
    saves.page.blinking = true;
    int sound = -1;
    auto lines = od::port::MENU_DrawSaveSlots(saves, 0, sound);
    CHECK(saves.page.blink_ticks == 1 && lines[0].dim && lines[1].dim);
    lines = od::port::MENU_DrawSaveSlots(saves, 0, sound);
    CHECK(!lines[0].dim && sound == -1);
    for (int i = 0; i < 29; ++i) od::port::MENU_DrawSaveSlots(saves, 0, sound);
    CHECK(sound == 10 && !saves.page.blinking);
    CHECK(od::port::MENU_DrawSaveSlots(saves, 0, sound).empty());
    return true;
}

bool test_volume_and_playlist() {
    od::AudioOutput channel;
    CHECK(od::port::DSOUND_SetChannelVolume(channel, 127, 127) == 10);
    CHECK(od::port::DSOUND_SetChannelVolume(channel, 100, 127) == 43);
    CHECK(od::port::DSOUND_SetChannelVolume(channel, 127, 0) == 10000);
    CHECK(std::fabs(channel.gain() - 0.00001f) < 0.000001f);
    int master = 127;
    od::AudioOutput* channels[] = {&channel};
    od::port::DSOUND_SetMasterVolume(master, 40, channels, 1);
    // trunc(40 * 40 / 127) = 12 -> 10^(115 * 3/127 + 1) = 5206 hundredths.
    CHECK(master == 40 && std::fabs(channel.gain() - std::pow(10.0f, -5206 / 2000.0f)) < 1e-6f);

    od::port::CdPlaylist playlist;
    od::AudioOutput music;
    od::port::CD_SetPlaylist(playlist, 9, music);
    CHECK(playlist.enabled && playlist.count == 1 && playlist.tracks[0] == 9);
    CHECK(playlist.index == -1);
    od::port::CD_SetPlaylist(playlist, 0x000b0003u, music);
    CHECK(playlist.count == 2 && playlist.tracks[0] == 3 && playlist.tracks[2] == 11);
    od::port::CD_StopMusic(playlist, music);
    CHECK(!playlist.enabled);
    return true;
}

} // namespace

int main() {
    const auto base = std::filesystem::temp_directory_path() / "od-boot-menu-tests";
    std::error_code ignored;
    std::filesystem::remove_all(base, ignored);
    const bool ok = test_corners() && test_main_grid() && test_options_page() &&
                    test_save_index(base) && test_success_blink() &&
                    test_volume_and_playlist();
    std::filesystem::remove_all(base, ignored);
    if (!ok) return 1;
    std::cout << "boot menu checks passed\n";
    return 0;
}

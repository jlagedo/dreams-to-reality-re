#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/camera.h"
#include "port/game_entry.h"
#include "port/game_state.h"
#include "port/scene.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

// BOOT_Run's New Game reset block and its two callees.
int check_new_game_reset() {
    od::port::GameState game;
    game.health = 12.0f;
    game.magic = 3.0f;
    game.hotkeys_a = {{4, 5, 6}};
    game.hotkeys_b = {{7, 8, 9}};
    game.hotkeys_c = {{1, 2, 3}};
    game.hud_icon = {{2, 3}};
    game.elder_latch = 0;
    game.inventory.count = 7;
    game.inventory.names[0][0] = 'X';
    for (auto& slot : game.levels.slots) {
        slot[0x500] = 'L';
        slot[0x10] = 0x55;
    }
    game.levels.ring_index = 5;
    od::port::BOOT_ResetNewGame(game);
    if (game.levels.ring_index != 0 || game.levels.slots[7][0x500] != 0 ||
        game.levels.slots[7][0x10] != 0x55) return 20; // only names clear
    const auto& inventory = game.inventory;
    if (inventory.count != 0 || inventory.selected != 0xffffff00u ||
        inventory.hotkey_item[2] != -1 || inventory.item_value[31] != 50.0f ||
        inventory.bind_value[0] != 0 || inventory.names[0][0] != 'X') return 21;
    if (game.record_flags[0] != 0x10200u || game.record_flags[3] != 0x20100u) return 22;
    if (game.hotkeys_a[0] != -1 || game.hotkeys_b[2] != -1 ||
        game.hotkeys_c[1] != 2 || game.hud_icon[1] != -1) return 23;
    // New Game leaves health, magic and the elder latch alone.
    if (game.health != 12.0f || game.magic != 3.0f || game.elder_latch != 0 ||
        game.transition != 15.0f) return 24;
    return 0;
}

} // namespace

int main() {
    using od::port::GameEntryEvent;
    using od::port::GameEntryPhase;
    if (const int reset = check_new_game_reset()) {
        std::cerr << "New Game reset check " << reset << " failed\n";
        return reset;
    }
    od::port::GameEntryState state;
    od::port::BOOT_BeginNewGame(state, "ETE_E~1.HNM", false);
    const auto hold_first = od::port::GAME_TickEntry(state, 0.59, false);
    const auto hold_second = od::port::GAME_TickEntry(state, 0.03, false);
    if (hold_first != GameEntryEvent::none ||
        hold_second != GameEntryEvent::stop_menu ||
        state.phase != GameEntryPhase::boot_return) {
        std::cerr << "hold: " << static_cast<int>(hold_first) << ", "
                  << static_cast<int>(hold_second) << ", phase "
                  << static_cast<int>(state.phase) << '\n';
        return 1;
    }
    // Retail never counts the armed 15.0 down on New Game: the first loading
    // tick starts the elder movie immediately.
    if (state.transition_frames != 15.0f ||
        od::port::GAME_TickEntry(state, 1.0 / 30.0, false) !=
            GameEntryEvent::start_elder_movie ||
        od::port::GAME_TickEntry(state, 1.0 / 30.0, false) !=
            GameEntryEvent::none ||
        od::port::GAME_TickEntry(state, 1.0 / 30.0, true) !=
            GameEntryEvent::load_level) return 3;
    od::port::GAME_EntryLoaded(state);
    if (state.phase != GameEntryPhase::running) return 4;

    od::port::BOOT_BeginNewGame(state, "OPTIONAL.HNM", true);
    if (od::port::GAME_TickEntry(state, 0.62, false) != GameEntryEvent::stop_menu ||
        od::port::GAME_TickEntry(state, 0.03, false) !=
            GameEntryEvent::start_project_movie ||
        od::port::GAME_TickEntry(state, 0.1, false) != GameEntryEvent::none ||
        od::port::GAME_TickEntry(state, 0.1, true) !=
            GameEntryEvent::start_elder_movie) return 5;

    const char* cue = std::getenv("DREAMS_CUE1");
    if (!cue || !*cue) {
        std::cout << "entry state checks passed; set DREAMS_CUE1 for Project0 corpus\n";
        return 0;
    }
    od::disc::Error source_error;
    auto opened = od::disc::Image::open(std::filesystem::u8path(cue), source_error);
    if (!opened) { std::cerr << source_error.message << '\n'; return 6; }
    std::shared_ptr<const od::disc::Image> image(std::move(opened));
    od::port::PreviewLevelContext project(image);
    std::string error;
    if (!project.select_project_scene("Project0", error)) {
        std::cerr << error << '\n'; return 7;
    }
    const auto& record = project.project_record();
    od::disc::FileId file;
    const std::string optional(reinterpret_cast<const char*>(record.data() + 0x3c));
    const std::string stale(reinterpret_cast<const char*>(record.data() + 0x3d));
    const bool optional_found = image->find("DATA/HNM/ETE_E~1.HNM", file,
                                            source_error);
    const bool elder_found = image->find("DATA/HNM/TETE_E~1.HNM", file,
                                         source_error);
    const std::string scene(reinterpret_cast<const char*>(record.data() + 0x60c));
    if (!optional.empty() || stale != "ETE_E~1.HNM" || optional_found || !elder_found ||
        scene != "H18ANGKR.DSN") {
        std::cerr << "Project0 assets: optional=" << optional << " found="
                  << optional_found << " elder=" << elder_found << " scene="
                  << scene << '\n';
        return 8;
    }
    if (!od::port::SCENE_LoadLevel(project, error) || !project.loaded() ||
        project.level_graph().faces.size() != 1959 ||
        project.placed_actors().size() != 7 ||
        !project.object_issues().empty() ||
        project.render_graph().faces.size() < project.level_graph().faces.size()) {
        std::cerr << error << '\n'; return 9;
    }
    od::port::FollowCamera camera;
    od::port::CAM_StartFollow(camera, {{-319, -625, -3187}}, 3046,
                              record.data());
    if (camera.eye != std::array<int32_t,3>{{-847, -705, -3166}} ||
        camera.target != std::array<int32_t,3>{{704, -625, -3228}}) {
        std::cerr << "Project0 snapped follow camera differs from preset 0\n";
        return 10;
    }
    std::cout << "Project0 loaded: " << project.level_graph().faces.size()
              << " scene faces, " << project.placed_actors().size()
              << " placed actors, " << project.object_issues().size()
              << " unavailable actors\n";
    return 0;
}

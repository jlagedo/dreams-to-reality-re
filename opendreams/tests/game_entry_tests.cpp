#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/camera.h"
#include "port/game_entry.h"
#include "port/scene.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

int main() {
    using od::port::GameEntryEvent;
    using od::port::GameEntryPhase;
    od::port::GameEntryState state;
    od::port::BOOT_BeginNewGame(state, "ETE_E~1.HNM", false);
    const auto hold_first = od::port::GAME_TickEntry(state, 0.59, false);
    const auto hold_second = od::port::GAME_TickEntry(state, 0.03, false);
    if (hold_first != GameEntryEvent::none ||
        hold_second != GameEntryEvent::stop_menu ||
        state.phase != GameEntryPhase::transition) {
        std::cerr << "hold: " << static_cast<int>(hold_first) << ", "
                  << static_cast<int>(hold_second) << ", phase "
                  << static_cast<int>(state.phase) << '\n';
        return 1;
    }
    for (int frame = 0; frame < 14; ++frame)
        if (od::port::GAME_TickEntry(state, 1.0 / 30.0, false) !=
            GameEntryEvent::none) return 2;
    if (od::port::GAME_TickEntry(state, 1.0 / 30.0, false) !=
            GameEntryEvent::start_elder_movie ||
        od::port::GAME_TickEntry(state, 1.0 / 30.0, false) !=
            GameEntryEvent::none ||
        od::port::GAME_TickEntry(state, 1.0 / 30.0, true) !=
            GameEntryEvent::load_level) return 3;
    od::port::GAME_EntryLoaded(state);
    if (state.phase != GameEntryPhase::running) return 4;

    od::port::BOOT_BeginNewGame(state, "OPTIONAL.HNM", true);
    if (od::port::GAME_TickEntry(state, 0.62, false) !=
            GameEntryEvent::start_project_movie ||
        od::port::GAME_TickEntry(state, 0.1, true) !=
            GameEntryEvent::none ||
        state.phase != GameEntryPhase::transition) return 5;

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

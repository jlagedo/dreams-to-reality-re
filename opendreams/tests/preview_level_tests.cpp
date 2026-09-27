#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/scene.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

int main() {
    const char* cue = std::getenv("DREAMS_CUE2");
    if (!cue || !*cue) {
        std::cout << "SKIP: set DREAMS_CUE2 for Project 71 preview corpus\n";
        return 77;
    }
    od::disc::Error source_error;
    auto opened = od::disc::Image::open(std::filesystem::u8path(cue), source_error);
    if (!opened) {
        std::cerr << source_error.message << '\n';
        return 1;
    }
    std::shared_ptr<const od::disc::Image> image(std::move(opened));
    od::port::PreviewLevelContext context(image);
    std::string error;
    if (!context.select_project("Project71", 2, error) ||
        !od::port::SCENE_LoadLevel(context, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    const auto& actor = context.selected_actor();
    if (!context.loaded() || context.project_name() != "Project71" ||
        context.level_graph().nodes.size() != 55 ||
        context.level_graph().faces.size() != 2225 ||
        actor.object_slot != 2 || actor.asset_name != "CAI.DAN" ||
        !actor.attached_to_camera_root || actor.model.nodes.size() != 1 ||
        actor.model.nodes[0].external_parent_handle != 0 ||
        actor.model.faces.size() != 10 || actor.model.materials.size() != 1 ||
        actor.spawn_xyz != std::array<int32_t, 3>{7312, 3187, 124} ||
        actor.spawn_heading != 2226 ||
        actor.model.nodes[0].local_xyz != actor.spawn_xyz) {
        std::cerr << "Project 71 preview state differs from retail trace\n";
        return 3;
    }
    od::port::PreviewLevelContext selected_model(image);
    if (!selected_model.select_model("DATA/3DC/CAI.DAN", "CAISSE", error) ||
        !od::port::SCENE_LoadLevel(selected_model, error)) {
        std::cerr << "selected model preview: " << error << '\n';
        return 4;
    }
    const auto& isolated = selected_model.selected_actor();
    if (!selected_model.loaded() || selected_model.project_name() != "Preview" ||
        selected_model.level_graph().nodes.size() != 1 ||
        !selected_model.level_graph().faces.empty() ||
        isolated.object_slot != 1 || isolated.asset_name != "CAI.DAN" ||
        isolated.model.faces.size() != 10 || isolated.model.materials.size() != 1 ||
        isolated.spawn_xyz != std::array<int32_t, 3>{0, 0, 0} ||
        isolated.spawn_heading != 0 || !isolated.attached_to_camera_root) {
        std::cerr << "selected CAISSE model did not produce the in-memory preview scene\n";
        return 5;
    }
    std::cout << "Project 71 and selected CAISSE preview paths produced CAI nodes\n";
    return 0;
}

#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/scene.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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
    od::port::PreviewLevelContext physical_scene(image);
    if (!physical_scene.select_scene("DATA/3DC/E29USINE.DSN",error) ||
        !od::port::SCENE_LoadLevel(physical_scene,error)) {
        std::cerr << "physical scene preview: " << error << '\n';
        return 6;
    }
    const auto& graph=physical_scene.render_graph();
    const std::array<uint8_t,20> e29_header{{
        0,0,0,0, 0,0,0,0, 3,0,0,0, 0x51,0xc8,0xa0,0x82, 0,0,0,0}};
    const auto* first_bank=graph.materials.empty() ? nullptr :
        &graph.materials[0].bank;
    if (graph.nodes.size()!=55 || graph.faces.size()!=2225 ||
        graph.materials.size()!=30 ||
        graph.faces[0].material_index>=graph.materials.size() ||
        graph.materials[0].preview_lod!=256 ||
        !graph.materials[0].static_palette_row15 ||
        !first_bank || first_bank->size()!=0x18014u ||
        !std::equal(e29_header.begin(),e29_header.end(),first_bank->begin()) ||
        (*first_bank)[0x14u+15u*0x400u+6u]!=static_cast<uint8_t>(23114) ||
        (*first_bank)[0x14u+15u*0x400u+7u]!=static_cast<uint8_t>(23114>>8)) {
        std::cerr << "physical E29USINE scene materials differ\n";
        return 7;
    }
    od::port::PreviewLevelContext project_scene(image);
    if (!project_scene.select_project_scene("Project71",error) ||
        !od::port::SCENE_LoadLevel(project_scene,error) ||
        project_scene.placed_actors().size()!=2 ||
        !project_scene.object_issues().empty() ||
        project_scene.render_graph().faces.size()!=2807) {
        std::cerr << "Project 71 scene preview: " << error << '\n';
        return 8;
    }
    std::cout << "Project 71 and selected CAISSE preview paths produced CAI nodes\n";
    return 0;
}

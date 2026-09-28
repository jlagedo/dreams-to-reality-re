#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/ddat.h"
#include "port/glide_model.h"
#include "port/player.h"
#include "port/scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

bool bounded_player_faces(const od::port::ModelGraph& graph,
                          std::string& error) {
    od::port::GlideModelDraw prepared;
    if (!od::port::GLIDE_DrawObjectFaces(graph, prepared, error)) return false;
    float longest = 0.0f;
    for (size_t triangle = 0; triangle < prepared.vertices.size(); triangle += 3)
        for (size_t corner = 0; corner < 3; ++corner) {
            const auto& a = prepared.vertices[triangle + corner].position;
            const auto& b = prepared.vertices[triangle + (corner + 1) % 3].position;
            float squared = 0.0f;
            for (size_t axis = 0; axis < 3; ++axis)
                squared += (a[axis] - b[axis]) * (a[axis] - b[axis]);
            longest = std::max(longest, std::sqrt(squared));
        }
    // The stale serialized pointer value 1 names root node 0. Treating it as
    // null produces ~218-unit pelvis-to-thigh edges in the bind/idle model.
    if (longest >= 120.0f) {
        error = "XH_ has a stretched face edge of " + std::to_string(longest);
        return false;
    }
    return true;
}

} // namespace

int main() {
    const char* cue = std::getenv("DREAMS_CUE1");
    if (!cue || !*cue) {
        std::cout << "SKIP: set DREAMS_CUE1 for player cone corpus\n";
        return 77;
    }
    od::disc::Error source_error;
    auto opened = od::disc::Image::open(std::filesystem::u8path(cue), source_error);
    if (!opened) { std::cerr << source_error.message << '\n'; return 1; }
    std::shared_ptr<const od::disc::Image> image(std::move(opened));
    od::port::VfsContext vfs(image);
    od::port::DdatBank bank;
    od::port::DdatError data_error;
    const uint8_t* record = nullptr;
    if (!od::port::DDAT_Load(vfs, bank, data_error) ||
        !od::port::DDAT_LoadRecord(bank, "Project0", record, data_error) ||
        !record) {
        std::cerr << data_error.message << '\n'; return 2;
    }
    od::port::PlayerState player(image);
    od::port::ANIM_InitStateTable(player);
    std::string error;
    if (!od::port::ENT_LoadObject(player, record, "XH_.3DC", error) ||
        !od::port::ANIM_LoadEntitySet(player, error) ||
        !od::port::ANIM_RequestState(player, 0, error) ||
        !od::port::ANIM_ApplyPendingState(player, error)) {
        std::cerr << error << '\n'; return 3;
    }
    std::cout << "XH_ player: " << player.model().nodes.size() << " nodes, "
              << player.model().faces.size() << " faces, " << player.clip_count()
              << " clips; state 0 " << player.active_clip_name() << " ("
              << player.active_clip_duration() << " frames)\n";
    if (player.spawn_xyz() != std::array<int32_t,3>{{-319,-625,-3187}} ||
        player.spawn_heading() != 3046 || player.active_action() != 0 ||
        player.active_clip_name() != "XH_AN000.3DA" ||
        player.active_clip_duration() != 200 ||
        player.model().faces.size() != 504) return 4;
    const auto& nodes = player.bind_model().nodes;
    if (nodes.size() != 27 || nodes[0].parent != -1 ||
        nodes[4].parent != 0 || nodes[7].parent != 0 ||
        nodes[8].parent != 0 || nodes[26].parent != 0) {
        std::cerr << "XH_ root pointer 1 was not resolved to bassin\n";
        return 5;
    }
    if (!bounded_player_faces(player.bind_model(), error)) {
        std::cerr << error << '\n'; return 5;
    }
    for (int frame = 0; frame < 200; ++frame) {
        if (!od::port::ANIM_TickPlayerIdle(player, 1.0/30.0, error) ||
            !bounded_player_faces(player.model(), error)) {
            std::cerr << error << '\n'; return 5;
        }
    }
    od::port::PreviewLevelContext scene(image);
    if (!scene.select_project_scene("Project0", error)) {
        std::cerr << error << '\n'; return 6;
    }
    scene.set_runtime_player(&player);
    if (!od::port::SCENE_LoadLevel(scene, error)) {
        std::cerr << error << '\n'; return 6;
    }
    if (player.active_action() != 0 ||
        player.active_clip_name() != "XH_AN000.3DA") return 9;
    od::port::ModelGraph combined;
    size_t player_base = 0;
    od::port::GlideModelDraw prepared;
    if (!od::port::PLAYER_ComposeRenderGraph(player, scene.render_graph(),
                                             combined, player_base, error) ||
        !od::port::PLAYER_UpdateRenderGraph(player, combined, player_base,
                                            error) ||
        !od::port::GLIDE_DrawObjectFaces(combined, prepared, error)) {
        std::cerr << error << '\n'; return 7;
    }
    if (combined.faces.size() != scene.render_graph().faces.size() + 504 ||
        prepared.vertices.size() != combined.faces.size() * 3) return 8;
    std::vector<od::port::ModelJoint> joints;
    if (!od::port::GLIDE_ModelJoints(combined, joints, error) ||
        player_base >= joints.size() ||
        joints[player_base].world_xyz != player.spawn_xyz()) {
        std::cerr << "player model root is not at Project0 spawn: " << error << '\n';
        return 11;
    }
    return 0;
}

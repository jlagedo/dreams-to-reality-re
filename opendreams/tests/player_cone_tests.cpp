#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/ddat.h"
#include "port/glide_model.h"
#include "port/player.h"
#include "port/scene.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

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
    if (!od::port::ANIM_TickPlayerIdle(player, 1.0/30.0, error)) {
        std::cerr << error << '\n'; return 5;
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
    return 0;
}

#pragma once

#include "port/animation.h"
#include "port/dan.h"
#include "port/vfs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace od::port {

struct EntityAnimSlot {
    int archive_index = -1;
    uint8_t flags = 0;
};

// Source-scoped player actor for the reached Project0 SCENE_InitLevel branch.
// Retail's global entity, resource arena and family table are owned here.
class PlayerState {
public:
    explicit PlayerState(std::shared_ptr<const disc::Image> image);
    const ModelGraph& model() const { return pose_model_; }
    const ModelGraph& bind_model() const { return bind_model_; }
    const std::array<int32_t,3>& spawn_xyz() const { return spawn_xyz_; }
    int32_t spawn_heading() const { return spawn_heading_; }
    std::string_view asset_name() const { return asset_name_; }
    const std::array<EntityAnimSlot,64>& action_slots() const { return slots_; }
    int active_action() const { return active_action_; }
    int pending_action() const { return pending_action_; }
    std::string_view active_clip_name() const { return active_clip_.name; }
    uint32_t active_clip_duration() const { return active_clip_.duration; }
    size_t clip_count() const { return DAN_GetAnimCount(archive_); }
    bool loaded() const { return loaded_; }

private:
    VfsContext vfs_;
    DanArchive archive_;
    ModelGraph bind_model_;
    ModelGraph pose_model_;
    AnimationClip active_clip_;
    std::array<EntityAnimSlot,64> slots_{};
    std::array<int32_t,3> spawn_xyz_{};
    int32_t spawn_heading_ = 0;
    int active_action_ = -1;
    int pending_action_ = -1;
    float frame_ = 1.0f;
    std::string asset_name_;
    bool loaded_ = false;
    bool evaluate(float frame, std::string& error);

    friend void ANIM_InitStateTable(PlayerState&);
    friend bool ENT_LoadObject(PlayerState&, const uint8_t*,
                               std::string_view, std::string&);
    friend bool ENT_MoveToPlayerSpawn(PlayerState&, const uint8_t*,
                                      std::string&);
    friend bool ANIM_LoadEntitySet(PlayerState&, std::string&);
    friend bool ANIM_RequestState(PlayerState&, int, std::string&);
    friend bool ANIM_ApplyPendingState(PlayerState&, std::string&);
    friend bool ANIM_TickPlayerIdle(PlayerState&, double, std::string&);
    friend bool PLAYER_ComposeRenderGraph(const PlayerState&, const ModelGraph&,
                                          ModelGraph&, size_t&, std::string&);
    friend bool PLAYER_UpdateRenderGraph(const PlayerState&, ModelGraph&,
                                         size_t, std::string&);
};

void ANIM_InitStateTable(PlayerState& player);
bool ENT_LoadObject(PlayerState& player, const uint8_t* project_record,
                    std::string_view logical_name, std::string& error);
bool ENT_MoveToPlayerSpawn(PlayerState& player, const uint8_t* project_record,
                           std::string& error);
bool ANIM_LoadEntitySet(PlayerState& player, std::string& error);
bool ANIM_RequestState(PlayerState& player, int action, std::string& error);
bool ANIM_ApplyPendingState(PlayerState& player, std::string& error);
bool ANIM_TickPlayerIdle(PlayerState& player, double elapsed_seconds,
                         std::string& error);

// Host-side assembly of the ported actor graph and the already loaded scene.
bool PLAYER_ComposeRenderGraph(const PlayerState& player, const ModelGraph& scene,
                               ModelGraph& combined, size_t& node_base,
                               std::string& error);
bool PLAYER_UpdateRenderGraph(const PlayerState& player, ModelGraph& combined,
                              size_t node_base, std::string& error);

} // namespace od::port

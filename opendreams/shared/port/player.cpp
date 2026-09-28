#include "port/player.h"

#include "port/resource.h"
#include "port/scene.h"

#include <algorithm>
#include <cmath>

namespace od::port {
namespace {

bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}

int32_t le32(const uint8_t* bytes) {
    const uint32_t value = static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
    return static_cast<int32_t>(value);
}

int action_number(std::string_view name) {
    const size_t marker = name.rfind("AN");
    if (marker == std::string_view::npos || marker + 5 > name.size()) return -1;
    int value = 0;
    for (size_t i = marker + 2; i < marker + 5; ++i) {
        if (name[i] < '0' || name[i] > '9') return -1;
        value = value * 10 + (name[i] - '0');
    }
    return value < 64 ? value : -1;
}

} // namespace

PlayerState::PlayerState(std::shared_ptr<const disc::Image> image)
    : vfs_(std::move(image)), archive_(vfs_) {}

bool PlayerState::evaluate(float frame, std::string& error) {
    return active_clip_.resource_type == 4 ?
        ANIM_ApplyModelLinear(active_clip_, frame, bind_model_, pose_model_,
                              false, error) :
        ANIM_ApplyModelSpline(active_clip_, frame, bind_model_, pose_model_,
                              false, error);
}

void ANIM_InitStateTable(PlayerState& player) {
    player.slots_ = {};
    for (int action = 0; action < 64; ++action) {
        uint8_t flags = 0;
        if ((action >= 2 && action <= 8) ||
            (action >= 44 && action <= 45) ||
            (action >= 60 && action <= 61) ||
            action == 25 || action == 26) flags |= 2;
        if ((action >= 16 && action <= 23) ||
            action == 9 || action == 10 || action == 11 || action == 12 ||
            action == 13 || action == 14 || action == 24 || action == 28 ||
            (action >= 30 && action <= 33) || action == 35 ||
            action == 39 || action == 40 || action == 46 ||
            action == 47 || action == 48 ||
            (action >= 50 && action <= 59) || action == 62 || action == 63)
            flags |= 4;
        if (action == 9 || action == 10 || action == 11 ||
            action == 20 || action == 22 || action == 23 ||
            action == 39 || action == 46) flags |= 0x20;
        if (action == 50 || action == 51) flags |= 8;
        player.slots_[static_cast<size_t>(action)].flags = flags;
    }
    player.active_action_ = -1;
    player.pending_action_ = -1;
    player.active_clip_ = {};
}

bool ENT_MoveToPlayerSpawn(PlayerState& player, const uint8_t* project_record,
                           std::string& error) {
    error.clear();
    if (!project_record) return fail(error, "player spawn needs a project record");
    for (size_t axis = 0; axis < 3; ++axis)
        player.spawn_xyz_[axis] = le32(project_record + 0xb4 + axis * 4);
    player.spawn_heading_ = le32(project_record + 0x10c);
    return true;
}

bool ENT_LoadObject(PlayerState& player, const uint8_t* project_record,
                    std::string_view logical_name, std::string& error) {
    MaterialCache cache;
    return ENT_LoadObject(player, project_record, logical_name, cache, error);
}

bool ENT_LoadObject(PlayerState& player, const uint8_t* project_record,
                    std::string_view logical_name, MaterialCache& cache,
                    std::string& error) {
    error.clear();
    player.loaded_ = false;
    player.bind_model_ = {};
    player.pose_model_ = {};
    if (!project_record || logical_name.size() < 5 ||
        logical_name.substr(logical_name.size() - 4) != ".3DC")
        return fail(error, "player resource must be a logical .3DC name");
    player.asset_name_ = std::string(logical_name);
    const std::string stem(logical_name.substr(0, logical_name.size() - 4));
    DanError dan_error;
    if (!DAN_OpenArchive(player.archive_, "DATA/3DC/" + stem + ".DAN",
                         dan_error)) {
        error = dan_error.message;
        return false;
    }
    if (!RES_Load(player.archive_, logical_name, cache, player.bind_model_, error))
        return false;
    if (player.bind_model_.nodes.empty())
        return fail(error, "player model has no root node");
    player.bind_model_.nodes[0].external_parent_handle = 0;
    player.pose_model_ = player.bind_model_;
    if (!ENT_MoveToPlayerSpawn(player, project_record, error)) return false;
    player.loaded_ = true;
    return true;
}

bool ANIM_LoadEntitySet(PlayerState& player, std::string& error) {
    error.clear();
    if (!player.loaded_) return fail(error, "player model is not loaded");
    DanError source;
    if (!DAN_ReadAnimChunks(player.archive_, source)) {
        error = source.message;
        return false;
    }
    const size_t count = DAN_GetAnimCount(player.archive_);
    for (size_t index = 0; index < count; ++index) {
        const int action = action_number(DAN_GetAnimName(player.archive_, index));
        if (action >= 0)
            player.slots_[static_cast<size_t>(action)].archive_index =
                static_cast<int>(index);
    }
    int previous = -1;
    for (auto& slot : player.slots_) {
        if (slot.archive_index >= 0) previous = slot.archive_index;
        else slot.archive_index = previous;
    }
    if (previous < 0) return fail(error, "player archive contains no action clips");
    player.slots_[0].flags |= 1; // Retail marks the claimed family occupied.
    return true;
}

bool ANIM_RequestState(PlayerState& player, int action, std::string& error) {
    error.clear();
    if (action < 0 || action >= 64 ||
        player.slots_[static_cast<size_t>(action)].archive_index < 0)
        return fail(error, "player action has no resolved clip");
    player.pending_action_ = action;
    return true;
}

bool ANIM_ApplyPendingState(PlayerState& player, std::string& error) {
    error.clear();
    if (player.pending_action_ < 0) return true;
    const int index = player.slots_[static_cast<size_t>(player.pending_action_)].archive_index;
    if (index < 0 || static_cast<size_t>(index) >= DAN_GetAnimCount(player.archive_))
        return fail(error, "player pending clip index is invalid");
    const std::string name(DAN_GetAnimName(player.archive_,
                                          static_cast<size_t>(index)));
    if (player.active_clip_.name != name) {
        std::vector<uint8_t> bytes;
        if (!RES_ReadFile(player.archive_, name, bytes, error) ||
            !ANIM_DecodeClip(bytes, name, player.active_clip_, error)) return false;
    }
    player.active_action_ = player.pending_action_;
    player.pending_action_ = -1;
    player.frame_ = player.active_clip_.duration ? 1.0f : 0.0f;
    return player.evaluate(player.frame_, error);
}

bool ANIM_TickPlayerIdle(PlayerState& player, double elapsed_seconds,
                         std::string& error) {
    error.clear();
    if (player.active_action_ != 0 || player.active_clip_.duration <= 1)
        return true;
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0)
        return fail(error, "player frame interval is invalid");
    player.frame_ += static_cast<float>(std::min(elapsed_seconds, 0.25) * 30.0);
    const float span = static_cast<float>(player.active_clip_.duration) - 1.0f;
    if (player.frame_ >= player.active_clip_.duration)
        player.frame_ = 1.0f + std::fmod(player.frame_ - 1.0f, span);
    return player.evaluate(player.frame_, error);
}

bool PLAYER_ComposeRenderGraph(const PlayerState& player, const ModelGraph& scene,
                               ModelGraph& combined, size_t& node_base,
                               std::string& error) {
    error.clear();
    if (!player.loaded_ || player.pose_model_.nodes.empty())
        return fail(error, "player pose is not loaded");
    combined = scene;
    ModelNode transform;
    transform.name = "Player world transform";
    if (!MATH_EulerToMat3(0, player.spawn_heading_, 0,
                          transform.local_rot, error)) return false;
    // ENT_MoveToPlayerSpawn writes the actor's model root to the level spawn.
    // The authored root already has a local offset (XH_ bassin Y=-177), so
    // applying the spawn as an additional parent translation would lift the
    // whole actor by that offset once its child links are resolved.
    Vec3 rotated_root{};
    MATH_MulMat3Vec3(transform.local_rot,
                     player.pose_model_.nodes[0].local_xyz, rotated_root);
    for (size_t axis = 0; axis < 3; ++axis)
        transform.local_xyz[axis] = static_cast<int32_t>(
            static_cast<uint32_t>(player.spawn_xyz_[axis]) -
            static_cast<uint32_t>(rotated_root[axis]));
    const size_t transform_index = combined.nodes.size();
    combined.nodes.push_back(std::move(transform));
    node_base = combined.nodes.size();
    for (auto node : player.pose_model_.nodes) {
        node.parent = node.parent < 0 ? static_cast<int>(transform_index) :
                      node.parent + static_cast<int>(node_base);
        combined.nodes.push_back(std::move(node));
    }
    // Faces sharing a cache entry with the scene keep sharing one bank.
    const auto mapping = merge_graph_materials(combined.materials,
                                               player.pose_model_.materials);
    const auto append = [&](const std::vector<ModelFace>& from,
                            std::vector<ModelFace>& to) {
        for (auto face : from) {
            face.owner_node += node_base;
            if (face.material_index < mapping.size())
                face.material_index = mapping[face.material_index];
            for (auto& corner : face.corners) corner.node += node_base;
            to.push_back(std::move(face));
        }
    };
    append(player.pose_model_.faces, combined.faces);
    append(player.pose_model_.flat_faces, combined.flat_faces);
    return true;
}

bool PLAYER_UpdateRenderGraph(const PlayerState& player, ModelGraph& combined,
                              size_t node_base, std::string& error) {
    error.clear();
    if (node_base > combined.nodes.size() ||
        player.pose_model_.nodes.size() > combined.nodes.size() - node_base)
        return fail(error, "player render graph has no pose node range");
    for (size_t index = 0; index < player.pose_model_.nodes.size(); ++index) {
        combined.nodes[node_base + index].local_rot =
            player.pose_model_.nodes[index].local_rot;
        combined.nodes[node_base + index].local_xyz =
            player.pose_model_.nodes[index].local_xyz;
    }
    return true;
}

} // namespace od::port

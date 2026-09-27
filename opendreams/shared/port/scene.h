#pragma once

#include "port/ddat.h"
#include "port/model.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace od::port {

struct PreviewActor {
    size_t object_slot = SIZE_MAX;
    std::string asset_name;
    uint16_t flags = 0;
    std::array<int32_t, 3> spawn_xyz{};
    uint32_t spawn_heading = 0;
    ModelGraph model;
    bool attached_to_camera_root = false;
};

// Preview-owned adaptation of the globals read by SCENE_LoadLevel. One source
// and one loaded level are active at a time; the original binary is never run.
class PreviewLevelContext {
public:
    explicit PreviewLevelContext(std::shared_ptr<const disc::Image> image);
    bool select_project(std::string_view name, size_t object_slot,
                        std::string& error);
    bool select_model(std::string_view physical_path, std::string_view model_name,
                      std::string& error);
    const ModelGraph& level_graph() const { return level_; }
    const PreviewActor& selected_actor() const { return actor_; }
    bool loaded() const { return loaded_; }
    std::string_view project_name() const { return project_name_; }
    const std::array<uint8_t, DdatBank::record_size>& project_record() const {
        return project_record_;
    }

private:
    VfsContext vfs_;
    DdatBank bank_;
    std::array<uint8_t, DdatBank::record_size> project_record_{};
    std::string project_name_;
    std::string model_source_path_;
    std::string selected_model_name_;
    size_t selected_slot_ = SIZE_MAX;
    ModelGraph level_;
    PreviewActor actor_;
    bool pending_load_ = false;
    bool loaded_ = false;
    bool synthetic_scene_ = false;

    friend bool SCENE_InitLevel(PreviewLevelContext&, std::string&);
    friend bool SCENE_LoadLevel(PreviewLevelContext&, std::string&);
    friend bool ENT_InstantiateFromObjet(PreviewLevelContext&, size_t, std::string&);
    friend bool ENT_LoadModel(PreviewLevelContext&, PreviewActor&, std::string&);
    friend bool ENT_ResetToSpawn(PreviewLevelContext&, PreviewActor&, std::string&);
};

bool SCENE_InitLevel(PreviewLevelContext& context, std::string& error);
bool SCENE_LoadLevel(PreviewLevelContext& context, std::string& error);
bool ENT_InstantiateFromObjet(PreviewLevelContext& context, size_t slot,
                              std::string& error);
bool ENT_LoadModel(PreviewLevelContext& context, PreviewActor& actor,
                   std::string& error);
bool ENT_ResetToSpawn(PreviewLevelContext& context, PreviewActor& actor,
                      std::string& error);
bool MDL_AttachNode(PreviewActor& actor, std::string& error);
bool MATH_EulerToMat3(int x, int y, int z,
                      std::array<int32_t, 9>& matrix, std::string& error);
bool MDL_SetNodeRotation(ModelGraph& graph, size_t node,
                         const std::array<int32_t, 9>& rotation,
                         std::string& error);
bool MDL_SetNodePosition(ModelGraph& graph, size_t node,
                         const std::array<int32_t, 3>& position,
                         std::string& error);

} // namespace od::port

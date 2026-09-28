#pragma once

#include "port/ddat.h"
#include "port/model.h"
#include "port/resource.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

class PlayerState;

struct PreviewActor {
    size_t object_slot = SIZE_MAX;
    std::string asset_name;
    uint16_t flags = 0;
    std::array<int32_t, 3> spawn_xyz{};
    uint32_t spawn_heading = 0;
    ModelGraph model;
    bool attached_to_camera_root = false;
    bool from_secondary_source = false;
};

struct PreviewObjectIssue {
    size_t slot = SIZE_MAX;
    std::string asset_name;
    std::string reason;
};

// Preview-owned adaptation of the globals read by SCENE_LoadLevel. One source
// and one loaded level are active at a time; the original binary is never run.
class PreviewLevelContext {
public:
    explicit PreviewLevelContext(std::shared_ptr<const disc::Image> image,
        std::shared_ptr<const disc::Image> secondary = {});
    bool select_project(std::string_view name, size_t object_slot,
                        std::string& error);
    bool select_project_scene(std::string_view name, std::string& error);
    bool select_scene(std::string_view physical_path, std::string& error);
    bool select_model(std::string_view physical_path, std::string_view model_name,
                      std::string& error);
    const ModelGraph& level_graph() const { return level_; }
    const ModelGraph& render_graph() const { return render_; }
    const std::vector<PreviewActor>& placed_actors() const { return actors_; }
    const std::vector<PreviewObjectIssue>& object_issues() const { return issues_; }
    // Faces left without a drawable page (retail misses and zero-size loads).
    const std::vector<std::string>& unbound_materials() const { return missing_materials_; }
    const std::vector<std::string>& missing_scene_materials() const { return missing_materials_; }
    // The level-scoped material cache, in retail slot order.
    const MaterialCache& material_cache() const { return cache_; }
    // DSN maps DSN_Create3DM never instantiated ("map %s not used").
    const std::vector<std::string>& unused_scene_maps() const { return unused_maps_; }
    // Retail load-order steps the port could not perform (documented gaps).
    const std::vector<std::string>& load_order_gaps() const { return load_gaps_; }
    const PreviewActor& selected_actor() const { return actor_; }
    bool loaded() const { return loaded_; }
    std::string_view project_name() const { return project_name_; }
    const std::array<uint8_t, DdatBank::record_size>& project_record() const {
        return project_record_;
    }
    void set_runtime_player(PlayerState* player) { runtime_player_ = player; }
    // The disc that supplied the scene DSN; its datanim files share it.
    const std::shared_ptr<const disc::Image>& scene_image() const { return scene_image_; }

private:
    std::shared_ptr<const disc::Image> scene_image_;
    VfsContext vfs_;
    std::unique_ptr<VfsContext> secondary_vfs_;
    DdatBank bank_;
    std::array<uint8_t, DdatBank::record_size> project_record_{};
    std::string project_name_;
    std::string model_source_path_;
    std::string scene_source_path_;
    std::string selected_model_name_;
    size_t selected_slot_ = SIZE_MAX;
    ModelGraph level_;
    ModelGraph render_;
    PreviewActor actor_;
    std::vector<PreviewActor> actors_;
    std::vector<PreviewObjectIssue> issues_;
    std::vector<std::string> missing_materials_;
    MaterialCache cache_;
    std::vector<std::string> unused_maps_;
    std::vector<std::string> load_gaps_;
    std::array<ModelGraph, 2> shadow_models_; // ombre.3dc, ombre2.3dc
    std::array<ModelGraph, 2> shot_models_;   // gr00.3dc, boule.3dc
    PreviewActor player_seed_;                // preview-only player cache seed
    bool pending_load_ = false;
    bool loaded_ = false;
    bool synthetic_scene_ = false;
    bool project_scene_ = false;
    PlayerState* runtime_player_ = nullptr; // Caller owns this optional actor.

    friend bool SCENE_InitLevel(PreviewLevelContext&, std::string&);
    friend bool SCENE_LoadLevel(PreviewLevelContext&, std::string&);
    friend bool ENT_InstantiateFromObjet(PreviewLevelContext&, size_t, std::string&);
    friend bool ENT_LoadModel(PreviewLevelContext&, PreviewActor&, std::string&);
    friend bool ENT_ResetToSpawn(PreviewLevelContext&, PreviewActor&, std::string&);
    friend bool ENT_InitShadows(PreviewLevelContext&, std::string&);
    friend bool ENT_LoadShotModels(PreviewLevelContext&, std::string&);
    friend bool load_scene_object(PreviewLevelContext&, const std::string&, std::string&);
    friend bool load_level_resource(PreviewLevelContext&, std::string_view,
                                    ModelGraph&, std::string&);
};

// Project modes follow the retail level load order, which decides the
// first-wins material cache: SCENE_InitLevel (RES_InitArena, ENT_InitShadows,
// the player, ENT_LoadShotModels), then SCENE_LoadLevel's active OBJET1..15
// and OBJET0's DSN last. Standalone DSN and DAN previews seed the cache only
// from their own file (viewer adaptation).
bool SCENE_InitLevel(PreviewLevelContext& context, std::string& error);
// ENT_InitShadows (0x43e55c), cache part: RES_Load of data/3dc/ombre.3dc and
// ombre2.3dc. Shadow duplicates, node attachment and ENT_ResetShadows are not
// ported.
bool ENT_InitShadows(PreviewLevelContext& context, std::string& error);
// ENT_LoadShotModels (0x42c414), cache part: RES_Load of gr00.3dc and
// boule.3dc. The duplicated, hidden shot instances are not ported.
bool ENT_LoadShotModels(PreviewLevelContext& context, std::string& error);
// Host: OBJET0's DSN in DSN mode (DAT_0049da5c = 1): geometry, texture pages
// and MDL_LoadMaterials through DSN_Create3DM.
bool load_scene_object(PreviewLevelContext& context, const std::string& path,
                       std::string& error);
// Host: RES_Load of a loose level resource on the scene disc, else the
// secondary mounted disc.
bool load_level_resource(PreviewLevelContext& context, std::string_view path,
                         ModelGraph& graph, std::string& error);
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

#pragma once

#include "render/direct.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace wd {

// Reader is supplied by the runtime (guest arena) or an independent dump.
// It must copy exact bytes, reject unmapped ranges, and never mutate the guest.
struct SceneReader {
    void *context = nullptr;
    bool (*read)(void *, uint32_t address, void *destination, size_t bytes) = nullptr;
    od_render_id (*gpu_image)(void *, uint32_t address, uint64_t *version) = nullptr;
    // The DOS 3dfx build's view, supplied by the live adapter only. dos_palette
    // fills the 32 rows below a page when the host generates them (true), and
    // dos_lights drops the bound light indices that build would not have,
    // returning the new count.
    bool (*dos_palette)(void *, uint32_t page, uint16_t *bank) = nullptr;
    // The rows of faces lit by an attack light, indexed by shade (false: none
    // for this page). Asked only for materials of nodes with bound lights.
    bool (*lit_palette)(void *, uint32_t page, uint16_t *bank) = nullptr;
    uint32_t (*dos_lights)(void *, uint32_t node, uint8_t *indices, uint32_t count) = nullptr;
};
struct SceneNormal {
    uint32_t address = 0;
    std::array<int32_t, 3> xyz{};
    int32_t dot = 0; // retained scratch when a corner lies outside the refreshed owner pool
};
struct SceneNode {
    // Old transformed/cull classification bits are removed from flags.
    uint32_t address = 0, flags = 0, first_vertex = 0, vertex_count = 0;
    bool submitted = false;
    uint32_t shade = 15, light_count = 0;
    std::array<uint8_t, 8> light_indices{};
    std::vector<SceneNormal> vertex_normals; // node +0x8c/+0x90; refreshed per Gouraud block/light
    bool visual_active = false; // hidden/flag-4 exclusion; hook-disabled nodes can still update UVs
    std::array<int32_t, 9> source_rotation{32768, 0, 0, 0, 32768, 0, 0, 0, 32768};
    std::array<int32_t, 3> source_position{};
    uint32_t face_normal_base = 0, face_normal_count = 0;
};
struct SceneLight {
    uint32_t type = 0;
    std::array<int32_t, 3> position{};
    std::array<int32_t, 9> orientation{};
    int32_t inner_radius = 0, outer_radius = 0, intensity = 0;
    // Record +0x34/+0x40: retail's retained view-space transform. Only a live
    // slot at or above light_transform_count is lit from it (stale by then).
    std::array<int32_t, 3> view_position{};
    std::array<int32_t, 9> view_orientation{};
};
struct SceneFace {
    // Only stable bit 8 (recompute normal) survives in flags, never old cull/clip bits.
    // capture_scene sets bit 1 on a face with a corner at or beyond the far plane.
    uint32_t address = 0, owner = 0, flags = 0, material_slot = 0, colour = 0;
    int32_t type = 0;
    uint8_t shade = 0;
    std::array<uint8_t, 3> corner_shades{}; // face +0x41..+0x43, used by 0x16..0x18
    std::array<SceneNormal, 3> corner_normals{}; // face +0x0c/+0x18/+0x24
    std::array<uint32_t, 3> uv_addresses{};
    std::array<std::array<int32_t, 2>, 3> source_uvs{};
    std::array<od_scene_corner, 3> corners{};
    std::array<int32_t, 3> normal{};
    uint32_t normal_address = 0;
    int32_t plane_distance = 0;
    int32_t source_normal_dot = 0;
    uint32_t block = 0, material = UINT32_MAX;
};
struct SceneMaterial {
    uint32_t slot = 0, page = 0;
    // Source bank: 256x256; the material-cache Glide descriptor uploads
    // even texels as a single 128x128 LOD (MDL_LoadMaterials).
    std::vector<uint8_t> indices;
    std::array<uint16_t, 32 * 256> palette{}; // physical row order (page - 0x8000 upward)
    // Empty, or 32 rows indexed by face shade for the faces of a lit node
    // (od_lit_palette_rows; live capture, WDSD). Row 0 is the unlit row.
    std::vector<uint16_t> lit_palette;
    od_render_id gpu_mask = 0;
    uint64_t gpu_version = 0;
};
struct SceneCamera {
    uint32_t address = 0;
    std::array<int32_t, 3> eye{};
    std::array<int32_t, 9> local_rotation{};
    std::array<float, 12> view{}; // camera inverse, derived from local fields
    uint32_t screen_width = 0, screen_height = 0;
    uint32_t viewport_x = 0, viewport_y = 0, viewport_width = 0, viewport_height = 0;
    float focal_x = 0, focal_y = 0;
    int32_t center_x = 0, center_y = 0, near_plane = 0, far_plane = 0;
};
struct SceneSnapshot {
    SceneCamera camera;
    od_scene_fog fog{};
    std::vector<SceneNode> nodes;
    std::vector<od_pose_node> poses;
    std::vector<od_scene_vertex> vertices;
    std::vector<std::array<int32_t, 3>>
        source_vertices; // exact integers for lighting, not old projections
    std::array<SceneLight, 100> lights{};
    uint32_t light_transform_count = 0; // prefix refreshed by retail's retained view transform
    bool light_views = false; // lights carry view_position/view_orientation (capture, WDSB)
    // Material banks hold the DOS 3dfx build's rows and the node light lists its
    // bindings (live capture, WDSC): a block binds physical row = shade, as
    // GLIDE_BindTexture does. Otherwise they are the Windows build's, whose
    // row for shade s is 31 - s.
    bool dos_palette = false;
    std::vector<uint32_t> vertex_addresses;
    std::vector<SceneFace> faces;
    std::vector<SceneMaterial> materials;
    size_t cross_node_faces = 0;

    // Main display: Hor+ relative to the original screen aspect. Offscreen
    // contracts: preserve independent retail X/Y focal terms instead.
    bool view_projection(int width, int height, bool hor_plus, float output[16]) const;
};

// root is the resolved render-root pointer, NOT a model handle or source
// encoded pointer. Source relocation (including parent 1) remains lifted.
// Captures original records, ignoring generated visibility/projection output.
bool capture_scene(SceneReader reader, uint32_t root, SceneSnapshot &out, std::string &error);
bool write_scene(const SceneSnapshot &, const char *path, std::string &error);
bool read_scene(const char *path, SceneSnapshot &, std::string &error);

} // namespace wd

extern "C" int wd_capture_scene_file(bool (*reader)(void *, uint32_t, void *, size_t),
                                     void *context, uint32_t root, const char *path, char *error,
                                     size_t error_size);

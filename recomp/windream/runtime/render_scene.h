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
};
struct SceneNode {
    // Old transformed/cull classification bits are removed from flags.
    uint32_t address = 0, flags = 0, first_vertex = 0, vertex_count = 0;
    bool submitted = false;
    uint32_t shade = 15, light_count = 0;
    std::array<uint8_t, 8> light_indices{};
};
struct SceneLight {
    uint32_t type = 0;
    std::array<int32_t, 3> position{};
    std::array<int32_t, 9> orientation{};
    int32_t inner_radius = 0, outer_radius = 0, intensity = 0;
};
struct SceneFace {
    // Only stable bit 8 (recompute normal) survives in flags, never old cull/clip bits.
    uint32_t address = 0, owner = 0, flags = 0, material_slot = 0, colour = 0;
    int32_t type = 0;
    uint8_t shade = 0;
    std::array<od_scene_corner, 3> corners{};
    std::array<int32_t, 3> normal{};
    uint32_t normal_address = 0;
    int32_t plane_distance = 0;
    uint32_t block = 0, material = UINT32_MAX;
};
struct SceneMaterial {
    uint32_t slot = 0, page = 0;
    // Source bank: 256x256; the material-cache Glide descriptor uploads
    // even texels as a single 128x128 LOD (MDL_LoadMaterials).
    std::vector<uint8_t> indices;
    std::array<uint16_t, 32 * 256> palette{}; // physical WINDREAM row order
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

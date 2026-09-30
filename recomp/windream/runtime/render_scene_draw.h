#pragma once
#include "render_scene.h"

namespace wd {
struct SceneLightingWrite {
    uint32_t address, value, bytes, entry;
};
struct SceneLighting {
    std::vector<uint8_t> shades;
    std::vector<std::array<uint8_t, 3>> corner_shades;
    std::vector<std::array<od_scene_corner, 3>> corners; // current UV version at each object's draw
    std::vector<SceneLightingWrite> writes;
};
bool prepare_scene_lighting(const SceneSnapshot &, const float view_projection[16], SceneLighting &,
                           std::string &error, bool require_metadata = false);
bool compose_feedback_rotations(const SceneSnapshot &, std::vector<int32_t> &);
struct ShadowInput {
    std::vector<od_shadow_node> nodes;
    std::vector<od_shadow_vertex> vertices;
    std::vector<od_shadow_triangle> triangles;
    od_shadow_packet packet{};
};
bool prepare_shadow_input(const SceneSnapshot &, od_render_id, ShadowInput &, std::string &);

// Host adapter owns the Windows palette-row convention and material bindings.
// GPU handles are immutable content versions and never contain guest pointers.
class SceneDraw {
  public:
    static bool submit_shadow(od_renderer *, const SceneSnapshot &, od_render_id, std::string &);
    bool submit(od_renderer *, const SceneSnapshot &, od_render_id target, int drawable_width,
                int drawable_height, bool hor_plus, std::string &error,
                std::vector<SceneLightingWrite> *lighting_writes = nullptr);
    // After all frame submissions, before od_renderer_frame_complete().
    void finish_frame(od_renderer *);
    void reset(od_renderer *); // level/resource lifetime boundary
  private:
    struct TextureVersion {
        uint64_t hash = 0, last_frame = 0;
        std::vector<uint32_t> rgba;
        std::vector<uint8_t> indices;
        std::array<uint16_t, 256> palette{};
        od_render_id texture = 0;
        od_render_id mask = 0;
        uint64_t mask_version = 0;
    };
    std::vector<TextureVersion> textures_;
    uint64_t frame_ = 1;
    uint32_t palette_page_ = 0;
    std::array<uint16_t, 256> palette_{};
    od_render_id texture(od_renderer *, const SceneMaterial &, std::string &);
};
} // namespace wd

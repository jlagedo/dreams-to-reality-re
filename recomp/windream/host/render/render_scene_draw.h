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
struct SceneCollectorTriangle {
    std::array<std::array<int32_t, 2>, 3> points{};
    std::array<uint8_t, 3> clipped{};
    uint8_t face_flags = 0; // Original face bit 8, normalized to 0/1.
};
struct SceneCollector {
    std::vector<SceneCollectorTriangle> triangles;
    size_t total_triangles = 0; // Includes triangles omitted by the output capacity.
};
// Diagnostic-only projection of source geometry; no GPU work or retail scratch
// input. Modern frustum clipping may split faces differently from retail.
bool prepare_scene_collector(const SceneSnapshot &, int width, int height, bool hor_plus,
                              SceneCollector &, std::string &, size_t capacity = 682);
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
    // A prepared result belongs to this immutable snapshot and viewport. It
    // lets callers publish metadata before callbacks while drawing afterward.
    // Submission never recomputes or mutates supplied preparation.
    bool submit(od_renderer *, const SceneSnapshot &, od_render_id target, int drawable_width,
                int drawable_height, bool hor_plus, std::string &error,
                std::vector<SceneLightingWrite> *lighting_writes = nullptr,
                const SceneLighting *prepared_lighting = nullptr,
                float source_edge_quantum = 0.0f); // experimental replay only; disabled in live game
    // After all frame submissions, before od_renderer_frame_complete().
    void finish_frame(od_renderer *);
    void reset(od_renderer *); // level/resource lifetime boundary
    // Entries the last submit would have put in Glide's 256-entry deferred list.
    size_t deferred_blocks() const { return deferred_blocks_; }
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
    size_t deferred_blocks_ = 0;
    bool deferred_overflow_logged_ = false;
    uint64_t no_draw_logged_ = 0; // bit (type + 15): one log line per type
    uint64_t lit_submits_ = 0;    // submits that drew faces with the host's lit rows
    od_render_id texture(od_renderer *, const SceneMaterial &, const std::array<uint16_t, 256> &,
                         std::string &);
};
} // namespace wd

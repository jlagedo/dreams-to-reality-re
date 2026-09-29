#pragma once

#include "port/fog.h"
#include "port/scene.h"
#include "port/glide_model.h"
#include "render/direct.h"

#include <sokol_gfx.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace od {

struct ModelView {
    float yaw = 0.78539816f;
    float pitch = 0.34906585f;
    float distance = 3.5f;
    float target[3]{};
    bool explicit_eye = false;
    float eye[3]{};
    float focal_x = 2.2f;
    float focal_y = 2.2f;
    float near_plane = 0.1f;
    float far_plane = 100.0f;
};

// Hor+ relative to the retail 4:3 horizontal focal length.
inline ModelView with_horizontal_focal(ModelView view, float focal_x, int width, int height,
                                       float reference_aspect = 4.0f / 3.0f) {
    if (width > 0 && height > 0) {
        view.focal_y = focal_x * reference_aspect;
        view.focal_x = view.focal_y * static_cast<float>(height) / static_cast<float>(width);
    }
    return view;
}

// Viewer orbit framing keeps the chosen focal length on the shorter side, so
// a model stays inside a wide or tall preview pane.
inline ModelView fit_to_target(ModelView view, int width, int height) {
    if (width > 0 && height > 0) {
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        if (aspect >= 1.0f)
            view.focal_x = view.focal_y / aspect;
        else
            view.focal_y = view.focal_x * aspect;
    }
    return view;
}

// ModelGraph adapter for ODRender. Original vertices/local poses are submitted
// through the shared scene API; palette/page snapshots become immutable GPU
// versions. Shell owns the only sokol context, commit and window presentation.
class ModelPreview {
  public:
    ModelPreview() = default;
    ModelPreview(const ModelPreview &) = delete;
    ModelPreview &operator=(const ModelPreview &) = delete;
    bool init(std::string &error, int width = 512, int height = 512);
    bool load(const port::PreviewActor &actor, std::string &error);
    bool load(const port::ModelGraph &graph, std::string &error);
    bool update_pose(const port::ModelGraph &graph, std::string &error);
    // Recreates the destination attachments when the size changes.
    bool resize_target(int width, int height, std::string &error);
    // Stage palette rows (bit r = row r) of one material from its bank.
    void update_palette_rows(size_t material, uint32_t rows, const std::vector<uint8_t> &bank);
    // Stage the 256x256 page of one material (an animated material).
    void update_material_pixels(size_t material, const std::vector<uint8_t> &bank);
    // DREAMSFX table fog (SCENE_SetFog / GLIDE_SetFog state) and the
    // scan-out gamma of grGammaCorrectionValue (out = in^(1/gamma)).
    void set_fog(const port::GlideFogState &fog) { fog_ = fog; }
    void set_output_gamma(float glide_gamma) {
        output_gamma_ = glide_gamma > 0.0f ? glide_gamma : 1.0f;
    }
    // Every call snapshots uploads; source and batch changes within a sokol
    // frame are supported. No commit or present occurs here.
    bool draw(const ModelView &view, std::string &error);
    bool project_joint(const std::array<int32_t, 3> &world_xyz, const ModelView &view, float &u,
                       float &v) const;
    std::array<float, 3> world_to_view(const std::array<int32_t, 3> &xyz) const;
    float scaled_distance(float source_units) const { return source_units * frame_scale_; }
    void clear_model();
    void shutdown();
    bool has_model() const { return loaded_; }
    sg_view texture_view() const;
    od_render_stats renderer_stats() const;
    int target_width() const { return target_width_; }
    int target_height() const { return target_height_; }

  private:
    bool create_target(int width, int height, std::string &error);
    void destroy_target();
    static void committed(void *);
    od_render_id texture(size_t material, uint32_t row, std::string &error);
    bool stage_graph(const port::ModelGraph &, bool fit, std::string &error);
    int target_width_ = 0;
    int target_height_ = 0;
    od_renderer *renderer_ = nullptr;
    od_render_id scene_target_ = 0, output_target_ = 0;
    bool listening_ = false;
    bool loaded_ = false;
    port::ModelGraph graph_;
    std::vector<od_pose_node> poses_;
    std::vector<od_scene_vertex> vertices_;
    std::vector<uint32_t> vertex_bases_;
    struct GpuMaterial {
        unsigned lod = 0;
        uint64_t version = 0, identity = 0;
        std::vector<uint8_t> bank;
    };
    std::vector<GpuMaterial> materials_;
    struct TextureVersion {
        size_t material = 0;
        uint64_t version = 0, last_frame = 0;
        std::array<uint16_t, 256> palette{};
        od_render_id texture = 0;
    };
    std::vector<TextureVersion> textures_;
    bool palette_bound_ = false;
    uint64_t palette_identity_ = 0, frame_ = 1, next_version_ = 1;
    std::array<uint16_t, 256> palette_{};
    port::GlideFogState fog_;
    float output_gamma_ = 1.0f;
    float frame_scale_ = 1.0f;
    float frame_center_[3]{};
};

} // namespace od

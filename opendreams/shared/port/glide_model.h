#pragma once

#include "port/model.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace od::port {

enum class GlideFaceMode { clamp, wrap, chroma, translucent };

struct GlideModelVertex {
    float position[3]{};
    float uv[2]{};
};

struct GlideModelBatch {
    uint32_t first = 0;
    uint32_t count = 0;
    size_t material = 0;
    uint32_t palette_row = 0;
    GlideFaceMode mode = GlideFaceMode::clamp;
};

struct GlideModelDraw {
    std::vector<GlideModelVertex> vertices;
    std::vector<GlideModelBatch> batches;
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
};

struct ModelJoint {
    size_t slot = 0;
    int parent = -1;
    std::string name;
    std::array<int32_t,3> world_xyz{};
    bool render_relevant = false;
};

// Inspection view of the same parent-composed Q15 transforms used for faces.
bool GLIDE_ModelJoints(const ModelGraph& graph, std::vector<ModelJoint>& joints,
                       std::string& error);

// The DREAMSFX face hook, adapted to emit GPU draw data instead of calling
// grDrawTriangle. Deferred faces are appended after opaque traversal.
bool GLIDE_DrawObjectFaces(const ModelGraph& graph, GlideModelDraw& draw,
                           std::string& error);

// RGB565 high words from one 0x400-byte palette row; matches the 3dfx color
// expansion (R/B x8, G x4). The zero alpha byte is retained here.
bool GLIDE_ConvertPalette(const std::vector<uint8_t>& bank, unsigned row,
                          std::array<uint8_t, 1024>& rgba, std::string& error);

// Modern 128x128 RGBA upload source for the retail model-material LOD. The
// index-zero alpha marker is used only by chroma-key GPU pipelines.
bool model_texture_lod(const std::vector<uint8_t>& bank, unsigned row,
                       std::vector<uint8_t>& rgba, std::string& error);
bool scene_texture_page(const std::vector<uint8_t>& bank, unsigned row,
                        std::vector<uint8_t>& rgba, std::string& error);

} // namespace od::port

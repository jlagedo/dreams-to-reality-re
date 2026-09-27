#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace od::port {

struct ModelCorner {
    size_t node = 0;
    size_t vertex = 0;
    int32_t u = 0; // Original signed 16.16 texels.
    int32_t v = 0;
};

struct ModelFace {
    size_t owner_node = 0;
    int32_t type = 0;
    std::string material_name;
    std::array<ModelCorner, 3> corners{};
    uint8_t shade = 0;
    size_t material_index = SIZE_MAX;
};

struct ModelNode {
    uint32_t source_offset = 0;
    int parent = -1;
    int external_parent_handle = -1; // Retail handle 0 is the camera root.
    std::array<int32_t, 3> local_xyz{};
    std::array<int32_t, 9> local_rot{}; // Q15, row major.
    uint32_t light_count = 0;
    uint32_t shade = 15;
    std::vector<std::array<int32_t, 3>> vertices;
};

struct ModelMaterial {
    std::string name;
    std::vector<uint8_t> bank; // 0x14 header, 32 palette rows, 256x256 page.
    uint16_t preview_lod = 128; // DAN's Glide descriptor; DSN uses its scene page.
    bool static_palette_row15 = false; // Viewer DSN path before dynamic row updates.
};

struct ModelGraph {
    std::vector<ModelNode> nodes;
    std::vector<ModelFace> faces;
    std::vector<ModelMaterial> materials;
};

// Adapted retail relocation: source 32-bit addresses become checked offsets
// and stable host-side indices instead of pointer writes into a 32-bit arena.
bool RES_RelocOffsetTable(const std::vector<uint8_t>& record,
                          std::vector<uint32_t>& node_offsets, std::string& error);
bool MDL_RelocNodeTree(const std::vector<uint8_t>& record,
                       const std::vector<uint32_t>& node_offsets,
                       ModelGraph& graph, std::string& error);
bool RES_Relocate(const std::vector<uint8_t>& record,
                  ModelGraph& graph, std::string& error);

} // namespace od::port

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

// How MDL_BindFaceMaterials (0x4554e0) resolved a face block's +0x08 word.
enum class FaceBinding : uint8_t {
    unbound, // no live cache entry of that name: retail leaves +0x08 unwritten
    page,    // textured type: &entry+8, the cache slot's texture page
    colour,  // flat type -2/1/4/0x11/0x1b: the entry's colour word (+0x2c)
};

struct ModelFace {
    size_t owner_node = 0;
    uint32_t flags = 0; // Retail bit 8 recomputes facing from posed corners.
    int32_t type = 0;
    std::array<int32_t,3> normal{}; // Original Q15 face normal.
    int32_t plane_distance = 0;
    std::string material_name;
    std::array<ModelCorner, 3> corners{};
    uint8_t shade = 0;
    size_t material_index = SIZE_MAX; // Graph-local copy of the bound page.
    size_t cache_slot = SIZE_MAX;     // Level material-cache entry (pointer identity).
    FaceBinding binding = FaceBinding::unbound;
    uint32_t colour = 0;              // Flat blocks: the bound colour word.
};

// One 44-byte record of the material directory that follows a tag-1 model's
// node table: name[16] | file[16] | colour u32 | 8 trailing bytes.
struct MaterialRecord {
    std::array<uint8_t, 44> raw{};
    std::string name; // NUL-terminated within 16 bytes, compared by strcmp_
    std::string file; // empty for a flat-colour material
    uint32_t colour = 0;
};

struct ModelNode {
    uint32_t source_offset = 0;
    std::string name;
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
    size_t cache_slot = SIZE_MAX; // Level material-cache entry this bank copies.
};

struct ModelGraph {
    std::vector<ModelNode> nodes;
    std::vector<ModelFace> faces;       // 68-byte textured-layout records
    std::vector<ModelFace> flat_faces;  // 56-byte records of flat types
    std::vector<ModelMaterial> materials;
    std::vector<MaterialRecord> directory; // Tag-1 material directory.
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
// Retail block types that store a colour word instead of a page pointer.
constexpr bool flat_block_type(int32_t type) {
    return type == -2 || type == 1 || type == 4 || type == 0x11 || type == 0x1b;
}

} // namespace od::port

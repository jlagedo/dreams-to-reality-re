#pragma once

#include "port/model.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

class DanArchive;
class VfsContext;
struct DsnTexturePage;

// What the page word (+0x08) of a cache entry refers to.
enum class MaterialPage : uint8_t {
    none,      // flat-colour record (empty file): nothing was loaded
    loaded,    // RES_Load returned a bank; `bank` holds the host copy
    zero_size, // DSN_Create3DM/DAN_Load3DM miss: retail loads a zero-size
               // resource and +0x08 points at arena memory the next load
               // overwrites. The host keeps no page for it.
};

// One 0x3c-byte entry of the material cache that follows the resource slot
// table (RES_InitArena 0x4568a4, 256 entries, DAT_004aa5f8).
struct MaterialCacheSlot {
    uint32_t refcount = 0;          // +0x00
    MaterialRecord record;          // +0x0c..+0x37: name, file, colour
    MaterialPage page = MaterialPage::none; // +0x08
    ModelMaterial bank;             // host copy of the resource +0x04 names
    std::string source;             // host diagnostic: the model that loaded it
};

// Level-scoped emulation of the global cache. `count` mirrors DAT_004aa6fc:
// it only grows, and RES_InitArena zeroes the entries without resetting it.
struct MaterialCache {
    static constexpr size_t capacity = 256;
    std::vector<MaterialCacheSlot> slots; // size() == count
};

// The .3DM routes of RES_ReadFile (0x41c666) for one model load, in the
// retail order: DSN mode (DAT_0049da5c), an open DAN, else the raw file.
struct MaterialRoute {
    const std::vector<DsnTexturePage>* dsn_pages = nullptr;
    std::vector<uint8_t>* dsn_created = nullptr; // 0x5df880: map instantiated
    DanArchive* dan = nullptr;
    VfsContext* vfs = nullptr;                   // <root>data\3dc\<file>.3DM
    std::string source;                          // diagnostic model label
};

// Host split of RES_InitArena: the material part of the arena reset (every
// entry zeroed, count retained). Called by SCENE_InitLevel once per level.
void RES_InitArena(MaterialCache& cache);

// The first adapted RES source is an active DAN archive. DSN and loose-file
// routes remain explicit future branches of the same retail entry points.
bool RES_ReadFile(DanArchive& archive, std::string_view logical_name,
                  std::vector<uint8_t>& bytes, std::string& error);
// Retail's no-container physical-file branch: skip two u32 header words and
// return the remaining body. The selected VFS context supplies the source.
bool RES_ReadFile(VfsContext& vfs, std::string_view physical_path,
                  std::vector<uint8_t>& bytes, std::string& error);
// MDL_BindFaceMaterials (0x4554e0): for every face block, the first live
// cache entry whose name strcmp-matches the block name. Textured types take
// the entry's page, flat types its colour word; a miss leaves the block
// unbound. Rebuilds graph.materials from the pages the faces reference.
bool MDL_BindFaceMaterials(const MaterialCache& cache, ModelGraph& graph,
                           std::string& error);
bool MDL_BindTreeMaterials(const MaterialCache& cache, ModelGraph& graph,
                           std::string& error);
// MDL_LoadMaterials (0x456038): walks graph.directory; a live entry of the
// same name gains a reference, otherwise the first free entry takes the
// record and loads <file>.3DM through `route` (nothing for an empty file).
bool MDL_LoadMaterials(MaterialCache& cache, MaterialRoute& route,
                       ModelGraph& graph, std::string& error);
// Standalone wrapper: a fresh cache seeded only by this DAN model.
bool MDL_LoadMaterials(DanArchive& archive, ModelGraph& graph, std::string& error);
bool RES_Load(DanArchive& archive, std::string_view logical_name,
              MaterialCache& cache, ModelGraph& graph, std::string& error);
bool RES_Load(VfsContext& vfs, std::string_view physical_path,
              MaterialCache& cache, ModelGraph& graph, std::string& error);
// Standalone wrappers: a fresh cache seeded only by the loaded file.
bool RES_Load(DanArchive& archive, std::string_view logical_name,
              ModelGraph& graph, std::string& error);
bool RES_Load(VfsContext& vfs, std::string_view physical_path,
              ModelGraph& graph, std::string& error);

// Host graph composition: appends `from` to `into`, reusing a bank already
// present for the same cache slot (retail faces share &entry+8). Returns the
// new index of every `from` material.
std::vector<size_t> merge_graph_materials(std::vector<ModelMaterial>& into,
                                          const std::vector<ModelMaterial>& from);

// Host diagnostics for faces left without a drawable page: retail misses
// (no live entry) and zero-size loads. One line per block name.
std::vector<std::string> unbound_material_report(const MaterialCache& cache,
                                                 const ModelGraph& graph,
                                                 std::string_view label);

} // namespace od::port

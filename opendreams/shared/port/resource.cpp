#include "port/resource.h"

#include "port/dan.h"
#include "port/dsn.h"
#include "port/vfs.h"

#include <algorithm>
#include <array>
#include <utility>

namespace od::port {
namespace {

std::string upper_ascii(std::string_view input) {
    std::string result(input);
    for (char& ch : result)
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - ('a' - 'A'));
    return result;
}

bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}

} // namespace

bool RES_ReadFile(VfsContext& vfs, std::string_view physical_path,
                  std::vector<uint8_t>& bytes, std::string& error) {
    error.clear();
    bytes.clear();
    VfsError source;
    int32_t handle = 0;
    if (!VFS_Open(vfs,physical_path,0x200,handle,source)) {
        error = source.message;
        return false;
    }
    const auto close = [&]() {
        VfsError ignored;
        VFS_Close(vfs,handle,ignored);
    };
    uint64_t size = 0;
    if (!VFS_GetSize(vfs,handle,size,source)) {
        error=source.message; close(); return false;
    }
    if (size<8 || size>64u*1024u*1024u) {
        error="physical resource is outside the supported retail read size";
        close(); return false;
    }
    std::array<uint8_t,8> header{};
    size_t read=0;
    if (!VFS_Read(vfs,handle,header.data(),header.size(),read,source) ||
        read!=header.size()) {
        error=source ? source.message : "physical resource has no complete 8-byte header";
        close(); return false;
    }
    bytes.resize(static_cast<size_t>(size-8));
    if (!VFS_Read(vfs,handle,bytes.data(),bytes.size(),read,source) ||
        read!=bytes.size()) {
        error=source ? source.message : "physical resource body is truncated";
        bytes.clear(); close(); return false;
    }
    close();
    return true;
}

bool RES_ReadFile(DanArchive& archive, std::string_view logical_name,
                  std::vector<uint8_t>& bytes, std::string& error) {
    error.clear();
    bytes.clear();
    const std::string name = upper_ascii(logical_name);
    DanError dan_error;
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".3DC") == 0) {
        if (!DAN_Read3DC(archive, name, bytes, dan_error) ||
            !DAN_ReadTextureChunks(archive, dan_error)) {
            error = dan_error.message;
            return false;
        }
        return true;
    }
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".3DM") == 0) {
        if (!DAN_Load3DM(archive, name, bytes, dan_error)) {
            // DAN_Load3DM (0x41053e) returns size 0 for a name absent from
            // the directory; RES_Load then continues with an empty resource.
            if (dan_error.code == DanErrorCode::missing_file) {
                bytes.clear();
                return true;
            }
            error = dan_error.message;
            return false;
        }
        return true;
    }
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".3DA") == 0) {
        if (!DAN_Load3DA(archive,name,bytes,dan_error)) {
            error=dan_error.message;
            return false;
        }
        return true;
    }
    return fail(error, "DAN resource route supports .3DC, .3DM and .3DA");
}

namespace {

enum class BlockKind { none, textured, flat };

// The three case groups of MDL_BindFaceMaterials' jump table (0x455428).
BlockKind block_kind(int32_t type) {
    switch (type) {
    case -15: case -14: case -13: case -11: case -10: case -8: case -5: case -3:
    case 2: case 0x12: case 0x15:
    case -12: case -9: case -7: case -6: case -4: case 3: case 9: case 0x14:
    case 0x17: case 0x1a: case 0x1c: case 0x1d:
    case 0x16: case 0x19: case 0x1e:
        return BlockKind::textured;
    case -2: case 1: case 4: case 0x11: case 0x1b:
        return BlockKind::flat;
    default:
        return BlockKind::none;
    }
}

std::string basename_upper(std::string_view path) {
    const size_t slash = path.find_last_of("/\\");
    return upper_ascii(path.substr(slash == std::string_view::npos ? 0 : slash + 1));
}

// RES_ReadFile's .3DM branch for one route. Returns false only for a failed
// raw open (RES_Load -1); DSN and DAN misses return true with no bytes.
bool read_material_bank(MaterialRoute& route, const MaterialRecord& record,
                        ModelMaterial& material, std::string& error) {
    material = {};
    const std::string file = upper_ascii(record.file);
    if (route.dsn_pages) {
        // DSN_Create3DM (0x417a07): uppercased name against the 11-byte map
        // table; a miss returns 0, not -1.
        const auto& pages = *route.dsn_pages;
        for (size_t index = 0; index < pages.size(); ++index) {
            if (pages[index].object_name != file) continue;
            DsnError source;
            if (!DSN_Create3DM(pages[index], material, source)) {
                error = source.message;
                return false;
            }
            if (route.dsn_created && index < route.dsn_created->size())
                (*route.dsn_created)[index] = 1;
            return true;
        }
        return true;
    }
    std::vector<uint8_t> bytes;
    if (route.dan) {
        if (!RES_ReadFile(*route.dan, file + ".3DM", bytes, error)) return false;
    } else if (route.vfs) {
        if (!RES_ReadFile(*route.vfs, "DATA/3DC/" + file + ".3DM", bytes, error))
            return false;
    } else {
        return fail(error, "material load has no open resource route");
    }
    if (bytes.empty()) return true;
    if (bytes.size() != 0x18014u)
        return fail(error, "material resource is not a retail texture bank");
    material.bank = std::move(bytes);
    return true;
}

bool live_match(const MaterialCacheSlot& slot, const std::string& name) {
    return slot.refcount != 0 && slot.record.name == name;
}

} // namespace

void RES_InitArena(MaterialCache& cache) {
    for (auto& slot : cache.slots) slot = {};
}

bool MDL_BindFaceMaterials(const MaterialCache& cache, ModelGraph& graph,
                           std::string& error) {
    error.clear();
    graph.materials.clear();
    std::vector<size_t> copies(cache.slots.size(), SIZE_MAX);
    const auto bind = [&](ModelFace& face) {
        face.binding = FaceBinding::unbound;
        face.cache_slot = SIZE_MAX;
        face.material_index = SIZE_MAX;
        face.colour = 0;
        const BlockKind kind = block_kind(face.type);
        if (kind == BlockKind::none) return;
        // strcmp_ over entries 0..count-1, refcount != 0, first match wins.
        for (size_t slot = 0; slot < cache.slots.size(); ++slot) {
            if (!live_match(cache.slots[slot], face.material_name)) continue;
            face.cache_slot = slot;
            if (kind == BlockKind::flat) {
                face.binding = FaceBinding::colour;
                face.colour = cache.slots[slot].record.colour;
                return;
            }
            face.binding = FaceBinding::page;
            if (cache.slots[slot].page != MaterialPage::loaded) return;
            if (copies[slot] == SIZE_MAX) {
                copies[slot] = graph.materials.size();
                graph.materials.push_back(cache.slots[slot].bank);
                graph.materials.back().cache_slot = slot;
            }
            face.material_index = copies[slot];
            return;
        }
    };
    for (ModelFace& face : graph.faces) bind(face);
    for (ModelFace& face : graph.flat_faces) bind(face);
    return true;
}

bool MDL_BindTreeMaterials(const MaterialCache& cache, ModelGraph& graph,
                           std::string& error) {
    return MDL_BindFaceMaterials(cache, graph, error);
}

bool MDL_LoadMaterials(MaterialCache& cache, MaterialRoute& route,
                       ModelGraph& graph, std::string& error) {
    error.clear();
    for (const MaterialRecord& record : graph.directory) {
        bool hit = false;
        for (auto& slot : cache.slots) {
            if (!live_match(slot, record.name)) continue;
            ++slot.refcount; // A live entry is shared; nothing is loaded.
            hit = true;
            break;
        }
        if (hit) continue;
        size_t free = 0;
        while (free < cache.slots.size() && cache.slots[free].refcount) ++free;
        if (free == cache.slots.size()) {
            if (cache.slots.size() >= MaterialCache::capacity)
                return fail(error, "material cache exceeds its 256 retail entries");
            cache.slots.emplace_back();
        }
        MaterialCacheSlot& slot = cache.slots[free];
        slot = {};
        slot.refcount = 1;
        slot.record = record;
        slot.source = route.source;
        if (record.file.empty()) continue; // Flat colour: nothing to load.
        ModelMaterial material;
        if (!read_material_bank(route, record, material, error)) {
            error = "model material " + record.name + " (" + record.file + ".3DM): " + error;
            return false;
        }
        if (material.bank.empty()) {
            slot.page = MaterialPage::zero_size;
            continue;
        }
        material.name = record.name;
        slot.bank = std::move(material);
        slot.page = MaterialPage::loaded;
    }
    return MDL_BindTreeMaterials(cache, graph, error);
}

bool MDL_LoadMaterials(DanArchive& archive, ModelGraph& graph, std::string& error) {
    MaterialCache cache;
    MaterialRoute route;
    route.dan = &archive;
    route.source = basename_upper(archive.path());
    return MDL_LoadMaterials(cache, route, graph, error);
}

bool RES_Load(DanArchive& archive, std::string_view logical_name,
              MaterialCache& cache, ModelGraph& graph, std::string& error) {
    graph = {};
    std::vector<uint8_t> bytes;
    if (!RES_ReadFile(archive, logical_name, bytes, error)) return false;
    if (!RES_Relocate(bytes, graph, error)) return false;
    MaterialRoute route;
    route.dan = &archive;
    route.source = basename_upper(archive.path());
    return MDL_LoadMaterials(cache, route, graph, error);
}

bool RES_Load(VfsContext& vfs, std::string_view physical_path,
              MaterialCache& cache, ModelGraph& graph, std::string& error) {
    graph = {};
    const std::string path = upper_ascii(physical_path);
    if (path.size() < 4 || path.substr(path.size() - 4) != ".3DC")
        return fail(error, "loose model resource must be a .3DC file");
    std::vector<uint8_t> bytes;
    if (!RES_ReadFile(vfs, physical_path, bytes, error) ||
        !RES_Relocate(bytes, graph, error)) return false;
    MaterialRoute route;
    route.vfs = &vfs;
    route.source = basename_upper(path);
    return MDL_LoadMaterials(cache, route, graph, error);
}

bool RES_Load(DanArchive& archive, std::string_view logical_name,
              ModelGraph& graph, std::string& error) {
    MaterialCache cache;
    return RES_Load(archive, logical_name, cache, graph, error);
}

bool RES_Load(VfsContext& vfs, std::string_view physical_path,
              ModelGraph& graph, std::string& error) {
    MaterialCache cache;
    return RES_Load(vfs, physical_path, cache, graph, error);
}

std::vector<size_t> merge_graph_materials(std::vector<ModelMaterial>& into,
                                          const std::vector<ModelMaterial>& from) {
    std::vector<size_t> mapping;
    mapping.reserve(from.size());
    for (const auto& material : from) {
        size_t found = SIZE_MAX;
        if (material.cache_slot != SIZE_MAX)
            for (size_t index = 0; index < into.size(); ++index)
                if (into[index].cache_slot == material.cache_slot) {
                    found = index;
                    break;
                }
        if (found == SIZE_MAX) {
            found = into.size();
            into.push_back(material);
        }
        mapping.push_back(found);
    }
    return mapping;
}

std::vector<std::string> unbound_material_report(const MaterialCache& cache,
                                                 const ModelGraph& graph,
                                                 std::string_view label) {
    struct Line { std::string name; std::string reason; size_t faces = 0; };
    std::vector<Line> lines;
    const auto note = [&](const ModelFace& face, std::string reason) {
        for (auto& line : lines)
            if (line.name == face.material_name && line.reason == reason) {
                ++line.faces;
                return;
            }
        lines.push_back({face.material_name, std::move(reason), 1});
    };
    for (const ModelFace& face : graph.faces) {
        if (face.binding == FaceBinding::unbound) {
            note(face, "no live material-cache entry; retail leaves +0x08 unrelocated");
            continue;
        }
        if (face.binding != FaceBinding::page || face.cache_slot >= cache.slots.size())
            continue;
        const auto& slot = cache.slots[face.cache_slot];
        if (slot.page == MaterialPage::zero_size)
            note(face, upper_ascii(slot.record.file) + ".3DM absent from " + slot.source +
                           "; retail zero-size load (stale arena page)");
        else if (slot.page == MaterialPage::none)
            note(face, "textured block bound to flat-colour entry from " + slot.source);
    }
    std::vector<std::string> report;
    for (const auto& line : lines)
        report.push_back(std::string(label) + ":" + line.name + " (" +
                         std::to_string(line.faces) + " faces not drawn): " + line.reason);
    return report;
}

} // namespace od::port

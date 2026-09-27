#include "port/resource.h"

#include "port/dan.h"
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
            error = dan_error.message;
            return false;
        }
        return true;
    }
    return fail(error, "DAN resource route supports .3DC and .3DM in this slice");
}

bool MDL_BindFaceMaterials(ModelGraph& graph, std::string& error) {
    error.clear();
    for (ModelFace& face : graph.faces) {
        const std::string name = upper_ascii(face.material_name);
        const auto found = std::find_if(graph.materials.begin(), graph.materials.end(),
            [&](const ModelMaterial& material) {
                return upper_ascii(material.name) == name;
            });
        if (found == graph.materials.end())
            return fail(error, "model face material has no loaded bank");
        face.material_index = static_cast<size_t>(found - graph.materials.begin());
    }
    return true;
}

bool MDL_BindTreeMaterials(ModelGraph& graph, std::string& error) {
    return MDL_BindFaceMaterials(graph, error);
}

bool MDL_LoadMaterials(DanArchive& archive, ModelGraph& graph, std::string& error) {
    error.clear();
    graph.materials.clear();
    for (const ModelFace& face : graph.faces) {
        if (face.material_name.empty())
            return fail(error, "model face block has no material name");
        const std::string name = upper_ascii(face.material_name);
        const auto found = std::find_if(graph.materials.begin(), graph.materials.end(),
            [&](const ModelMaterial& material) {
                return upper_ascii(material.name) == name;
            });
        if (found != graph.materials.end()) continue;
        std::vector<uint8_t> bytes;
        if (!RES_ReadFile(archive, name + ".3DM", bytes, error)) return false;
        graph.materials.push_back({name, std::move(bytes)});
    }
    return MDL_BindTreeMaterials(graph, error);
}

bool RES_Load(DanArchive& archive, std::string_view logical_name,
              ModelGraph& graph, std::string& error) {
    graph = {};
    std::vector<uint8_t> bytes;
    if (!RES_ReadFile(archive, logical_name, bytes, error)) return false;
    if (!RES_Relocate(bytes, graph, error)) return false;
    return MDL_LoadMaterials(archive, graph, error);
}

} // namespace od::port

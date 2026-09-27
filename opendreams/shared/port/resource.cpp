#include "port/resource.h"

#include "port/dan.h"

#include <algorithm>
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

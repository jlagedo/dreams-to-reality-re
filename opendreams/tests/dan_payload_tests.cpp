#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/dan.h"
#include "port/model.h"
#include "port/resource.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

uint32_t crc32(const std::vector<uint8_t>& bytes) {
    uint32_t value = 0xffffffffu;
    for (uint8_t byte : bytes) {
        value ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1u) ? 0xedb88320u : 0u);
    }
    return value ^ 0xffffffffu;
}

} // namespace

int main() {
    const char* cue = std::getenv("DREAMS_CUE2");
    if (!cue || !*cue) {
        std::cout << "SKIP: set DREAMS_CUE2 for CAI.DAN corpus\n";
        return 77;
    }
    od::disc::Error image_error;
    auto opened = od::disc::Image::open(std::filesystem::u8path(cue), image_error);
    if (!opened) {
        std::cerr << image_error.message << '\n';
        return 1;
    }
    std::shared_ptr<const od::disc::Image> image(std::move(opened));
    od::port::VfsContext vfs(image);
    od::port::DanArchive archive(vfs);
    od::port::DanError error;
    if (!od::port::DAN_OpenArchive(archive, "DATA/3DC/CAI.DAN", error)) {
        std::cerr << error.message << '\n';
        return 2;
    }
    std::vector<uint8_t> model, texture;
    if (!od::port::DAN_Read3DC(archive, "CAI.3DC", model, error) ||
        !od::port::DAN_ReadTextureChunks(archive, error) ||
        !od::port::DAN_Load3DM(archive, "CAISSE.3DM", texture, error)) {
        std::cerr << error.message << '\n';
        return 3;
    }
    if (model.size() != 3248 || crc32(model) != 0x84df2731u ||
        texture.size() != 98324 || crc32(texture) != 0xcc4e1967u) {
        std::cerr << "CAI model or texture differs from Python oracle\n";
        return 4;
    }
    od::port::ModelGraph graph;
    std::string model_error;
    if (!od::port::RES_Relocate(model, graph, model_error) ||
        graph.nodes.size() != 1 || graph.faces.size() != 10 ||
        graph.nodes[0].vertices.size() != 8) {
        std::cerr << "CAI node relocation failed: " << model_error << '\n';
        return 6;
    }
    for (const auto& face : graph.faces)
        if (face.material_name != "CAISSE" || face.type != 2) {
            std::cerr << "CAI face material or type differs from Python oracle\n";
            return 7;
        }
    if (!od::port::RES_Load(archive, "CAI.3DC", graph, model_error) ||
        graph.materials.size() != 1 || graph.materials[0].name != "CAISSE" ||
        crc32(graph.materials[0].bank) != 0xcc4e1967u ||
        graph.faces.size() != 10) {
        std::cerr << "CAI resource/material load failed: " << model_error << '\n';
        return 8;
    }
    for (const auto& face : graph.faces)
        if (face.material_index != 0) return 9;
    if (archive.texture_chunks().size() != 1 ||
        od::port::DAN_Load3DM(archive, "MISSING.3DM", texture, error) ||
        error.code != od::port::DanErrorCode::missing_file) {
        std::cerr << "DAN material directory lookup differs\n";
        return 5;
    }
    std::cout << "CAI.3DC and CAISSE.3DM match Python decompressed CRCs\n";
    return 0;
}

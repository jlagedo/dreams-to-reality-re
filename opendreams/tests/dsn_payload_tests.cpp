#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/dsn.h"
#include "port/model.h"
#include "port/stream.h"

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
        std::cout << "SKIP: set DREAMS_CUE2 for E29USINE.DSN corpus\n";
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
    od::port::StreamError stream_error;
    auto stream = od::port::STRM_Create(vfs, 0x57800, 0x57800, 0x8000, stream_error);
    if (!stream) {
        std::cerr << stream_error.message << '\n';
        return 2;
    }
    od::port::DsnState state;
    od::port::DSN_InitState(state, *stream);
    od::port::DsnError error;
    std::vector<uint8_t> geometry, collision;
    if (!od::port::DSN_LoadHeader(state, "DATA/3DC/E29USINE.DSN", error) ||
        !od::port::DSN_LoadMaterialsAndFaces(state, geometry, error) ||
        !od::port::DSN_LoadVertexPool(state, collision, error)) {
        std::cerr << error.message << '\n';
        return 3;
    }
    if (geometry.size() != 657696 || crc32(geometry) != 0x3dddd9edu ||
        collision.size() != 256224 || crc32(collision) != 0x51d8ea87u) {
        std::cerr << "E29USINE body differs from Python oracle\n";
        return 4;
    }
    od::port::ModelGraph graph;
    std::string model_error;
    if (!od::port::RES_Relocate(geometry, graph, model_error) ||
        graph.nodes.size() != 55 || graph.faces.size() != 2225) {
        std::cerr << "E29USINE render graph differs: " << model_error << " ("
                  << graph.nodes.size() << " nodes, " << graph.faces.size()
                  << " faces)\n";
        return 5;
    }
    std::cout << "E29USINE tag-1/2 payloads and render graph match Python oracle\n";
    return 0;
}

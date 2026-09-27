#pragma once

#include "port/stream.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

enum class DsnErrorCode {
    none,
    no_stream,
    invalid_state,
    missing_file,
    truncated_header,
    invalid_header,
    invalid_chunk,
    stream_error,
};

struct DsnError {
    DsnErrorCode code = DsnErrorCode::none;
    std::string message;
    StreamError stream;
    explicit operator bool() const { return code != DsnErrorCode::none; }
};

struct DsnObject {
    std::string name;
    std::array<uint8_t, 11> raw_name{};
    std::array<uint8_t, 20> raw_record{};
    std::array<uint32_t, 5> words{};
};

struct DsnTexturePage {
    std::string object_name;
    std::array<uint16_t, 256> palette_rgb565{};
    std::vector<uint8_t> indices; // 256x256 page, assembled from 64 planes.
};

// Retail's 0x6fc-byte mutable DSN header state, with its stream reference and
// copied name/object tables exposed as source-scoped values.
class DsnState {
public:
    bool loaded() const { return loaded_; }
    bool geometry_loaded() const { return geometry_loaded_; }
    uint32_t declared_size() const { return declared_size_; }
    uint32_t span() const { return span_; }
    uint16_t name_count() const { return name_count_; }
    size_t body_offset() const { return body_offset_; }
    std::string_view path() const { return path_; }
    const std::vector<DsnObject>& objects() const { return objects_; }
    Stream* stream() const { return stream_; }

private:
    Stream* stream_ = nullptr;
    std::string path_;
    std::vector<DsnObject> objects_;
    uint32_t declared_size_ = 0;
    uint32_t span_ = 0;
    uint16_t name_count_ = 0;
    size_t body_offset_ = 0;
    bool loaded_ = false;
    bool geometry_loaded_ = false;

    friend bool DSN_InitState(DsnState&, Stream&);
    friend void DSN_ResetState(DsnState&);
    friend bool DSN_LoadHeader(DsnState&, std::string_view, DsnError&);
    friend bool DSN_LoadMaterialsAndFaces(DsnState&, std::vector<uint8_t>&, DsnError&);
    friend bool DSN_LoadVertexPool(DsnState&, std::vector<uint8_t>&, DsnError&);
};

bool DSN_InitState(DsnState& state, Stream& stream);
void DSN_ResetState(DsnState& state);
// Leaves the retail stream positioned at the first packed-body tag on success.
bool DSN_LoadHeader(DsnState& state, std::string_view path, DsnError& error);
bool DSN_LoadMaterialsAndFaces(DsnState& state, std::vector<uint8_t>& geometry,
                               DsnError& error);
bool DSN_LoadVertexPool(DsnState& state, std::vector<uint8_t>& collision,
                        DsnError& error);
// Viewer adaptation of the retail 32-step loader: consume the palette and
// all 64 planes for one selected scene object in one bounded call.
bool DSN_LoadTextures(DsnState& state, size_t object_index,
                      DsnTexturePage& page, DsnError& error);
bool DSN_BlitTileToPage(const uint8_t* plane, size_t plane_size,
                        unsigned record, std::vector<uint8_t>& page,
                        DsnError& error);

} // namespace od::port

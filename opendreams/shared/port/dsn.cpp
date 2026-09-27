#include "port/dsn.h"

#include "port/model.h"
#include "port/lz.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace od::port {
namespace {

bool fail(DsnError& error, DsnErrorCode code, std::string message) {
    error = {code, std::move(message), {}};
    return false;
}

bool stream_fail(DsnError& error, const StreamError& stream) {
    error = {DsnErrorCode::stream_error, stream.message, stream};
    return false;
}

uint16_t little16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(bytes[1] << 8);
}

uint32_t little32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
}

bool take(Stream& stream, size_t length, std::vector<uint8_t>& output,
          DsnError& error) {
    const uint8_t* pointer = nullptr;
    StreamError stream_error;
    if (!STRM_Peek(stream, length, pointer, stream_error))
        return fail(error, DsnErrorCode::truncated_header,
                    "DSN header needs more bytes: " + stream_error.message);
    output.assign(pointer, pointer + length);
    if (!STRM_Commit(stream, stream_error)) return stream_fail(error, stream_error);
    return true;
}

bool take_body(Stream& stream, size_t length, std::vector<uint8_t>& output,
               DsnError& error) {
    output.clear();
    if (length > stream.ring_size())
        return fail(error, DsnErrorCode::invalid_chunk,
                    "DSN packed chunk exceeds the stream ring");
    StreamError stream_error;
    while (stream.available() < length) {
        const size_t before = stream.available();
        if (!STRM_Fill(stream, stream_error)) return stream_fail(error, stream_error);
        if (stream.available() == before)
            return fail(error, DsnErrorCode::invalid_chunk,
                        "DSN packed chunk ends before its declared length");
    }
    const uint8_t* pointer = nullptr;
    if (!STRM_Peek(stream, length, pointer, stream_error))
        return stream_fail(error, stream_error);
    output.assign(pointer, pointer + length);
    if (!STRM_Commit(stream, stream_error)) return stream_fail(error, stream_error);
    return true;
}

bool unpack_next(DsnState& state, uint8_t expected,
                 std::vector<uint8_t>& output, DsnError& error) {
    output.clear();
    if (!state.loaded() || !state.stream())
        return fail(error, DsnErrorCode::no_stream, "DSN header is not loaded");
    Stream& stream = *state.stream();
    std::vector<uint8_t> header, packed;
    if (!take_body(stream, 5, header, error)) return false;
    const uint32_t size = little32(header.data() + 1);
    if (header[0] != expected || size < 5 || size > state.declared_size())
        return fail(error, DsnErrorCode::invalid_chunk,
                    "DSN body tag or packed length is invalid");
    if (!take_body(stream, size - 5, packed, error)) return false;
    std::string lz_error;
    if (!LZ_Unpack(packed.data(), packed.size(), output, lz_error))
        return fail(error, DsnErrorCode::invalid_chunk,
                    "DSN body decompression failed: " + lz_error);
    return true;
}

} // namespace

bool DSN_InitState(DsnState& state, Stream& stream) {
    state = {};
    state.stream_ = &stream;
    return true;
}

void DSN_ResetState(DsnState& state) {
    Stream* stream = state.stream_;
    state = {};
    state.stream_ = stream;
}

bool DSN_LoadHeader(DsnState& state, std::string_view path, DsnError& error) {
    error = {};
    DSN_ResetState(state);
    if (!state.stream_) return fail(error, DsnErrorCode::no_stream,
                                    "DSN loader has no stream context");
    Stream& stream = *state.stream_;
    StreamError stream_error;
    if (!STRM_Open(stream, path, stream_error)) {
        if (stream_error.code == StreamErrorCode::missing_file)
            return fail(error, DsnErrorCode::missing_file, stream_error.message);
        return stream_fail(error, stream_error);
    }
    const auto close_on_failure = [&]() {
        StreamError ignored;
        STRM_Close(stream, ignored);
    };
    if (!STRM_Fill(stream, stream_error)) {
        close_on_failure();
        return stream_fail(error, stream_error);
    }
    std::vector<uint8_t> header;
    if (!take(stream, 9, header, error)) {
        close_on_failure();
        return false;
    }
    if (std::memcmp(header.data(), "DSNF", 4) != 0) {
        close_on_failure();
        return fail(error, DsnErrorCode::invalid_header, "DSN file has no DSNF magic");
    }
    state.declared_size_ = little32(header.data() + 5);
    if (state.declared_size_ != stream.source_size()) {
        close_on_failure();
        return fail(error, DsnErrorCode::invalid_header,
                    "DSN declared size differs from its physical file");
    }
    if (!take(stream, 5, header, error)) {
        close_on_failure();
        return false;
    }
    if (header[0] != 0) {
        close_on_failure();
        return fail(error, DsnErrorCode::invalid_header,
                    "DSN second header flag must be zero");
    }
    state.span_ = little32(header.data() + 1);
    if (!take(stream, 2, header, error)) {
        close_on_failure();
        return false;
    }
    state.name_count_ = little16(header.data());
    if (state.name_count_ > 32) {
        close_on_failure();
        return fail(error, DsnErrorCode::invalid_header,
                    "DSN name count exceeds the retail header table");
    }
    state.body_offset_ = 16 + static_cast<size_t>(state.name_count_) * 31;
    if (state.body_offset_ > stream.source_size()) {
        close_on_failure();
        return fail(error, DsnErrorCode::truncated_header,
                    "DSN name and object tables exceed the file");
    }
    std::vector<uint8_t> names, records;
    if (!take(stream, static_cast<size_t>(state.name_count_) * 11, names, error) ||
        !take(stream, static_cast<size_t>(state.name_count_) * 20, records, error)) {
        close_on_failure();
        return false;
    }
    state.objects_.reserve(state.name_count_);
    for (size_t i = 0; i < state.name_count_; ++i) {
        DsnObject object;
        std::memcpy(object.raw_name.data(), names.data() + i * 11, 11);
        std::memcpy(object.raw_record.data(), records.data() + i * 20, 20);
        size_t used = 0;
        while (used < object.raw_name.size() && object.raw_name[used] != 0) ++used;
        object.name.assign(reinterpret_cast<const char*>(object.raw_name.data()), used);
        for (size_t word = 0; word < object.words.size(); ++word)
            object.words[word] = little32(object.raw_record.data() + word * 4);
        state.objects_.push_back(std::move(object));
    }
    state.path_ = std::string(path);
    state.loaded_ = true;
    return true;
}

bool DSN_LoadMaterialsAndFaces(DsnState& state, std::vector<uint8_t>& geometry,
                               DsnError& error) {
    error = {};
    state.geometry_loaded_ = false;
    if (!unpack_next(state, 1, geometry, error)) return false;
    state.geometry_loaded_ = true;
    return true;
}

bool DSN_LoadVertexPool(DsnState& state, std::vector<uint8_t>& collision,
                        DsnError& error) {
    error = {};
    if (!state.geometry_loaded_)
        return fail(error, DsnErrorCode::invalid_state,
                    "DSN geometry must load before the vertex pool");
    return unpack_next(state, 2, collision, error);
}

bool DSN_BlitTileToPage(const uint8_t* plane, size_t plane_size,
                        unsigned record, std::vector<uint8_t>& page,
                        DsnError& error) {
    error = {};
    if (!plane || plane_size != 1024 || record >= 64 || page.size() != 256u * 256u)
        return fail(error, DsnErrorCode::invalid_chunk,
                    "DSN texture plane or destination has an invalid size");
    const unsigned row = ((record & 1u) << 2) | ((record & 4u) >> 1) |
                         ((record & 0x10u) >> 4);
    const unsigned col = ((record & 2u) << 1) | ((record & 8u) >> 2) |
                         ((record & 0x20u) >> 5);
    const unsigned h = record < 2 ? 8 : record < 8 ? 4 : record < 16 ? 2 : 1;
    const unsigned v = record < 1 ? 8 : record < 4 ? 4 : record < 16 ? 2 : 1;
    for (unsigned y = 0; y < 32; ++y) for (unsigned x = 0; x < 32; ++x) {
        const uint8_t index = plane[y * 32 + x];
        const unsigned left = col + x * 8, top = row + y * 8;
        for (unsigned dy = 0; dy < v && top + dy < 256; ++dy)
            for (unsigned dx = 0; dx < h && left + dx < 256; ++dx)
                page[(top + dy) * 256 + left + dx] = index;
    }
    return true;
}

bool DSN_LoadTextures(DsnState& state, size_t object_index,
                      DsnTexturePage& page, DsnError& error) {
    page = {};
    if (!state.loaded() || !state.geometry_loaded() || !state.stream())
        return fail(error,DsnErrorCode::invalid_state,
                    "DSN header and packed geometry must load before textures");
    if (object_index >= state.objects().size())
        return fail(error,DsnErrorCode::invalid_state,
                    "DSN texture object index exceeds the scene directory");
    std::vector<DsnTexturePage> pages;
    if (!DSN_LoadTextures(state,pages,error)) return false;
    page = std::move(pages[object_index]);
    return true;
}

bool DSN_LoadTextures(DsnState& state, std::vector<DsnTexturePage>& pages,
                      DsnError& error) {
    error = {};
    pages.clear();
    if (!state.loaded() || !state.geometry_loaded() || !state.stream())
        return fail(error, DsnErrorCode::invalid_state,
                    "DSN header and packed geometry must load before textures");
    Stream& stream = *state.stream();
    const size_t expected = state.objects().size() * 1024u;
    std::vector<uint8_t> header, payload;
    const auto read_tag = [&](uint8_t tag) -> bool {
        if (!take_body(stream, 5, header, error)) return false;
        const uint32_t span = little32(header.data() + 1);
        if (header[0] != tag || span != expected + 5)
            return fail(error, DsnErrorCode::invalid_chunk,
                        "DSN texture tag type or per-object size is invalid");
        return take_body(stream, expected, payload, error);
    };
    if (!read_tag(3)) return false;
    pages.resize(state.objects().size());
    for (size_t object=0; object<pages.size(); ++object) {
        auto& decoded=pages[object];
        decoded.object_name=state.objects()[object].name;
        decoded.header_template=state.objects()[object].raw_record;
        const size_t start=object*1024u;
        for (size_t i=0; i<decoded.palette_rgb565.size(); ++i)
            decoded.palette_rgb565[i]=little16(payload.data()+start+i*4u+2u);
        decoded.indices.assign(256u*256u,0);
    }
    for (unsigned record = 0; record < 64; ++record) {
        if (!read_tag(4)) return false;
        for (size_t object=0; object<pages.size(); ++object)
            if (!DSN_BlitTileToPage(payload.data()+object*1024u,1024,record,
                                    pages[object].indices,error)) return false;
    }
    return true;
}

bool DSN_Create3DM(const DsnTexturePage& page, ModelMaterial& material,
                   DsnError& error) {
    error = {};
    material = {};
    if (page.object_name.empty() || page.indices.size()!=256u*256u)
        return fail(error,DsnErrorCode::invalid_state,
                    "scene material has no name or complete texture page");
    material.name=page.object_name;
    material.bank.assign(0x18014u,0);
    std::copy(page.header_template.begin(),page.header_template.end(),
              material.bank.begin());
    material.preview_lod=256;
    material.static_palette_row15=true;
    const size_t palette=0x14u+15u*0x400u;
    for (size_t i=0; i<256; ++i) {
        const uint16_t packed=page.palette_rgb565[i];
        material.bank[palette+i*4u+2u]=static_cast<uint8_t>(packed);
        material.bank[palette+i*4u+3u]=static_cast<uint8_t>(packed>>8);
    }
    std::copy(page.indices.begin(),page.indices.end(),
              material.bank.begin()+0x8014u);
    return true;
}

} // namespace od::port

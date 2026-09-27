#include "port/dsn.h"
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

} // namespace od::port

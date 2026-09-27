#pragma once

#include "port/vfs.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

enum class StreamErrorCode {
    none,
    invalid_config,
    missing_file,
    invalid_state,
    invalid_peek,
    source_error,
};

struct StreamError {
    StreamErrorCode code = StreamErrorCode::none;
    std::string message;
    VfsError source;
    explicit operator bool() const { return code != StreamErrorCode::none; }
};

// Retail's 0x2c-byte stream descriptor, with the same ring cursor and
// peek/commit transitions. Ownership and storage are explicit and host-sized.
class Stream {
public:
    bool is_open() const { return open_; }
    size_t available() const { return available_; }
    uint64_t source_size() const { return source_size_; }
    size_t ring_size() const { return ring_size_; }

private:
    VfsContext* vfs_ = nullptr;
    std::vector<uint8_t> storage_;
    size_t read_offset_ = 0;
    size_t write_offset_ = 0;
    size_t available_ = 0;
    size_t free_ = 0;
    size_t ring_size_ = 0;
    size_t slack_size_ = 0;
    size_t chunk_size_ = 0;
    size_t peeked_ = 0;
    int32_t handle_ = 0;
    uint64_t source_size_ = 0;
    bool open_ = false;
    bool full_ = false;

    friend std::unique_ptr<Stream> STRM_Create(VfsContext&, size_t, size_t, size_t,
                                                 StreamError&);
    friend bool STRM_Open(Stream&, std::string_view, StreamError&);
    friend bool STRM_Fill(Stream&, StreamError&);
    friend bool STRM_Peek(Stream&, size_t, const uint8_t*&, StreamError&);
    friend bool STRM_Commit(Stream&, StreamError&);
    friend bool STRM_Close(Stream&, StreamError&);
};

// The original GAME_Init passes 0x57800, 0x57800, 0x8000. STRM_Create rounds
// each requested size up to the chunk alignment, as the retail function does.
std::unique_ptr<Stream> STRM_Create(VfsContext& vfs, size_t ring_request,
                                    size_t slack_request, size_t chunk_size,
                                    StreamError& error);
bool STRM_Open(Stream& stream, std::string_view path, StreamError& error);
bool STRM_Fill(Stream& stream, StreamError& error);
// A false peek with code=invalid_peek means the requested bytes are not yet
// buffered or the source ended. The returned pointer remains valid until the
// next fill, peek, commit or open on this Stream.
bool STRM_Peek(Stream& stream, size_t length, const uint8_t*& bytes,
               StreamError& error);
bool STRM_Commit(Stream& stream, StreamError& error);
bool STRM_Close(Stream& stream, StreamError& error);
void STRM_Free(std::unique_ptr<Stream>& stream);

} // namespace od::port

#include "port/stream.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace od::port {
namespace {

bool fail(StreamError& error, StreamErrorCode code, std::string message) {
    error = {code, std::move(message), {}};
    return false;
}

bool source_fail(StreamError& error, const VfsError& source) {
    error = {StreamErrorCode::source_error, source.message, source};
    return false;
}

} // namespace

std::unique_ptr<Stream> STRM_Create(VfsContext& vfs, size_t ring_request,
                                    size_t slack_request, size_t chunk_size,
                                    StreamError& error) {
    error = {};
    if (chunk_size == 0 || (chunk_size & (chunk_size - 1)) != 0 ||
        ring_request == 0 || slack_request == 0 ||
        ring_request > std::numeric_limits<size_t>::max() - (chunk_size - 1) ||
        slack_request > std::numeric_limits<size_t>::max() - (chunk_size - 1)) {
        fail(error, StreamErrorCode::invalid_config, "invalid stream alignment or size");
        return nullptr;
    }
    const size_t ring = (ring_request + chunk_size - 1) & ~(chunk_size - 1);
    const size_t slack = (slack_request + chunk_size - 1) & ~(chunk_size - 1);
    if (ring > 32 * 1024 * 1024 || slack > ring) {
        fail(error, StreamErrorCode::invalid_config,
             "stream ring or wrap area exceeds the supported size");
        return nullptr;
    }
    auto result = std::unique_ptr<Stream>(new Stream);
    result->vfs_ = &vfs;
    result->ring_size_ = ring;
    result->slack_size_ = slack;
    result->chunk_size_ = chunk_size;
    result->free_ = ring;
    return result;
}

bool STRM_Open(Stream& stream, std::string_view path, StreamError& error) {
    error = {};
    StreamError close_error;
    if (!STRM_Close(stream, close_error)) return fail(error, close_error.code,
                                                       close_error.message);
    stream.read_offset_ = 0;
    stream.write_offset_ = 0;
    stream.available_ = 0;
    stream.free_ = stream.ring_size_;
    stream.peeked_ = 0;
    stream.full_ = false;
    stream.source_size_ = 0;
    VfsError source;
    int32_t handle = 0;
    if (!VFS_Open(*stream.vfs_, path, 0x200, handle, source)) {
        if (source.code == VfsErrorCode::missing_file)
            return fail(error, StreamErrorCode::missing_file, source.message);
        return source_fail(error, source);
    }
    if (handle < 0) {
        VFS_Close(*stream.vfs_, handle, source);
        return fail(error, StreamErrorCode::missing_file,
                    "retail stream requires a loose disc file");
    }
    uint64_t size = 0;
    if (!VFS_Seek(*stream.vfs_, handle, 0, 2, size, source)) {
        VfsError ignored;
        VFS_Close(*stream.vfs_, handle, ignored);
        return source_fail(error, source);
    }
    uint64_t position = 0;
    if (!VFS_Seek(*stream.vfs_, handle, 0, 0, position, source)) {
        VfsError ignored;
        VFS_Close(*stream.vfs_, handle, ignored);
        return source_fail(error, source);
    }
    stream.storage_.assign(stream.ring_size_ * 2, 0);
    stream.handle_ = handle;
    stream.source_size_ = size;
    stream.open_ = true;
    return true;
}

bool STRM_Fill(Stream& stream, StreamError& error) {
    error = {};
    if (!stream.open_ || stream.full_) return true;
    if (stream.free_ < stream.chunk_size_) {
        stream.full_ = true;
        return true;
    }
    if (stream.write_offset_ + stream.chunk_size_ > stream.ring_size_)
        return fail(error, StreamErrorCode::invalid_state,
                    "stream write cursor is not chunk-aligned");
    VfsError source;
    size_t count = 0;
    if (!VFS_Read(*stream.vfs_, stream.handle_,
                  stream.storage_.data() + stream.write_offset_,
                  stream.chunk_size_, count, source))
        return source_fail(error, source);
    stream.write_offset_ = (stream.write_offset_ + count) % stream.ring_size_;
    stream.available_ += count;
    stream.free_ -= count;
    if (count != stream.chunk_size_) {
        StreamError close_error;
        if (!STRM_Close(stream, close_error)) return fail(error, close_error.code,
                                                           close_error.message);
    }
    return true;
}

bool STRM_Peek(Stream& stream, size_t length, const uint8_t*& bytes,
               StreamError& error) {
    error = {};
    bytes = nullptr;
    if (length > stream.available_)
        return fail(error, StreamErrorCode::invalid_peek,
                    "stream has fewer buffered bytes than requested");
    if (length > stream.ring_size_ + stream.slack_size_ - stream.read_offset_)
        return fail(error, StreamErrorCode::invalid_peek,
                    "stream peek exceeds the wrap area");
    if (stream.read_offset_ + length > stream.ring_size_) {
        const size_t wrapped = length - (stream.ring_size_ - stream.read_offset_);
        std::memmove(stream.storage_.data() + stream.ring_size_,
                     stream.storage_.data(), wrapped);
    }
    stream.peeked_ = length;
    bytes = stream.storage_.data() + stream.read_offset_;
    return true;
}

bool STRM_Commit(Stream& stream, StreamError& error) {
    error = {};
    if (stream.peeked_ > stream.available_ || stream.ring_size_ == 0)
        return fail(error, StreamErrorCode::invalid_state,
                    "stream has no valid peek to commit");
    stream.read_offset_ = (stream.read_offset_ + stream.peeked_) % stream.ring_size_;
    stream.available_ -= stream.peeked_;
    stream.free_ += stream.peeked_;
    stream.full_ = false;
    stream.peeked_ = 0;
    return true;
}

bool STRM_Close(Stream& stream, StreamError& error) {
    error = {};
    if (stream.open_) {
        VfsError source;
        if (!VFS_Close(*stream.vfs_, stream.handle_, source))
            return source_fail(error, source);
    }
    stream.open_ = false;
    stream.handle_ = 0;
    return true;
}

void STRM_Free(std::unique_ptr<Stream>& stream) {
    if (!stream) return;
    StreamError ignored;
    STRM_Close(*stream, ignored);
    stream.reset();
}

} // namespace od::port

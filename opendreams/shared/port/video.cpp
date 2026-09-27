#include "port/video.h"

#include <array>
#include <cstring>
#include <utility>

namespace od::port {
namespace {

int fail(VideoError& error, VideoErrorCode code, std::string message) {
    error = {code, std::move(message), {}};
    return 0;
}

int source_fail(VideoError& error, const StreamError& source) {
    error = {VideoErrorCode::source_error, source.message, source};
    return 0;
}

bool magic_is(const std::array<uint8_t, 64>& header, const char* magic) {
    return std::memcmp(header.data(), magic, 4) == 0;
}

} // namespace

VideoState::~VideoState() {
    VID_Close(*this);
    STRM_Free(stream_);
}

void VID_Close(VideoState& state) {
    // No decoder or DirectSound channel is started in 002. Clear the logical
    // sound selection and close the selected stream on every path, including
    // a header failure that left the retail kind at zero.
    state.sound_variant_selected_ = false;
    if (state.stream_) {
        StreamError ignored;
        STRM_Close(*state.stream_, ignored);
    }
    state.family_ = VideoFamily::none;
    state.kind_flags_ = 0;
    state.header_.fill(0);
    state.next_word_.fill(0);
    state.path_.clear();
    state.source_size_ = 0;
}

int VID_Open(VideoState& state, std::string_view path, VideoError& error) {
    error = {};
    VID_Close(state);
    StreamError stream_error;
    if (!state.stream_) {
        state.stream_ = STRM_Create(*state.vfs_, 0x57800, 0x57800, 0x8000,
                                     stream_error);
        if (!state.stream_) return source_fail(error, stream_error);
    }
    if (!STRM_Open(*state.stream_, path, stream_error)) {
        if (stream_error.code == StreamErrorCode::missing_file)
            return fail(error, VideoErrorCode::missing_file, stream_error.message);
        return source_fail(error, stream_error);
    }
    for (unsigned i = 0; i < 0x18; ++i) {
        if (!STRM_Fill(*state.stream_, stream_error)) {
            VID_Close(state);
            return source_fail(error, stream_error);
        }
    }
    const uint8_t* bytes = nullptr;
    if (!STRM_Peek(*state.stream_, 0x44, bytes, stream_error)) {
        VID_Close(state);
        return fail(error, VideoErrorCode::truncated_header,
                    "video file has fewer than 68 header bytes");
    }
    std::memcpy(state.header_.data(), bytes, state.header_.size());
    std::memcpy(state.next_word_.data(), bytes + state.header_.size(),
                state.next_word_.size());
    if (!STRM_Commit(*state.stream_, stream_error)) {
        VID_Close(state);
        return source_fail(error, stream_error);
    }
    bool sound_variant = false;
    if (magic_is(state.header_, "HNM4")) state.family_ = VideoFamily::hnm4;
    else if (magic_is(state.header_, "HNS4")) {
        state.family_ = VideoFamily::hnm4; sound_variant = true;
    } else if (magic_is(state.header_, "UBB2")) state.family_ = VideoFamily::hnm5;
    else if (magic_is(state.header_, "UBS2")) {
        state.family_ = VideoFamily::hnm5; sound_variant = true;
    } else if (magic_is(state.header_, "HNM6")) state.family_ = VideoFamily::hnm6;
    else if (magic_is(state.header_, "HNS6")) {
        state.family_ = VideoFamily::hnm6; sound_variant = true;
    } else {
        VID_Close(state);
        return fail(error, VideoErrorCode::unsupported_magic,
                    "video header has no retail HNM/UBB magic");
    }
    const bool sound_selected = sound_variant && state.sound_enabled_;
    state.kind_flags_ = static_cast<uint8_t>(state.family_) |
        static_cast<uint8_t>(sound_selected ? 8 : 0);
    state.sound_variant_selected_ = sound_selected; // Logical selection, no playback.
    state.source_size_ = state.stream_->source_size();
    state.path_ = std::string(path);
    return sound_selected ? 2 : 1;
}

} // namespace od::port

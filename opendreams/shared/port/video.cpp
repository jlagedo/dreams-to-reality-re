#include "port/video.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

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

uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

std::string caption_utf8(const uint8_t* bytes, size_t size) {
    std::string text;
    text.reserve(size * 2);
    for (size_t i=0; i<size; ++i) {
        const uint8_t c=bytes[i];
        if (c<0x80) text.push_back(static_cast<char>(c));
        else {
            text.push_back(static_cast<char>(0xc0 | (c >> 6)));
            text.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
    }
    return text;
}

bool read_exact(Stream& stream, size_t size, std::vector<uint8_t>& output,
                VideoError& error) {
    if (size > 0x5f000) {
        fail(error, VideoErrorCode::corrupt_chunk, "video chunk exceeds the retail stream limit");
        return false;
    }
    StreamError source;
    const uint8_t* bytes = nullptr;
    while (!STRM_Peek(stream, size, bytes, source)) {
        const size_t before = stream.available();
        if (!STRM_Fill(stream, source)) { source_fail(error, source); return false; }
        if (stream.available() == before) {
            fail(error, VideoErrorCode::corrupt_chunk, "video chunk ends before its declared size");
            return false;
        }
    }
    output.assign(bytes, bytes + size);
    if (!STRM_Commit(stream, source)) { source_fail(error, source); return false; }
    return true;
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
    state.total_frames_ = 0;
    state.decoded_frames_ = 0;
    state.next_chunk_word_ = 0;
    state.width_ = 0;
    state.height_ = 0;
    state.previous_index_ = 0;
    state.ended_ = false;
    state.rgb_frames_[0].clear();
    state.rgb_frames_[1].clear();
    state.dpcm_.reset();
    state.hnm5_ = Hnm5Decoder{};
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
    state.total_frames_ = le32(state.header_.data() + 16);
    state.width_ = static_cast<uint16_t>(state.header_[8] | (state.header_[9] << 8));
    state.height_ = static_cast<uint16_t>(state.header_[10] | (state.header_[11] << 8));
    state.next_chunk_word_ = le32(state.next_word_.data());
    state.ended_ = (state.next_chunk_word_ & 0x00ffffffu) == 0;
    if (state.family_ == VideoFamily::hnm6) {
        if (state.header_[7] != 16 || !state.width_ || !state.height_ ||
            state.width_ > 640 || state.height_ > 480 ||
            state.width_ % 8 || state.height_ % 8) {
            VID_Close(state);
            return fail(error, VideoErrorCode::invalid_header,
                        "HNM6 frame dimensions or pixel format are unsupported");
        }
        state.hnm6_ = Hnm6Decoder(state.width_,state.height_);
        state.rgb_frames_[0].assign(static_cast<size_t>(state.width_)*state.height_,0);
        state.rgb_frames_[1].assign(static_cast<size_t>(state.width_)*state.height_,0);
    } else if (state.family_ == VideoFamily::hnm5) {
        if (state.header_[7] != 8 || !state.width_ || !state.height_ ||
            state.width_ > 640 || state.height_ > 480) {
            VID_Close(state);
            return fail(error, VideoErrorCode::invalid_header,
                        "HNM5 frame dimensions or pixel format are unsupported");
        }
        state.hnm5_ = Hnm5Decoder(state.width_,state.height_);
    }
    return sound_selected ? 2 : 1;
}

bool VID_DecodeFrame(VideoState& state, VideoStep& step, VideoError& error) {
    error = {};
    step = {};
    if (state.family_ != VideoFamily::hnm6 && state.family_ != VideoFamily::hnm5) {
        return fail(error, VideoErrorCode::unsupported_codec,
                    "this movie needs the HNM4 animated-texture decoder port");
    }
    if (!state.stream_ || state.ended_) { step.ended = true; return true; }
    const uint32_t outer_size = state.next_chunk_word_ & 0x00ffffffu;
    if (outer_size < 4 || outer_size > 0x5f004u)
        return fail(error, VideoErrorCode::corrupt_chunk,
                    "movie outer superchunk has an invalid size");
    std::vector<uint8_t> body;
    if (!read_exact(*state.stream_, outer_size - 4, body, error)) return false;
    for (size_t offset = 0; offset < body.size();) {
        if (body.size() - offset < 8)
            return fail(error, VideoErrorCode::corrupt_chunk,
                        "HNM6 inner chunk header is truncated");
        const uint32_t chunk_size = le32(body.data() + offset) & 0x07ffffffu;
        if (chunk_size < 8 || chunk_size > body.size() - offset)
            return fail(error, VideoErrorCode::corrupt_chunk,
                        "HNM6 inner chunk size exceeds the superchunk");
        const uint8_t* payload = body.data() + offset + 8;
        const size_t payload_size = chunk_size - 8;
        const uint16_t id = static_cast<uint16_t>(body[offset + 4] | (body[offset + 5] << 8));
        if (id == 0x5849 && state.family_ == VideoFamily::hnm6) { // IX
            const unsigned dest = 1 - state.previous_index_;
            std::string decode_error;
            if (!state.hnm6_.decode(payload, payload_size,
                                    state.rgb_frames_[state.previous_index_],
                                    state.rgb_frames_[dest], decode_error))
                return fail(error, VideoErrorCode::decode_error, decode_error);
            state.previous_index_ = dest;
            ++state.decoded_frames_;
            step.image_ready = true;
        } else if (id == 0x5649 && state.family_ == VideoFamily::hnm5) { // IV
            std::string decode_error;
            if (!state.hnm5_.decode(payload,payload_size,state.rgb_frames_[0],decode_error))
                return fail(error, VideoErrorCode::decode_error, decode_error);
            state.previous_index_ = 0;
            ++state.decoded_frames_;
            step.image_ready = true;
        } else if (id == 0x4c50 && state.family_ == VideoFamily::hnm5) { // PL
            std::string decode_error;
            if (!state.hnm5_.update_palette(payload,payload_size,decode_error))
                return fail(error, VideoErrorCode::decode_error, decode_error);
        } else if (id == 0x4453 && state.sound_variant_selected_) { // SD
            std::vector<int16_t> chunk_pcm;
            std::string decode_error;
            if (!state.dpcm_.decode_sd(payload, payload_size, chunk_pcm, decode_error))
                return fail(error, VideoErrorCode::decode_error, decode_error);
            step.pcm.insert(step.pcm.end(), chunk_pcm.begin(), chunk_pcm.end());
        } else if (id == 0x5453) { // ST, retail copies a C string.
            const size_t length = std::find(payload, payload + payload_size, 0) - payload;
            if (length > 512)
                return fail(error, VideoErrorCode::corrupt_chunk,
                            "movie caption exceeds the supported text length");
            step.caption = caption_utf8(payload,length);
        }
        const size_t padded = state.family_ == VideoFamily::hnm6
            ? (static_cast<size_t>(chunk_size) + 3u) & ~size_t(3)
            : static_cast<size_t>(chunk_size);
        if (padded > body.size() - offset)
            return fail(error, VideoErrorCode::corrupt_chunk,
                        "HNM6 inner chunk padding exceeds the superchunk");
        offset += padded;
    }
    if (step.image_ready && state.family_ == VideoFamily::hnm5)
        state.hnm5_.expand(state.rgb_frames_[0]);
    // WINDREAM's HNM5 walker closes on the declared final IV frame. Some UBB2
    // files and the 3MILL HNM6 demo end at that byte with no zero size word.
    if (state.decoded_frames_ >= state.total_frames_) {
        state.ended_ = true;
        step.ended = true;
        return true;
    }
    std::vector<uint8_t> next_word;
    if (!read_exact(*state.stream_, 4, next_word, error)) return false;
    state.next_chunk_word_ = le32(next_word.data());
    state.ended_ = (state.next_chunk_word_ & 0x00ffffffu) == 0 ||
                   state.decoded_frames_ >= state.total_frames_;
    step.ended = state.ended_;
    return true;
}

} // namespace od::port

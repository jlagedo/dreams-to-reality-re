#pragma once

#include "port/stream.h"
#include "port/hnm6.h"
#include "port/hnm5.h"
#include "port/video_audio.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

enum class VideoErrorCode {
    none,
    missing_file,
    truncated_header,
    invalid_header,
    unsupported_magic,
    source_error,
    corrupt_chunk,
    unsupported_codec,
    decode_error,
};

struct VideoError {
    VideoErrorCode code = VideoErrorCode::none;
    std::string message;
    StreamError stream;
    explicit operator bool() const { return code != VideoErrorCode::none; }
};

enum class VideoFamily : uint8_t { none = 0, hnm4 = 1, hnm5 = 2, hnm6 = 4 };

struct VideoStep {
    bool image_ready = false;
    bool ended = false;
    std::vector<int16_t> pcm;
    std::string caption;
};

// Retail's selected video kind and 64-byte header plus the next four bytes,
// scoped to one selected VFS source. The VfsContext must outlive this state.
class VideoState {
public:
    explicit VideoState(VfsContext& vfs, bool sound_enabled = false)
        : vfs_(&vfs), sound_enabled_(sound_enabled) {}
    ~VideoState();
    VideoState(const VideoState&) = delete;
    VideoState& operator=(const VideoState&) = delete;

    VideoFamily family() const { return family_; }
    uint8_t kind_flags() const { return kind_flags_; }
    bool sound_enabled() const { return sound_enabled_; }
    bool sound_variant_selected() const { return sound_variant_selected_; }
    void set_sound_enabled(bool value) { sound_enabled_ = value; }
    const std::array<uint8_t, 64>& header() const { return header_; }
    const std::array<uint8_t, 4>& next_word() const { return next_word_; }
    uint64_t source_size() const { return source_size_; }
    std::string_view path() const { return path_; }
    bool stream_open() const { return stream_ && stream_->is_open(); }
    uint32_t total_frames() const { return total_frames_; }
    uint32_t decoded_frames() const { return decoded_frames_; }
    uint16_t width() const { return width_; }
    uint16_t height() const { return height_; }
    bool ended() const { return ended_; }
    const std::vector<uint16_t>& rgb565_frame() const { return rgb_frames_[previous_index_]; }

private:
    VfsContext* vfs_ = nullptr;
    std::unique_ptr<Stream> stream_;
    std::array<uint8_t, 64> header_{};
    std::array<uint8_t, 4> next_word_{};
    std::string path_;
    uint64_t source_size_ = 0;
    VideoFamily family_ = VideoFamily::none;
    uint8_t kind_flags_ = 0;
    bool sound_enabled_ = false;
    bool sound_variant_selected_ = false;
    uint32_t total_frames_ = 0, decoded_frames_ = 0, next_chunk_word_ = 0;
    uint16_t width_ = 0, height_ = 0;
    unsigned previous_index_ = 0;
    bool ended_ = false;
    std::array<std::vector<uint16_t>, 2> rgb_frames_;
    VideoDpcm dpcm_;
    Hnm6Decoder hnm6_;
    Hnm5Decoder hnm5_;

    friend int VID_Open(VideoState&, std::string_view, VideoError&);
    friend void VID_Close(VideoState&);
    friend bool VID_DecodeFrame(VideoState&, VideoStep&, VideoError&);
};

// Retail returns 0 on failure, 1 for video or disabled soundtrack, and 2
// when an S-variant is selected with sound enabled. Decode/playback is outside
// 002; the raw header and exact retail kind bits remain available to the caller.
int VID_Open(VideoState& state, std::string_view path, VideoError& error);
void VID_Close(VideoState& state);
bool VID_DecodeFrame(VideoState& state, VideoStep& step, VideoError& error);

} // namespace od::port

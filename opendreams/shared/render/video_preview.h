#pragma once

#include "disc/image.h"
#include "audio/audio_output.h"
#include "port/video.h"
#include "render/glide_compat.h"
#include "render/movie_clock.h"

#include <SDL3/SDL.h>
#include <sokol_gfx.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace od {

// Viewer destination for the retail VID_* stream and 3dfx LFB presentation.
// The decoded frame and audio stay source-scoped; only a frozen RGBA image is
// uploaded to sokol, once per Shell frame.
class VideoPreview {
public:
    bool init(std::string& error);
    bool open(std::shared_ptr<const disc::Image> image,
              const std::string& physical_path, std::string& error);
    void close();
    void shutdown();
    bool tick(std::string& error);
    bool single_step(std::string& error);
    bool restart(std::string& error);
    // Viewer-only bounded seek: replays from the start (retail has no seek),
    // decoding at most a bounded number of frames per tick without presenting
    // them, and keeps the SD audio decoded on the way so playback resumes in
    // sync. The target is clamped to the last frame.
    bool seek(uint64_t frame, std::string& error);
    bool seeking() const { return seek_target_ != no_seek; }
    uint64_t shown_frames() const { return shown_images_; }
    void set_playing(bool value);
    void upload(); // Call once from the viewer render iteration.

    bool has_video() const { return video_ != nullptr; }
    bool has_image() const { return has_image_; }
    bool playing() const { return playing_; }
    // True once the last decoded frame has been shown for its 1/15 s.
    bool ended() const { return video_ && presentation_ended_; }
    uint32_t decoded_frames() const { return video_ ? video_->decoded_frames() : 0; }
    uint32_t total_frames() const { return video_ ? video_->total_frames() : 0; }
    const std::string& caption() const { return caption_; }
    // The movie SD stream is retail's 22 kHz stereo streaming channel.
    AudioOutput& audio() { return audio_; }
    sg_view texture_view() const { return texture_view_; }

private:
    bool present(const std::vector<uint16_t>& rgb565, uint16_t picture_width,
                 uint16_t picture_height, std::string& error);
    bool open_session(std::shared_ptr<const disc::Image> image,
                      const std::string& physical_path, bool show_first,
                      std::string& error);
    // show=false decodes without presenting; divert_audio keeps SD samples in
    // the seek buffer instead of queueing them (a seek's replay).
    bool decode_image(std::string& error, bool show = true, bool divert_audio = false);
    bool continue_seek(std::string& error);
    bool present_indexed(std::string& error);
    uint64_t played_samples() const { return audio_.active() ? audio_.played_frames() : 0; }
    std::shared_ptr<const disc::Image> image_;
    std::string path_;
    std::unique_ptr<port::VfsContext> vfs_;
    std::unique_ptr<port::VideoState> video_;
    GlideCompat glide_;
    AudioOutput audio_;
    sg_image texture_{};
    sg_view texture_view_{};
    std::vector<uint8_t> rgba_;
    std::string caption_;
    MovieClock clock_;
    uint64_t shown_images_ = 0;
    uint64_t open_ns_ = 0;
    static constexpr uint64_t no_seek = UINT64_MAX;
    uint64_t seek_target_ = no_seek;
    bool seek_resume_ = false;
    // SD samples (interleaved int16) decoded during a seek, starting at
    // sample index seek_audio_start_; frame k's audio starts at k*1470*2.
    std::vector<int16_t> seek_audio_;
    uint64_t seek_audio_start_ = 0;
    uint64_t decoded_samples_ = 0;
    bool audio_flushed_ = false;
    bool has_image_ = false;
    bool image_dirty_ = false;
    bool playing_ = false;
    bool presentation_ended_ = false;
};

} // namespace od

#pragma once

#include "disc/image.h"
#include "audio/audio_output.h"
#include "port/video.h"
#include "render/glide_compat.h"

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
    void set_playing(bool value);
    void upload(); // Call once from the viewer render iteration.

    bool has_video() const { return video_ != nullptr; }
    bool has_image() const { return has_image_; }
    bool playing() const { return playing_; }
    bool ended() const { return video_ && video_->ended(); }
    uint32_t decoded_frames() const { return video_ ? video_->decoded_frames() : 0; }
    uint32_t total_frames() const { return video_ ? video_->total_frames() : 0; }
    const std::string& caption() const { return caption_; }
    sg_view texture_view() const { return texture_view_; }

private:
    bool present(const std::vector<uint16_t>& rgb565, uint16_t picture_width,
                 uint16_t picture_height, std::string& error);
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
    uint64_t next_tick_ns_ = 0;
    bool has_image_ = false;
    bool image_dirty_ = false;
    bool playing_ = false;
};

} // namespace od

#include "render/video_preview.h"

#include <algorithm>
#include <cstring>

namespace od {

bool VideoPreview::init(std::string& error) {
    error.clear();
    sg_image_desc desc{};
    desc.width = 640;
    desc.height = 480;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.usage.dynamic_update = true;
    desc.label = "HNM movie canvas";
    texture_ = sg_make_image(&desc);
    sg_view_desc view_desc{};
    view_desc.texture.image = texture_;
    texture_view_ = sg_make_view(&view_desc);
    if (sg_query_image_state(texture_) != SG_RESOURCESTATE_VALID ||
        sg_query_view_state(texture_view_) != SG_RESOURCESTATE_VALID) {
        error = "sokol could not create the movie texture";
        shutdown();
        return false;
    }
    rgba_.resize(640u * 480u * 4u);
    return true;
}

bool VideoPreview::open(std::shared_ptr<const disc::Image> image,
                        const std::string& physical_path, std::string& error) {
    close();
    error.clear();
    if (!image) { error = "movie source disc is unavailable"; return false; }
    image_ = std::move(image);
    path_ = physical_path;
    vfs_ = std::make_unique<port::VfsContext>(image_);
    video_ = std::make_unique<port::VideoState>(*vfs_, true);
    port::VideoError port_error;
    if (!port::VID_Open(*video_, path_, port_error)) {
        error = port_error.message;
        close();
        return false;
    }
    if (video_->family() == port::VideoFamily::hnm4) {
        error = "this animated texture needs the HNM4 frame decoder port";
        close();
        return false;
    }
    if (video_->sound_variant_selected()) {
        if (!audio_.start_pcm_stream(22050,2,16,error)) {
            close();
            return false;
        }
    }
    playing_ = true;
    if (!single_step(error)) return false;
    next_tick_ns_ = SDL_GetTicksNS() + 1000000000ull/15ull;
    return true;
}

void VideoPreview::close() {
    playing_ = false;
    audio_.stop();
    video_.reset();
    vfs_.reset();
    image_.reset();
    path_.clear();
    caption_.clear();
    has_image_ = false;
    image_dirty_ = false;
    next_tick_ns_ = 0;
}

void VideoPreview::shutdown() {
    close();
    if (texture_view_.id) sg_destroy_view(texture_view_);
    if (texture_.id) sg_destroy_image(texture_);
    texture_view_ = {};
    texture_ = {};
    rgba_.clear();
}

bool VideoPreview::present(const std::vector<uint16_t>& frame,
                           uint16_t picture_width, uint16_t picture_height,
                           std::string& error) {
    if (!picture_width || !picture_height || picture_width>640 ||
        picture_height>480 ||
        frame.size()!=static_cast<size_t>(picture_width)*picture_height) {
        error = "decoded movie dimensions exceed the 640x480 canvas";
        return false;
    }
    GlideCompat::LfbInfo lfb;
    if (!glide_.grLfbLock(lfb,error)) return false;
    if (!glide_.grClipWindow(0,0,640,480,error) ||
        !glide_.grBufferClear(0,error)) {
        std::string ignored;
        glide_.grLfbUnlock(ignored);
        return false;
    }
    // The game movie path uses the 3dfx 300-row crop at row 90. Disc demo
    // variants have other sizes and are centered in the same 640x480 canvas.
    auto* bytes = reinterpret_cast<uint8_t*>(lfb.pixels);
    const uint32_t pitch = glide_.lfb_pitch_bytes();
    const bool game_movie = picture_width==640 && picture_height==304;
    const uint32_t left = (640-picture_width)/2;
    const uint32_t top = game_movie ? 90 : (480-picture_height)/2;
    const uint32_t rows = game_movie ? 300 : picture_height;
    for (uint32_t y=0; y<rows; ++y)
        std::memcpy(bytes + (y+top)*pitch + left*sizeof(uint16_t),
                    frame.data()+static_cast<size_t>(y)*picture_width,
                    picture_width*sizeof(uint16_t));
    if (!glide_.grLfbUnlock(error) || !glide_.grBufferSwap(error)) return false;
    const auto& pixels = glide_.front_buffer();
    for (size_t i=0; i<pixels.size(); ++i) {
        const uint16_t c=pixels[i];
        rgba_[i*4]=static_cast<uint8_t>(((c>>11)&31)*255/31);
        rgba_[i*4+1]=static_cast<uint8_t>(((c>>5)&63)*255/63);
        rgba_[i*4+2]=static_cast<uint8_t>((c&31)*255/31);
        rgba_[i*4+3]=255;
    }
    has_image_ = true;
    image_dirty_ = true;
    return true;
}

void VideoPreview::upload() {
    if (!image_dirty_ || !texture_.id) return;
    sg_image_data data{};
    data.mip_levels[0].ptr = rgba_.data();
    data.mip_levels[0].size = rgba_.size();
    sg_update_image(texture_, &data);
    image_dirty_ = false;
}

bool VideoPreview::single_step(std::string& error) {
    error.clear();
    if (!video_) { error = "no movie is open"; return false; }
    if (video_->ended()) { playing_ = false; return true; }
    port::VideoStep step;
    port::VideoError port_error;
    if (!port::VID_DecodeFrame(*video_,step,port_error)) {
        error = port_error.message;
        close();
        return false;
    }
    if (step.image_ready && !present(video_->rgb565_frame(),video_->width(),
                                     video_->height(),error)) {
        close();
        return false;
    }
    if (!step.caption.empty()) caption_ = std::move(step.caption);
    if (audio_.active() && !step.pcm.empty() &&
        !audio_.queue_pcm(step.pcm.data(),step.pcm.size()*sizeof(int16_t),error)) {
        close();
        return false;
    }
    if (step.ended) playing_ = false;
    return true;
}

bool VideoPreview::tick(std::string& error) {
    error.clear();
    if (!playing_ || !video_ || video_->ended()) return true;
    const uint64_t now = SDL_GetTicksNS();
    if (now < next_tick_ns_) return true;
    next_tick_ns_ = now + 1000000000ull/15ull;
    return single_step(error);
}

bool VideoPreview::restart(std::string& error) {
    auto image = image_;
    auto path = path_;
    if (!image) { error = "no movie is open"; return false; }
    return open(std::move(image),path,error);
}

void VideoPreview::set_playing(bool value) {
    playing_ = value && video_ && !video_->ended();
    if (playing_) next_tick_ns_ = SDL_GetTicksNS() + 1000000000ull/15ull;
    if (audio_.active()) audio_.set_paused(!playing_);
}

} // namespace od

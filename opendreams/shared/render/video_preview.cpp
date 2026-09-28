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
    return open_session(std::move(image), physical_path, true, error);
}

bool VideoPreview::open_session(std::shared_ptr<const disc::Image> image,
                                const std::string& physical_path, bool show_first,
                                std::string& error) {
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
    if (video_->sound_variant_selected()) {
        if (!audio_.start_pcm_stream(22050,2,16,error)) {
            close();
            return false;
        }
    }
    playing_ = true;
    presentation_ended_ = false;
    shown_images_ = 0;
    decoded_samples_ = 0;
    seek_target_ = no_seek;
    seek_audio_.clear();
    seek_audio_start_ = 0;
    // Frame 0 is due immediately; its superchunk also queues the 1 s SD preload.
    if (!decode_image(error, show_first, !show_first)) return false;
    open_ns_ = SDL_GetTicksNS();
    clock_.start(open_ns_, 0, played_samples());
    audio_flushed_ = false;
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
    presentation_ended_ = false;
    shown_images_ = 0;
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

// Viewer display of an HNM4 animated texture: retail writes these 256x256
// indices into the bound material; the preview expands them through the
// decoded PL palette (six-bit channels stored as v << 2) into RGB565.
bool VideoPreview::present_indexed(std::string& error) {
    const auto& decoder = video_->hnm4();
    const auto& indices = decoder.texture();
    const auto& palette = decoder.palette();
    const size_t count = static_cast<size_t>(decoder.width()) * decoder.height();
    if (indices.size() != count) {
        error = "HNM4 texture size differs from its header";
        close();
        return false;
    }
    std::vector<uint16_t> frame(count);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* rgb = palette.data() + indices[i] * 3;
        frame[i] = static_cast<uint16_t>(((rgb[0] >> 3) << 11) |
                                         ((rgb[1] >> 2) << 5) | (rgb[2] >> 3));
    }
    if (!present(frame, decoder.width(), decoder.height(), error)) {
        close();
        return false;
    }
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

// Decodes superchunks until one produces a picture (or the stream ends), and
// queues every SD chunk it meets. Each picture is the next 1/15 s frame.
bool VideoPreview::decode_image(std::string& error, bool show, bool divert_audio) {
    error.clear();
    if (!video_) { error = "no movie is open"; return false; }
    while (!video_->ended()) {
        port::VideoStep step;
        port::VideoError port_error;
        if (!port::VID_DecodeFrame(*video_,step,port_error)) {
            error = port_error.message;
            close();
            return false;
        }
        if (!step.caption.empty()) caption_ = std::move(step.caption);
        if (divert_audio) {
            // Seeking: keep about one second of SD audio, the file's lead.
            seek_audio_.insert(seek_audio_.end(), step.pcm.begin(), step.pcm.end());
            constexpr size_t keep = 2 * MovieClock::samples_per_frame * 32;
            if (seek_audio_.size() > keep) {
                const size_t drop = seek_audio_.size() - keep;
                seek_audio_.erase(seek_audio_.begin(),
                                  seek_audio_.begin() + static_cast<std::ptrdiff_t>(drop));
                seek_audio_start_ += drop;
            }
        } else if (audio_.active() && !step.pcm.empty() &&
            !audio_.queue_pcm(step.pcm.data(),step.pcm.size()*sizeof(int16_t),error)) {
            close();
            return false;
        }
        decoded_samples_ += step.pcm.size();
        if (video_->family() == port::VideoFamily::hnm4) {
            // Every I? chunk is one texture frame, even when its kind leaves
            // the indices unchanged; silent HNM4 follows the 15 Hz pump.
            if (show && step.image_ready && !present_indexed(error)) return false;
            ++shown_images_;
            return true;
        }
        if (step.image_ready && !show) {
            ++shown_images_;
            return true;
        }
        if (step.image_ready) {
            if (!present(video_->rgb565_frame(),video_->width(),video_->height(),error)) {
                close();
                return false;
            }
            ++shown_images_;
            return true;
        }
    }
    return true;
}

bool VideoPreview::single_step(std::string& error) {
    error.clear();
    if (!video_) { error = "no movie is open"; return false; }
    if (video_->ended()) {
        presentation_ended_ = true;
        playing_ = false;
        return true;
    }
    if (!decode_image(error)) return false;
    if (shown_images_) clock_.start(SDL_GetTicksNS(),shown_images_-1,played_samples());
    return true;
}

bool VideoPreview::seek(uint64_t frame, std::string& error) {
    error.clear();
    if (!video_) { error = "no movie is open"; return false; }
    const uint64_t last = video_->total_frames() ? video_->total_frames() - 1 : 0;
    const uint64_t target = frame < last ? frame : last;
    const bool resume = seeking() ? seek_resume_ : playing_;
    // Replay from the start: frame 0's superchunk holds the first second of
    // SD audio, so every target's audio passes through the seek buffer.
    auto image = image_;
    auto path = path_;
    if (!open_session(std::move(image), path, false, error)) return false;
    playing_ = false;
    if (audio_.active()) audio_.set_paused(true);
    seek_target_ = target;
    seek_resume_ = resume;
    return continue_seek(error);
}

bool VideoPreview::continue_seek(std::string& error) {
    // Bounded work per tick keeps the viewer responsive on long movies.
    for (int decoded = 0; decoded < 30 && !video_->ended() &&
                          shown_images_ < seek_target_; ++decoded)
        if (!decode_image(error, false, true)) return false;
    if (!video_->ended() && shown_images_ < seek_target_) return true;
    // Present the target frame itself (frame 0 was consumed by the reopen),
    // then queue its audio from the kept SD.
    if (seek_target_ == 0) {
        const bool shown = video_->family() == port::VideoFamily::hnm4 ?
            present_indexed(error) :
            present(video_->rgb565_frame(),video_->width(),video_->height(),error);
        if (!shown) return false;
    } else if (!video_->ended() && !decode_image(error, true, true)) return false;
    const uint64_t frame = shown_images_ ? shown_images_ - 1 : 0;
    if (audio_.active()) {
        const uint64_t from = frame * MovieClock::samples_per_frame * 2;
        if (from >= seek_audio_start_ && from - seek_audio_start_ < seek_audio_.size()) {
            const size_t offset = static_cast<size_t>(from - seek_audio_start_);
            if (!audio_.queue_pcm(seek_audio_.data() + offset,
                                  (seek_audio_.size() - offset) * sizeof(int16_t), error))
                return false;
        }
        seek_audio_.clear();
    }
    seek_target_ = no_seek;
    clock_.start(SDL_GetTicksNS(), frame, played_samples());
    set_playing(seek_resume_);
    return true;
}

bool VideoPreview::tick(std::string& error) {
    error.clear();
    if (video_ && seeking()) return continue_seek(error);
    if (!playing_ || !video_ || presentation_ended_) return true;
    // Audio drives the clock while at least one frame of SD samples is queued;
    // the drained tail (INTRO.HNM has 48 more frames than SD chunks) and any
    // underrun continue on 15 Hz host time.
    const bool audio_flowing = audio_.active() &&
        audio_.queued_frames() >= MovieClock::samples_per_frame;
    const uint64_t due = clock_.due(SDL_GetTicksNS(),audio_flowing,played_samples());
    // Catch up at most a few frames per display frame; decoding is sequential.
    for (int decoded = 0; decoded < 8 && !video_->ended() && shown_images_ <= due;
         ++decoded)
        if (!decode_image(error)) return false;
    if (video_->ended() && !audio_flushed_) {
        audio_.flush();
        audio_flushed_ = true;
    }
    if (video_->ended() && due >= shown_images_) {
        // SDL_LOGGING=app=debug reports playback drift for acceptance checks.
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "movie %s ended: %llu frames, %.3f s wall, %.3f s audio",
                     path_.c_str(), static_cast<unsigned long long>(shown_images_),
                     static_cast<double>(SDL_GetTicksNS() - open_ns_) / 1e9,
                     static_cast<double>(played_samples()) / 22050.0);
        presentation_ended_ = true;
        playing_ = false;
    }
    return true;
}

bool VideoPreview::restart(std::string& error) {
    auto image = image_;
    auto path = path_;
    if (!image) { error = "no movie is open"; return false; }
    return open(std::move(image),path,error);
}

void VideoPreview::set_playing(bool value) {
    playing_ = value && video_ && !presentation_ended_;
    if (audio_.active()) audio_.set_paused(!playing_);
    if (playing_ && shown_images_)
        clock_.start(SDL_GetTicksNS(),shown_images_-1,played_samples());
}

} // namespace od

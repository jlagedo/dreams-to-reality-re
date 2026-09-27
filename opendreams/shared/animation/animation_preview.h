#pragma once

#include "disc/image.h"
#include "port/animation.h"
#include "port/dan.h"
#include "render/model_preview.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace od {

// Source-scoped DAN clip inspector. Decoding and pose evaluation are retail
// ports; time controls and the preview root-motion policy are viewer-owned.
class AnimationPreview {
public:
    bool open(std::shared_ptr<const disc::Image> image,
              std::string_view archive_path, const port::ModelGraph& bind,
              ModelPreview& renderer, std::string& error);
    bool select(size_t index, std::string& error);
    bool tick(double elapsed_seconds, std::string& error);
    bool step(std::string& error);
    bool seek(float frame, std::string& error);
    bool restart(std::string& error);
    void set_follow_root(bool enabled) { follow_root_=enabled; dirty_=true; }
    bool flush(ModelPreview& renderer, std::string& error);
    void close();

    bool active() const { return archive_!=nullptr; }
    bool has_clip() const { return clip_loaded_; }
    bool playing() const { return playing_; }
    void set_playing(bool enabled) { playing_=enabled && clip_loaded_; }
    bool looping() const { return looping_; }
    void set_looping(bool enabled) { looping_=enabled; }
    bool follow_root() const { return follow_root_; }
    size_t clip_count() const { return archive_ ? archive_->clips().size() : 0; }
    size_t selected_index() const { return selected_index_; }
    std::string_view clip_name(size_t index) const;
    float frame() const { return frame_; }
    uint32_t duration() const { return clip_.duration; }
    size_t track_count() const { return clip_.tracks.size(); }
    uint32_t resource_type() const { return clip_.resource_type; }

private:
    std::unique_ptr<port::VfsContext> vfs_;
    std::unique_ptr<port::DanArchive> archive_;
    port::ModelGraph bind_;
    port::ModelGraph pose_;
    port::AnimationClip clip_;
    size_t selected_index_ = SIZE_MAX;
    float frame_ = 0;
    bool clip_loaded_ = false;
    bool playing_ = false;
    bool looping_ = true;
    bool follow_root_ = false;
    bool dirty_ = false;

    bool sample(ModelPreview& renderer, std::string& error);
};

} // namespace od

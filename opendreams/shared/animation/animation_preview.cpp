#include "animation/animation_preview.h"

#include "port/resource.h"

#include <algorithm>
#include <cmath>

namespace od {

bool AnimationPreview::open(std::shared_ptr<const disc::Image> image,
                            std::string_view archive_path,
                            const port::ModelGraph& bind,
                            ModelPreview& renderer, std::string& error) {
    close();
    error.clear();
    if (!image || bind.nodes.empty() || !renderer.has_model()) {
        error="animation preview needs a loaded model and source";
        return false;
    }
    vfs_=std::make_unique<port::VfsContext>(std::move(image));
    archive_=std::make_unique<port::DanArchive>(*vfs_);
    port::DanError source;
    if (!port::DAN_OpenArchive(*archive_,archive_path,source) ||
        !port::DAN_ReadAnimChunks(*archive_,source)) {
        error=source.message;
        close();
        return false;
    }
    bind_=bind;
    pose_=bind;
    if (archive_->clips().empty()) return true;
    return select(0,error);
}

bool AnimationPreview::select(size_t index, std::string& error) {
    error.clear();
    playing_=false;
    clip_loaded_=false;
    dirty_=true;
    if (!archive_ || index>=archive_->clips().size()) {
        error="animation clip index is outside its DAN directory";
        return false;
    }
    selected_index_=index;
    std::vector<uint8_t> bytes;
    if (!port::RES_ReadFile(*archive_,archive_->clips()[index].text,bytes,error))
        return false;
    if (!port::ANIM_DecodeClip(bytes,archive_->clips()[index].text,clip_,error))
        return false;
    if (clip_.tracks.size()!=bind_.nodes.size()) {
        error="clip track count differs from the model node directory";
        return false;
    }
    clip_loaded_=true;
    frame_=clip_.duration ? std::min(1.0f,static_cast<float>(clip_.duration)) : 0;
    dirty_=true;
    return true;
}

bool AnimationPreview::sample(ModelPreview& renderer, std::string& error) {
    error.clear();
    if (!clip_loaded_) {
        error="no animation clip is selected";
        return false;
    }
    const bool applied=clip_.resource_type==4 ?
        port::ANIM_ApplyModelLinear(clip_,frame_,bind_,pose_,follow_root_,error) :
        port::ANIM_ApplyModelSpline(clip_,frame_,bind_,pose_,follow_root_,error);
    return applied && renderer.update_pose(pose_,error);
}

bool AnimationPreview::tick(double elapsed_seconds, std::string& error) {
    error.clear();
    if (!playing_ || !clip_loaded_) return true;
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds<0)
        return (error="animation elapsed time is invalid",false);
    if (clip_.duration<=1) { playing_=false; return true; }
    frame_+=static_cast<float>(std::min(elapsed_seconds,0.25)*30.0);
    if (frame_>=clip_.duration) {
        if (looping_) {
            const float span=static_cast<float>(clip_.duration)-1.0f;
            frame_=1.0f+std::fmod(frame_-1.0f,span);
        } else {
            frame_=static_cast<float>(clip_.duration);
            playing_=false;
        }
    }
    dirty_=true;
    return true;
}

bool AnimationPreview::seek(float frame, std::string& error) {
    error.clear();
    if (!clip_loaded_) { error="no animation clip is selected"; return false; }
    if (!std::isfinite(frame)) { error="animation frame is not finite"; return false; }
    playing_=false;
    frame_=std::clamp(frame,0.0f,static_cast<float>(clip_.duration));
    dirty_=true;
    return true;
}

bool AnimationPreview::step(std::string& error) {
    if (!clip_loaded_) { error="no animation clip is selected"; return false; }
    float next=std::floor(frame_)+1.0f;
    if (next>clip_.duration)
        next=looping_ && clip_.duration>1 ? 1.0f : static_cast<float>(clip_.duration);
    return seek(next,error);
}

bool AnimationPreview::restart(std::string& error) {
    if (!clip_loaded_) { error="no animation clip is selected"; return false; }
    return seek(clip_.duration ? 1.0f : 0.0f,error);
}

bool AnimationPreview::flush(ModelPreview& renderer, std::string& error) {
    if (!dirty_) return true;
    if (clip_loaded_) {
        if (!sample(renderer,error)) return false;
    } else if (!bind_.nodes.empty() && !renderer.update_pose(bind_,error)) {
        return false;
    }
    dirty_=false;
    return true;
}

std::string_view AnimationPreview::clip_name(size_t index) const {
    return archive_ && index<archive_->clips().size() ?
        std::string_view(archive_->clips()[index].text) : std::string_view{};
}

void AnimationPreview::close() {
    archive_.reset();
    vfs_.reset();
    bind_={}; pose_={}; clip_={};
    selected_index_=SIZE_MAX;
    frame_=0;
    clip_loaded_=false;
    playing_=false;
    looping_=true;
    follow_root_=false;
    dirty_=false;
}

} // namespace od

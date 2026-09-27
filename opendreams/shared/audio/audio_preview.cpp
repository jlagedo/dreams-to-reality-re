#include "audio/audio_preview.h"

#include "port/drd.h"
#include "port/fsb.h"
#include "port/sound.h"

#include <algorithm>
#include <utility>

namespace od {

void AudioPreview::stop() {
    if (kind_==Kind::dialogue) port::DRD_StopVoice(output_);
    else if (kind_==Kind::cd_track) port::CD_StopAudio(output_);
    else output_.stop();
    image_.reset();
    kind_=Kind::none;
    captions_.clear();
    portrait_={};
    portrait_note_.clear();
    entry_duration_ticks_=0;
    repaired_header_=false;
}

bool AudioPreview::open_sound(std::shared_ptr<const disc::Image> image,
                              const std::string& physical_path, size_t clip,
                              std::string& error) {
    stop(); error.clear();
    if (!image) { error="sound source disc is unavailable"; return false; }
    image_=std::move(image);
    port::VfsContext vfs(image_);
    port::FsbBank bank(vfs);
    port::FsbError source;
    if (!port::FSB_Load(bank,physical_path,source)) {
        error=source.message; stop(); return false;
    }
    if (!port::DSOUND_PlaySound(bank,clip,output_,repaired_header_,error)) {
        stop(); return false;
    }
    kind_=Kind::effect;
    return true;
}

bool AudioPreview::open_dialogue(std::shared_ptr<const disc::Image> image,
                                 const std::string& physical_path, size_t entry,
                                 std::string& error) {
    stop(); error.clear();
    if (!image) { error="dialogue source disc is unavailable"; return false; }
    image_=std::move(image);
    port::VfsContext vfs(image_);
    port::DrdBank bank(vfs);
    port::DrdError source;
    if (!port::DRD_Open(bank,physical_path,source) ||
        !port::DRD_SelectEntry(bank,entry,source)) {
        error=source.message; stop(); return false;
    }
    entry_duration_ticks_=port::DRD_GetEntryDuration(bank);
    for (size_t i=0; i<port::DRD_GetLineCount(bank); ++i) {
        captions_.push_back({bank.lines()[i].ticks_15hz,
                             port::DRD_GetLineDuration(bank,i),
                             std::string(bank.line_text(i))});
    }
    const port::DrdBytes portrait=port::DRD_GetPortrait(bank);
    if (portrait.data) {
        port::SpriteError sprite_error;
        if (!port::SPR_LoadPortrait(portrait.data,portrait.size,portrait_,sprite_error))
            portrait_note_=sprite_error.message;
    }
    if (!port::DRD_PlayVoice(bank,output_,error)) { stop(); return false; }
    kind_=Kind::dialogue;
    return true;
}

bool AudioPreview::open_track(std::shared_ptr<const disc::Image> image,
                              unsigned number, std::string& error) {
    stop(); error.clear();
    if (!image) { error="CD source disc is unavailable"; return false; }
    image_=std::move(image);
    if (!port::CD_PlayTrack(image_,number,output_,error)) { stop(); return false; }
    kind_=Kind::cd_track;
    return true;
}

bool AudioPreview::tick(std::string& error) {
    if (!output_.tick(error)) { stop(); return false; }
    return true;
}

size_t AudioPreview::current_caption_index() const {
    if (kind_!=Kind::dialogue || captions_.empty()) return SIZE_MAX;
    const uint64_t tick=static_cast<uint64_t>(position_seconds()*15.0);
    size_t selected=SIZE_MAX;
    for (size_t i=0; i<captions_.size(); ++i) {
        if (captions_[i].start_tick>tick) break;
        selected=i;
    }
    return selected;
}

} // namespace od

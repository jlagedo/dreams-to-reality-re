#pragma once

#include "audio/audio_output.h"
#include "port/sprite.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace od {

struct DialogueCaption {
    uint32_t start_tick = 0;
    uint32_t duration_ticks = 0;
    std::string text;
};

class AudioPreview {
public:
    enum class Kind { none, effect, dialogue, cd_track };
    bool open_sound(std::shared_ptr<const disc::Image> image,
                    const std::string& physical_path, size_t clip,
                    std::string& error);
    bool open_dialogue(std::shared_ptr<const disc::Image> image,
                       const std::string& physical_path, size_t entry,
                       std::string& error);
    bool open_track(std::shared_ptr<const disc::Image> image,
                    unsigned number, std::string& error);
    bool tick(std::string& error);
    bool restart(std::string& error) { return output_.restart(error); }
    void set_paused(bool paused) { output_.set_paused(paused); }
    void stop();

    Kind kind() const { return kind_; }
    bool active() const { return output_.active(); }
    bool paused() const { return output_.paused(); }
    bool ended() const { return output_.ended(); }
    double position_seconds() const { return output_.position_seconds(); }
    double duration_seconds() const { return output_.duration_seconds(); }
    uint32_t rate() const { return output_.rate(); }
    uint16_t channels() const { return output_.channels(); }
    uint16_t bits() const { return output_.bits(); }
    const std::vector<float>& waveform() const { return output_.waveform(); }
    bool repaired_header() const { return repaired_header_; }
    const std::vector<DialogueCaption>& captions() const { return captions_; }
    uint32_t entry_duration_ticks() const { return entry_duration_ticks_; }
    const port::PortraitSprite& portrait() const { return portrait_; }
    bool has_portrait() const { return portrait_.width != 0; }
    const std::string& portrait_note() const { return portrait_note_; }
    size_t current_caption_index() const;

private:
    std::shared_ptr<const disc::Image> image_;
    AudioOutput output_;
    Kind kind_ = Kind::none;
    std::vector<DialogueCaption> captions_;
    port::PortraitSprite portrait_;
    std::string portrait_note_;
    uint32_t entry_duration_ticks_ = 0;
    bool repaired_header_ = false;
};

} // namespace od

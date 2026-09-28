#pragma once

#include <cstdint>

namespace od {

// Presentation clock for 15 Hz HNM movies. Retail paces sound movies from the
// 200 Hz timer (VID_IsFrameDue); the port follows played SD audio instead, as
// each SD chunk holds 1470 stereo sample frames (1/15 s at 22050 Hz) and the
// first superchunk preloads 15 of them. While the device has no queued audio
// (silent movie, underrun, or drained tail) the clock continues from the last
// audio-derived frame at 15 Hz host time.
class MovieClock {
public:
    static constexpr uint64_t samples_per_frame = 22050 / 15;
    static constexpr uint64_t frame_ns = 1000000000ull / 15;

    // frame is the index due at now_ns; played_samples is the device count at
    // that moment, so later audio advances relative to it. Called on open and
    // again after a pause, step or queue reset.
    void start(uint64_t now_ns, uint64_t frame = 0, uint64_t played_samples = 0) {
        anchor_frame_ = anchor_audio_frame_ = frame;
        anchor_ns_ = now_ns;
        audio_base_ = played_samples;
    }

    uint64_t due(uint64_t now_ns, bool audio_flowing, uint64_t played_samples) {
        if (audio_flowing && played_samples >= audio_base_) {
            const uint64_t frame = anchor_audio_frame_ +
                (played_samples - audio_base_) / samples_per_frame;
            if (frame >= anchor_frame_) {
                anchor_frame_ = frame;
                anchor_ns_ = now_ns;
            }
            return anchor_frame_;
        }
        const uint64_t elapsed = now_ns > anchor_ns_ ? now_ns - anchor_ns_ : 0;
        const uint64_t frames = elapsed / frame_ns;
        // Keep the fractional remainder so repeated calls do not drift.
        anchor_frame_ += frames;
        anchor_ns_ += frames * frame_ns;
        return anchor_frame_;
    }

private:
    uint64_t anchor_frame_ = 0;
    uint64_t anchor_ns_ = 0;
    uint64_t audio_base_ = 0;
    uint64_t anchor_audio_frame_ = 0;
};

} // namespace od

#pragma once

#include "disc/image.h"
#include "port/wave.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace od {

// One source-scoped SDL playback channel. RIFF samples use owned PCM; CD-DA
// tracks stream bounded ranges of the selected CUE backing file.
class AudioOutput {
public:
    ~AudioOutput() { stop(); }
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;
    AudioOutput() = default;

    bool play_wave(port::WavePcm pcm, std::string& error);
    bool play_track(std::shared_ptr<const disc::Image> image,
                    unsigned track_number, std::string& error);
    bool start_pcm_stream(uint32_t rate, uint16_t channels, uint16_t bits,
                          std::string& error);
    bool queue_pcm(const void* bytes, size_t size, std::string& error);
    bool tick(std::string& error);
    void set_paused(bool paused);
    bool restart(std::string& error);
    void stop();

    bool active() const { return stream_ != nullptr; }
    bool paused() const { return paused_; }
    bool ended() const { return ended_; }
    double position_seconds() const;
    double duration_seconds() const;
    uint32_t rate() const { return rate_; }
    uint16_t channels() const { return channels_; }
    uint16_t bits() const { return bits_; }
    const std::vector<float>& waveform() const { return waveform_; }

private:
    enum class Kind { none, wave, track, external_pcm };
    bool open_stream(std::string& error);
    Kind kind_ = Kind::none;
    std::shared_ptr<const disc::Image> image_;
    unsigned track_number_ = 0;
    std::vector<uint8_t> memory_;
    std::vector<float> waveform_;
    SDL_AudioStream* stream_ = nullptr;
    uint64_t total_bytes_ = 0, cursor_ = 0, submitted_bytes_ = 0;
    uint32_t rate_ = 0;
    uint16_t channels_ = 0, bits_ = 0;
    bool subsystem_ready_ = false;
    bool paused_ = true;
    bool ended_ = false;
};

} // namespace od

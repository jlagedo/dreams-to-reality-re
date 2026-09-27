#include "audio/audio_output.h"

#include <algorithm>
#include <array>
#include <climits>
#include <utility>

namespace od {

bool AudioOutput::open_stream(std::string& error) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        error = std::string("SDL audio initialization failed: ") + SDL_GetError();
        return false;
    }
    subsystem_ready_ = true;
    SDL_AudioSpec spec{};
    spec.format = bits_ == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
    spec.channels = static_cast<int>(channels_);
    spec.freq = static_cast<int>(rate_);
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                        &spec,nullptr,nullptr);
    if (!stream_) {
        error = std::string("SDL playback device failed: ") + SDL_GetError();
        stop();
        return false;
    }
    paused_ = false;
    if (!tick(error) || !SDL_ResumeAudioStreamDevice(stream_)) {
        if (error.empty()) error=std::string("SDL playback start failed: ")+SDL_GetError();
        stop();
        return false;
    }
    return true;
}

bool AudioOutput::play_wave(port::WavePcm pcm, std::string& error) {
    stop(); error.clear();
    if (!pcm.rate || !pcm.channels ||
        (pcm.bits!=8 && pcm.bits!=16) || pcm.samples.empty() ||
        !pcm.bytes_per_frame() || pcm.samples.size()%pcm.bytes_per_frame()!=0) {
        error="selected WAVE sample has no supported PCM frames";
        return false;
    }
    kind_=Kind::wave;
    rate_=pcm.rate; channels_=pcm.channels; bits_=pcm.bits;
    constexpr size_t bins=128;
    waveform_.assign(bins,0.0f);
    const size_t frames=static_cast<size_t>(pcm.frame_count());
    const size_t stride=pcm.bytes_per_frame();
    for (size_t frame=0; frame<frames; ++frame) {
        const uint8_t* sample=pcm.samples.data()+frame*stride;
        float amplitude=0.0f;
        for (size_t ch=0; ch<pcm.channels; ++ch) {
            float value=0.0f;
            if (pcm.bits==8) value=(static_cast<int>(sample[ch])-128)/128.0f;
            else {
                const size_t at=ch*2;
                const uint16_t raw=static_cast<uint16_t>(sample[at] | (sample[at+1]<<8));
                const int16_t signed_value=static_cast<int16_t>(raw);
                value=signed_value/32768.0f;
            }
            amplitude=std::max(amplitude,value<0 ? -value : value);
        }
        const size_t bin=std::min(bins-1,frame*bins/frames);
        waveform_[bin]=std::max(waveform_[bin],amplitude);
    }
    memory_=std::move(pcm.samples);
    total_bytes_=memory_.size();
    return open_stream(error);
}

bool AudioOutput::play_track(std::shared_ptr<const disc::Image> image,
                             unsigned track_number, std::string& error) {
    stop(); error.clear();
    if (!image) { error="selected CUE source is unavailable"; return false; }
    disc::Error source;
    uint64_t size=0;
    if (!image->audio_track_size(track_number,size,source)) {
        error=source.message; return false;
    }
    if (!size || size%4) { error="selected CD-DA track has no complete stereo frames"; return false; }
    kind_=Kind::track;
    image_=std::move(image);
    track_number_=track_number;
    total_bytes_=size;
    rate_=44100; channels_=2; bits_=16;
    return open_stream(error);
}

bool AudioOutput::start_pcm_stream(uint32_t rate, uint16_t channels,
                                   uint16_t bits, std::string& error) {
    stop(); error.clear();
    if (rate<8000 || rate>192000 || channels<1 || channels>2 ||
        (bits!=8 && bits!=16)) {
        error="external PCM format is outside the supported SDL range";
        return false;
    }
    kind_=Kind::external_pcm;
    rate_=rate; channels_=channels; bits_=bits;
    return open_stream(error);
}

bool AudioOutput::queue_pcm(const void* bytes, size_t size, std::string& error) {
    error.clear();
    const size_t frame=static_cast<size_t>(channels_)*bits_/8;
    if (!stream_ || kind_!=Kind::external_pcm || (size && !bytes) ||
        !frame || size%frame || size>INT_MAX ||
        total_bytes_>UINT64_MAX-size) {
        error="external PCM write is invalid or exceeds its source range";
        return false;
    }
    if (size && !SDL_PutAudioStreamData(stream_,bytes,static_cast<int>(size))) {
        error=std::string("SDL PCM queue failed: ")+SDL_GetError();
        return false;
    }
    cursor_+=size;
    submitted_bytes_+=size;
    total_bytes_+=size;
    return true;
}

bool AudioOutput::tick(std::string& error) {
    error.clear();
    if (!stream_ || paused_ || ended_) return true;
    if (kind_==Kind::external_pcm) return true;
    int queued=SDL_GetAudioStreamQueued(stream_);
    if (queued<0) { error=std::string("SDL audio queue failed: ")+SDL_GetError(); return false; }
    const uint64_t target=static_cast<uint64_t>(rate_)*channels_*(bits_/8)/2;
    std::array<uint8_t,32768> chunk{};
    while (static_cast<uint64_t>(queued)<target && cursor_<total_bytes_) {
        const size_t count=static_cast<size_t>(std::min<uint64_t>(chunk.size(),total_bytes_-cursor_));
        const uint8_t* data=nullptr;
        if (kind_==Kind::wave) data=memory_.data()+cursor_;
        else if (kind_==Kind::track) {
            disc::Error source;
            if (!image_->read_audio_track_at(track_number_,cursor_,chunk.data(),count,source)) {
                error=source.message; return false;
            }
            data=chunk.data();
        } else { error="audio output has no selected source"; return false; }
        if (!SDL_PutAudioStreamData(stream_,data,static_cast<int>(count))) {
            error=std::string("SDL audio write failed: ")+SDL_GetError();
            return false;
        }
        cursor_+=count;
        submitted_bytes_+=count;
        queued+=static_cast<int>(count);
    }
    if (cursor_==total_bytes_ && SDL_GetAudioStreamQueued(stream_)==0) {
        ended_=true;
        paused_=true;
    }
    return true;
}

void AudioOutput::set_paused(bool paused) {
    if (!stream_ || ended_) return;
    paused_=paused;
    if (paused) SDL_PauseAudioStreamDevice(stream_);
    else SDL_ResumeAudioStreamDevice(stream_);
}

bool AudioOutput::restart(std::string& error) {
    error.clear();
    if (!stream_) { error="no audio source is open"; return false; }
    if (!SDL_ClearAudioStream(stream_)) {
        error=std::string("SDL audio rewind failed: ")+SDL_GetError();
        return false;
    }
    cursor_=submitted_bytes_=0;
    ended_=false;
    paused_=false;
    if (!tick(error)) return false;
    if (!SDL_ResumeAudioStreamDevice(stream_)) {
        error=std::string("SDL audio restart failed: ")+SDL_GetError();
        return false;
    }
    return true;
}

void AudioOutput::stop() {
    if (stream_) SDL_DestroyAudioStream(stream_);
    stream_=nullptr;
    if (subsystem_ready_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    subsystem_ready_=false;
    kind_=Kind::none;
    image_.reset();
    track_number_=0;
    memory_.clear();
    waveform_.clear();
    total_bytes_=cursor_=submitted_bytes_=0;
    rate_=0; channels_=bits_=0;
    paused_=true; ended_=false;
}

double AudioOutput::position_seconds() const {
    if (!stream_ || !rate_ || !channels_ || !bits_) return 0.0;
    const int queued=SDL_GetAudioStreamQueued(stream_);
    const uint64_t waiting=queued>0 ? static_cast<uint64_t>(queued) : 0;
    const uint64_t played=submitted_bytes_>waiting ? submitted_bytes_-waiting : 0;
    const uint64_t safe=std::min(played,total_bytes_);
    return static_cast<double>(safe) /
        (static_cast<double>(rate_)*channels_*(bits_/8));
}

double AudioOutput::duration_seconds() const {
    if (!rate_ || !channels_ || !bits_) return 0.0;
    return static_cast<double>(total_bytes_) /
        (static_cast<double>(rate_)*channels_*(bits_/8));
}

} // namespace od

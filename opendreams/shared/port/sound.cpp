#include "port/sound.h"

#include "audio/audio_output.h"
#include "port/wave.h"

#include <cmath>
#include <utility>

namespace od::port {

bool DSOUND_PlaySound(const FsbBank& bank, size_t clip_index,
                      AudioOutput& output, bool& repaired_header,
                      std::string& error) {
    error.clear();
    repaired_header=false;
    const FsbSample sample=FSB_GetSample(bank,clip_index);
    if (!sample.data) { error="selected FSB clip is unavailable"; return false; }
    WavePcm pcm;
    if (!DSOUND_LoadWav(sample.data,sample.size,pcm,error)) return false;
    repaired_header=pcm.repaired_format_tag;
    return output.play_wave(std::move(pcm),error);
}

int DSOUND_SetChannelVolume(AudioOutput& channel, int master_volume, int volume) {
    // x87 FRNDINT under chop rounding (0x45943e) truncates both results.
    const int effective = static_cast<int>(
        static_cast<double>(master_volume) / 127.0 * static_cast<double>(volume));
    const double exponent = static_cast<double>(127 - effective) *
        0.02362204724409449 + 1.0;
    const int attenuation = static_cast<int>(std::pow(10.0, exponent));
    channel.set_gain(static_cast<float>(std::pow(10.0, -attenuation / 2000.0)));
    return attenuation;
}

void DSOUND_SetMasterVolume(int& master_volume, int volume,
                            AudioOutput* const* channels, size_t count) {
    master_volume = volume;
    for (size_t i = 0; i < count; ++i)
        if (channels[i]) DSOUND_SetChannelVolume(*channels[i], volume, volume);
}

bool DSOUND_PlayVoice(const DrdBank& bank, AudioOutput& output,
                      std::string& error) {
    error.clear();
    const DrdBytes wave=bank.wave();
    if (!wave.data) { error="selected dialogue has no WAVE voice"; return false; }
    WavePcm pcm;
    if (!DSOUND_LoadWav(wave.data,wave.size,pcm,error)) return false;
    return output.play_wave(std::move(pcm),error);
}

bool DRD_PlayVoice(const DrdBank& bank, AudioOutput& output,
                   std::string& error) {
    return DSOUND_PlayVoice(bank,output,error);
}

void DRD_StopVoice(AudioOutput& output) { output.stop(); }

bool CD_PlayTrack(std::shared_ptr<const disc::Image> image,
                  unsigned track_number, AudioOutput& output,
                  std::string& error) {
    return output.play_track(std::move(image),track_number,error);
}

void CD_StopAudio(AudioOutput& output) { output.stop(); }

void CD_SetPlaylist(CdPlaylist& playlist, uint32_t packed, AudioOutput& output) {
    CD_StopAudio(output);
    playlist.count = 0;
    for (int i = 0; i < 3; ++i) {
        // Retail stores a nonzero byte at its loop index, not at `count`.
        if (packed & 0xff) {
            ++playlist.count;
            playlist.tracks[static_cast<size_t>(i)] = packed & 0xff;
        }
        packed >>= 8;
    }
    playlist.index = -1;
    playlist.enabled = true;
}

void CD_StopMusic(CdPlaylist& playlist, AudioOutput& output) {
    CD_StopAudio(output);
    playlist.enabled = false;
}

bool CD_TickPlaylist(CdPlaylist& playlist, std::shared_ptr<const disc::Image> image,
                     AudioOutput& output, std::string& error) {
    error.clear();
    if (!playlist.enabled || ++playlist.pump_counter <= 14) return true;
    playlist.pump_counter = 0;
    // CD_GetMode == 4: MCI reports the drive stopped (or never started).
    if (output.active() && !output.ended()) return true;
    if (playlist.count <= 0) return true;
    if (++playlist.index >= playlist.count) playlist.index = 0;
    return CD_PlayTrack(std::move(image),
                        playlist.tracks[static_cast<size_t>(playlist.index)],
                        output, error);
}

} // namespace od::port

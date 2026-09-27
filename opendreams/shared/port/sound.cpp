#include "port/sound.h"

#include "audio/audio_output.h"
#include "port/wave.h"

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

} // namespace od::port

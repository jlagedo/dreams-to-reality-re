#pragma once

#include "disc/image.h"
#include "port/drd.h"
#include "port/fsb.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace od { class AudioOutput; }

namespace od::port {

bool DSOUND_PlaySound(const FsbBank& bank, size_t clip_index,
                      AudioOutput& output, bool& repaired_header,
                      std::string& error);
// DSOUND_SetChannelVolume (0x4476f4) with its attenuation helper at 0x44768f:
// v = trunc(master/127 * volume), then IDirectSoundBuffer::SetVolume(-A)
// with A = trunc(10^((127-v)*3/127 + 1)) hundredths of a dB. The channel
// receives the equivalent linear gain; the attenuation is returned.
int DSOUND_SetChannelVolume(AudioOutput& channel, int master_volume, int volume);

// DSOUND_SetMasterVolume (0x4465e1) stores the master volume (0x633bd0,
// DSOUND_Init leaves 127) and passes it as both master and channel volume to
// every voice and both streaming channels.
void DSOUND_SetMasterVolume(int& master_volume, int volume,
                            AudioOutput* const* channels, size_t count);

bool DSOUND_PlayVoice(const DrdBank& bank, AudioOutput& output,
                      std::string& error);
bool DRD_PlayVoice(const DrdBank& bank, AudioOutput& output,
                   std::string& error);
void DRD_StopVoice(AudioOutput& output);
bool CD_PlayTrack(std::shared_ptr<const disc::Image> image,
                  unsigned track_number, AudioOutput& output,
                  std::string& error);
void CD_StopAudio(AudioOutput& output);

// Music playlist behind MGM 0x1e/0x1f. Globals: tracks 0x626f58, count
// 0x626f78, index 0x626f6c, pump counter 0x4a2f8d; `enabled` is flag-word bit
// 0x400 (0x626f81 & 4), which gates CD_TickPlaylist in MGM_DispatchMessages.
struct CdPlaylist {
    std::array<unsigned, 3> tracks{};
    int count = 0;
    int index = -1;
    int pump_counter = 0;
    bool enabled = false;
};

// CD_SetPlaylist (0x43a9ef): stop the CD, then take up to three nonzero track
// bytes from `packed` (low byte first). The MGM 0x1e dispatch also sets the
// enable bit, which this port does here.
void CD_SetPlaylist(CdPlaylist& playlist, uint32_t packed, AudioOutput& output);
// CD_StopMusic (0x43aa71) with the MGM 0x1f enable-bit clear.
void CD_StopMusic(CdPlaylist& playlist, AudioOutput& output);
// CD_TickPlaylist (0x43aa97), once per message pump while enabled: every 15
// pumps, a stopped CD advances to the next listed track, so one track loops.
bool CD_TickPlaylist(CdPlaylist& playlist, std::shared_ptr<const disc::Image> image,
                     AudioOutput& output, std::string& error);

} // namespace od::port

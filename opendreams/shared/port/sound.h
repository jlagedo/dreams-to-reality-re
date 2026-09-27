#pragma once

#include "disc/image.h"
#include "port/drd.h"
#include "port/fsb.h"

#include <memory>
#include <string>

namespace od { class AudioOutput; }

namespace od::port {

bool DSOUND_PlaySound(const FsbBank& bank, size_t clip_index,
                      AudioOutput& output, bool& repaired_header,
                      std::string& error);
bool DSOUND_PlayVoice(const DrdBank& bank, AudioOutput& output,
                      std::string& error);
bool DRD_PlayVoice(const DrdBank& bank, AudioOutput& output,
                   std::string& error);
void DRD_StopVoice(AudioOutput& output);
bool CD_PlayTrack(std::shared_ptr<const disc::Image> image,
                  unsigned track_number, AudioOutput& output,
                  std::string& error);
void CD_StopAudio(AudioOutput& output);

} // namespace od::port

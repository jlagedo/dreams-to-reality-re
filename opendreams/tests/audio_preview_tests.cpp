#define _CRT_SECURE_NO_WARNINGS
#include "audio/audio_preview.h"
#include "disc/image.h"
#include "inspect/still_preview.h"
#include "port/drd.h"
#include "port/fsb.h"
#include "port/sprite.h"
#include "port/wave.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
uint64_t fnv(const uint8_t* bytes,size_t count) {
    uint64_t hash=14695981039346656037ull;
    for (size_t i=0;i<count;++i) hash=(hash^bytes[i])*1099511628211ull;
    return hash;
}
}

int main() {
    const char* cue=std::getenv("DREAMS_CUE1");
    if (!cue || !*cue) { std::cout << "DREAMS_CUE1 is not configured\n"; return 77; }
    od::disc::Error disc_error;
    auto opened=od::disc::Image::open(std::filesystem::u8path(cue),disc_error);
    if (!opened) { std::cerr << disc_error.message << '\n'; return 1; }
    std::shared_ptr<const od::disc::Image> image(std::move(opened));
    od::port::VfsContext vfs(image);
    od::port::FsbBank fsb(vfs);
    od::port::FsbError fsb_error;
    if (!od::port::FSB_Load(fsb,"DATA/SOUND/FSB.DAT",fsb_error)) {
        std::cerr << fsb_error.message << '\n'; return 1;
    }
    size_t repaired=0;
    for (size_t i=0;i<fsb.clips().size();++i) {
        const auto sample=od::port::FSB_GetSample(fsb,i);
        od::port::WavePcm pcm;
        std::string error;
        if (!od::port::DSOUND_LoadWav(sample.data,sample.size,pcm,error)) {
            std::cerr << "FSB " << i << ": " << error << '\n'; return 1;
        }
        if (pcm.repaired_format_tag) ++repaired;
        if (i==0 && fnv(pcm.samples.data(),pcm.samples.size())!=0xee27287c4d6a7055ull) {
            std::cerr << "FSB first PCM differs from Python oracle\n"; return 1;
        }
    }
    if (fsb.clips().size()!=24 || repaired!=1) {
        std::cerr << "FSB count or known format-tag repair differs\n"; return 1;
    }
    {
        const auto sample=od::port::FSB_GetSample(fsb,0);
        od::port::WavePcm short_wave;
        std::string error;
        if (od::port::DSOUND_LoadWav(sample.data,20,short_wave,error)) {
            std::cerr << "truncated WAVE header was accepted\n"; return 1;
        }
    }
    od::port::DrdBank drd(vfs);
    od::port::DrdError drd_error;
    if (!od::port::DRD_Open(drd,"DATA/3DC/DIALOG.DRD",drd_error)) {
        std::cerr << drd_error.message << '\n'; return 1;
    }
    size_t portraits=0,lines=0;
    for (size_t i=0;i<drd.entry_count();++i) {
        if (!od::port::DRD_SelectEntry(drd,i,drd_error)) {
            std::cerr << "DRD " << i << ": " << drd_error.message << '\n'; return 1;
        }
        const auto wave=drd.wave();
        od::port::WavePcm pcm;
        std::string error;
        if (!od::port::DSOUND_LoadWav(wave.data,wave.size,pcm,error)) {
            std::cerr << "DRD WAVE " << i << ": " << error << '\n'; return 1;
        }
        lines+=od::port::DRD_GetLineCount(drd);
        const auto portrait=od::port::DRD_GetPortrait(drd);
        if (portrait.data) {
            od::port::PortraitSprite sprite;
            od::port::SpriteError sprite_error;
            if (!od::port::SPR_LoadPortrait(portrait.data,portrait.size,sprite,sprite_error)) {
                std::cerr << "DRD portrait " << i << ": " << sprite_error.message << '\n'; return 1;
            }
            ++portraits;
            if (i==0) {
                od::port::PortraitSprite truncated;
                if (od::port::SPR_LoadPortrait(portrait.data,16,truncated,sprite_error)) {
                    std::cerr << "truncated portrait was accepted\n"; return 1;
                }
            }
            if (i==0) {
                od::inspect::StillImage still;
                std::vector<uint8_t> rgba;
                if (!od::inspect::portrait_still_image(sprite,still,error) ||
                    !still.render(0,true,rgba,error)) {
                    std::cerr << error << '\n'; return 1;
                }
                if (fnv(pcm.samples.data(),pcm.samples.size())!=0xb757858174e1e60aull ||
                    fnv(rgba.data(),rgba.size())!=0x245a943bc67d4ca5ull ||
                    od::port::DRD_GetEntryDuration(drd)!=137) {
                    std::cerr << "DRD voice, portrait or duration differs from Python oracle\n";
                    return 1;
                }
            }
        }
        for (size_t line=0;line<od::port::DRD_GetLineCount(drd);++line) {
            if (od::port::DRD_GetLineDuration(drd,line)>od::port::DRD_GetEntryDuration(drd)) {
                std::cerr << "DRD caption duration exceeds entry\n"; return 1;
            }
        }
    }
    if (drd.entry_count()!=178 || portraits!=169 || lines<589) {
        std::cerr << "DRD entry, portrait or line count differs\n"; return 1;
    }
    uint64_t track_size=0;
    if (!image->audio_track_size(2,track_size,disc_error) || track_size<2352) {
        std::cerr << disc_error.message << '\n'; return 1;
    }
    std::vector<uint8_t> first(2352),last(2352);
    if (!image->read_audio_track_at(2,0,first.data(),first.size(),disc_error) ||
        !image->read_audio_track_at(2,track_size-last.size(),last.data(),last.size(),disc_error) ||
        image->read_audio_track_at(2,track_size-1,last.data(),last.size(),disc_error)) {
        std::cerr << "CD-DA track boundary check failed\n"; return 1;
    }
    std::vector<uint8_t> music(2352);
    if (!image->read_audio_track_at(2,3u*75u*2352u,music.data(),music.size(),disc_error) ||
        fnv(music.data(),music.size())!=0x4e9471ee23b191f2ull) {
        std::cerr << "CD-DA track offset differs from backing-file oracle\n"; return 1;
    }
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    od::AudioPreview preview;
    std::string playback_error;
    if (!preview.open_sound(image,"DATA/SOUND/FSB.DAT",0,playback_error) ||
        !preview.active() || preview.rate()!=11025 || preview.bits()!=16) {
        std::cerr << "SDL FSB playback: " << playback_error << '\n'; return 1;
    }
    preview.set_paused(true);
    if (!preview.paused() || !preview.restart(playback_error)) {
        std::cerr << "SDL FSB pause/restart: " << playback_error << '\n'; return 1;
    }
    preview.stop();
    if (!preview.open_dialogue(image,"DATA/3DC/DIALOG.DRD",0,playback_error) ||
        !preview.active() || !preview.has_portrait() || preview.captions().empty()) {
        std::cerr << "SDL DRD playback: " << playback_error << '\n'; return 1;
    }
    preview.stop();
    if (!preview.open_track(image,2,playback_error) || !preview.active() ||
        preview.rate()!=44100 || !preview.tick(playback_error)) {
        std::cerr << "SDL CD-DA playback: " << playback_error << '\n'; return 1;
    }
    preview.stop();
    od::AudioOutput movie_channel;
    const uint8_t stereo_frame[4]={0,0,0,0};
    if (!movie_channel.start_pcm_stream(22050,2,16,playback_error) ||
        !movie_channel.queue_pcm(stereo_frame,sizeof(stereo_frame),playback_error)) {
        std::cerr << "shared movie PCM queue: " << playback_error << '\n'; return 1;
    }
    movie_channel.stop();
    if (const char* cue2=std::getenv("DREAMS_CUE2")) {
        od::disc::Error second_error;
        auto second_opened=od::disc::Image::open(std::filesystem::u8path(cue2),second_error);
        if (!second_opened) { std::cerr << second_error.message << '\n'; return 1; }
        std::shared_ptr<const od::disc::Image> second(std::move(second_opened));
        std::vector<uint8_t> second_music(2352);
        if (!second->read_audio_track_at(2,3u*75u*2352u,
                                         second_music.data(),second_music.size(),second_error) ||
            fnv(second_music.data(),second_music.size())!=0x252841a1036f6e64ull ||
            !preview.open_track(second,2,playback_error)) {
            std::cerr << "Disc 2 CD-DA source differs: " <<
                (second_error ? second_error.message : playback_error) << '\n';
            return 1;
        }
        preview.stop();
    }
    std::cout << "24 FSB clips, 178 DRD voices, 169 portraits and source-scoped CD-DA verified\n";
    return 0;
}

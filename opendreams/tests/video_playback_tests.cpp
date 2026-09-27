#include "disc/image.h"
#include "port/video.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {
uint64_t fnv(const uint8_t* bytes, size_t size) {
    uint64_t hash=14695981039346656037ull;
    for (size_t i=0; i<size; ++i) hash=(hash^bytes[i])*1099511628211ull;
    return hash;
}
}

int main() {
    const char* cue=std::getenv("DREAMS_CUE2");
    if (!cue) { std::cout << "DREAMS_CUE2 is not configured\n"; return 77; }
    od::disc::Error disc_error;
    auto opened=od::disc::Image::open(cue,disc_error);
    if (!opened) { std::cerr << disc_error.message << '\n'; return 1; }
    std::shared_ptr<const od::disc::Image> image(std::move(opened));
    od::port::VfsContext vfs(image);
    od::port::VideoState video(vfs,true);
    od::port::VideoError error;
    if (od::port::VID_Open(video,"DATA/HNM/GENERIC.HNM",error)!=2) {
        std::cerr << "GENERIC open: " << error.message << '\n'; return 1;
    }
    for (int frame=0; frame<4; ++frame) {
        od::port::VideoStep step;
        if (!od::port::VID_DecodeFrame(video,step,error) || !step.image_ready) {
            std::cerr << "GENERIC step " << frame << ": " << error.message << '\n'; return 1;
        }
        if (frame==0) {
            const auto& pixels=video.rgb565_frame();
            const uint64_t image_hash=fnv(reinterpret_cast<const uint8_t*>(pixels.data()),
                                           pixels.size()*sizeof(uint16_t));
            const uint64_t sound_hash=fnv(reinterpret_cast<const uint8_t*>(step.pcm.data()),
                                           step.pcm.size()*sizeof(int16_t));
            if (image_hash!=0x9c14693a3e6a7003ull ||
                sound_hash!=0x0a4680e29badf74cull) {
                std::cerr << "first GENERIC frame or SD audio differs from corpus oracle\n";
                return 1;
            }
        }
    }
    while (!video.ended()) {
        od::port::VideoStep step;
        if (!od::port::VID_DecodeFrame(video,step,error)) {
            std::cerr << "GENERIC full playback: " << error.message << '\n'; return 1;
        }
    }
    if (video.decoded_frames()!=video.total_frames()) {
        std::cerr << "GENERIC frame count differs\n"; return 1;
    }
    od::port::VID_Close(video);
    if (video.stream_open()) { std::cerr << "movie stream stayed open\n"; return 1; }
    if (od::port::VID_Open(video,"DATA/HNM/HNMFR2.UBB",error)!=2) {
        std::cerr << "UBS2 open: " << error.message << '\n'; return 1;
    }
    for (int frame=0; frame<4; ++frame) {
        od::port::VideoStep step;
        if (!od::port::VID_DecodeFrame(video,step,error) || !step.image_ready) {
            std::cerr << "UBS2 step " << frame << ": " << error.message << '\n'; return 1;
        }
        const auto& pixels=video.rgb565_frame();
        if (const char* dump=std::getenv("DREAMS_HNM5_DUMP")) {
            std::ofstream output(std::string(dump)+"."+std::to_string(frame),
                                 std::ios::binary);
            output.write(reinterpret_cast<const char*>(pixels.data()),
                         static_cast<std::streamsize>(pixels.size()*sizeof(uint16_t)));
        }
        if (frame==0) {
            const uint64_t image_hash=fnv(reinterpret_cast<const uint8_t*>(pixels.data()),
                                           pixels.size()*sizeof(uint16_t));
            const uint64_t sound_hash=fnv(reinterpret_cast<const uint8_t*>(step.pcm.data()),
                                           step.pcm.size()*sizeof(int16_t));
            if (image_hash!=0x0122ecd61e4a96a2ull ||
                sound_hash!=0x29e414b22c02e858ull) {
                std::cerr << "first UBS2 frame or SD audio differs from corpus oracle\n";
                return 1;
            }
        }
    }
    while (!video.ended()) {
        od::port::VideoStep step;
        if (!od::port::VID_DecodeFrame(video,step,error)) {
            std::cerr << "UBS2 full playback: " << error.message << '\n'; return 1;
        }
    }
    if (video.decoded_frames()!=video.total_frames()) {
        std::cerr << "UBS2 frame count differs\n"; return 1;
    }
    return 0;
}

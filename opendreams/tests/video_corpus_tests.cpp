#include "disc/image.h"
#include "port/video.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {
bool movie_path(const std::string& path) {
    const auto dot=path.find_last_of('.');
    if (dot==std::string::npos) return false;
    std::string extension=path.substr(dot);
    std::transform(extension.begin(),extension.end(),extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return extension==".HNM" || extension==".UBB";
}
}

int main() {
    const char* cues[2]={std::getenv("DREAMS_CUE1"),std::getenv("DREAMS_CUE2")};
    if (!cues[0] || !cues[1]) {
        std::cout << "DREAMS_CUE1/2 are not configured\n"; return 77;
    }
    unsigned files=0,frames=0,unsupported=0,failures=0;
    for (const char* cue : cues) {
        od::disc::Error disc_error;
        auto opened=od::disc::Image::open(cue,disc_error);
        if (!opened) { std::cerr << disc_error.message << '\n'; return 1; }
        std::shared_ptr<const od::disc::Image> image(std::move(opened));
        for (const auto& entry : image->entries()) {
            if (entry.kind!=od::disc::EntryKind::file || !movie_path(entry.path)) continue;
            od::port::VfsContext vfs(image);
            od::port::VideoState video(vfs,true);
            od::port::VideoError error;
            if (!od::port::VID_Open(video,entry.path,error)) {
                std::cerr << entry.path << ": open: " << error.message << '\n';
                ++failures; continue;
            }
            if (video.family()==od::port::VideoFamily::hnm4) { ++unsupported; continue; }
            ++files;
            while (!video.ended()) {
                od::port::VideoStep step;
                if (!od::port::VID_DecodeFrame(video,step,error)) {
                    std::cerr << entry.path << ": frame " << video.decoded_frames()
                              << ": " << error.message << '\n';
                    ++failures; break;
                }
            }
            if (video.ended() && video.decoded_frames()!=video.total_frames()) {
                std::cerr << entry.path << ": decoded " << video.decoded_frames()
                          << " of " << video.total_frames() << " frames\n";
                ++failures;
            }
            frames+=video.decoded_frames();
        }
    }
    std::cout << files << " HNM5/6 files, " << frames << " frames, " << unsupported
              << " HNM4 textures skipped, " << failures << " failures\n";
    return failures ? 1 : 0;
}

#include "port/hnm6.h"
#include "port/hnm5.h"
#include "port/video_audio.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1])<<8) |
           (static_cast<uint32_t>(p[2])<<16) | (static_cast<uint32_t>(p[3])<<24);
}
}

int main() {
    std::array<std::array<int16_t, 64>, 3> planes{};
    std::vector<uint16_t> color_block(64);
    planes[1][0] = 64;
    od::port::HNM6_StoreBlockRGB16(planes,color_block,0,0,8);
    if (color_block[0] != 0x835f || color_block[1] != 0x8410) {
        std::cerr << "retail HNM6 blue and green table terms differ\n";
        return 1;
    }
    od::port::VideoDpcm audio;
    std::array<uint8_t, 516> sd{};
    sd[2] = 1; // delta code 1 -> +1
    sd[4] = 255; sd[5] = 255; // delta code 2 -> -1
    sd[512] = 1; sd[513] = 2; sd[514] = 1; sd[515] = 2;
    std::vector<int16_t> pcm;
    std::string error;
    if (!audio.decode_sd(sd.data(),sd.size(),pcm,error) ||
        pcm != std::vector<int16_t>{1,-1,2,-2}) {
        std::cerr << "retail SD predictors differ: " << error << '\n'; return 1;
    }
    std::vector<uint16_t> malformed_frame;
    std::vector<uint16_t> previous(640u*304u);
    od::port::Hnm6Decoder malformed_hnm6;
    od::port::Hnm5Decoder malformed_hnm5;
    const uint8_t short_payload[2] = {0xe0,0};
    if (malformed_hnm6.decode(short_payload,2,previous,malformed_frame,error) ||
        malformed_hnm5.decode(short_payload,2,malformed_frame,error)) {
        std::cerr << "short movie frame was accepted\n"; return 1;
    }
    const char* root = std::getenv("DREAMS_DISC2");
    if (!root) { std::cout << "DREAMS_DISC2 is not configured\n"; return 77; }
    const auto path = std::filesystem::path(root) / "DATA" / "HNM" / "GENERIC.HNM";
    std::ifstream stream(path,std::ios::binary);
    if (!stream) { std::cerr << "GENERIC.HNM is unavailable\n"; return 77; }
    const std::vector<uint8_t> file((std::istreambuf_iterator<char>(stream)),{});
    if (file.size()<68) { std::cerr << "GENERIC.HNM is truncated\n"; return 1; }
    od::port::Hnm6Decoder decoder;
    std::vector<uint16_t> destination;
    size_t offset=64;
    unsigned frames=0;
    while (frames<4 && offset+4<=file.size()) {
        const size_t outer=le32(file.data()+offset)&0x00ffffffu;
        if (outer==0) break;
        if (outer<4 || offset+outer>file.size()) { std::cerr << "outer chunk invalid\n"; return 1; }
        size_t inner=offset+4;
        while (inner+8<=offset+outer) {
            const size_t size=le32(file.data()+inner)&0x07ffffffu;
            if (size<8 || inner+size>offset+outer) { std::cerr << "inner chunk invalid\n"; return 1; }
            if (file[inner+4]=='I' && file[inner+5]=='X') {
                if (!decoder.decode(file.data()+inner+8,size-8,previous,destination,error)) {
                    std::cerr << "GENERIC frame " << frames << ": " << error << '\n'; return 1;
                }
                previous.swap(destination);
                ++frames;
                if (const char* dump = std::getenv("DREAMS_HNM6_DUMP")) {
                    std::ofstream output(std::string(dump)+"."+std::to_string(frames-1),
                                         std::ios::binary);
                    output.write(reinterpret_cast<const char*>(previous.data()),
                                 static_cast<std::streamsize>(previous.size()*sizeof(uint16_t)));
                }
            }
            inner+=(size+3)&~size_t(3);
        }
        offset+=outer;
    }
    if (frames<4) { std::cerr << "GENERIC produced fewer than four frames\n"; return 1; }
    std::cout << "decoded " << frames << " GENERIC.HNM frames\n";
    return 0;
}

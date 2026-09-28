#include "disc/image.h"
#include "port/hnm4.h"
#include "port/video.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr uint64_t fnv_seed = 14695981039346656037ull;

uint64_t fnv(const uint8_t* bytes, size_t size, uint64_t hash = fnv_seed) {
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; return false; } } while (0)

// Hand-built payloads exercise every retail opcode family with known answers.
bool test_synthetic_codec() {
    od::port::Hnm4Decoder decoder(256, 256);
    std::string error;
    bool written = false;

    // PL: entry 0 = (63,0,1), entry 1 = (2,3,4); retail stores channel << 2.
    const std::vector<uint8_t> pl{0, 2, 63, 0, 1, 2, 3, 4, 0xff, 0xff};
    CHECK(decoder.update_palette(pl.data(), pl.size(), error));
    CHECK(decoder.palette()[0] == 0xfc && decoder.palette()[1] == 0 &&
          decoder.palette()[2] == 4 && decoder.palette()[3] == 8 &&
          decoder.palette()[5] == 16);

    // IZ: header word, bits 1 1 0 0 1 1 0 1 (MSB first), literals 5 and 7,
    // a short match (length 3+2, distance 2), then the long-form end marker.
    const std::vector<uint8_t> iz{0, 1, 0, 0, 0x00, 0x00, 0x00, 0xcd,
                                  5, 7, 0xfe, 0, 0, 0};
    CHECK(decoder.decode('Z', iz.data(), iz.size(), written, error) && written);
    CHECK(decoder.frame_counter() == 1);
    const auto& key = decoder.texture();
    CHECK(key[0] == 5 && key[1] == 5 && key[2] == 5 && key[3] == 5 && key[4] == 0);
    CHECK(key[256] == 7 && key[257] == 7 && key[258] == 7 && key[259] == 0);

    // IU on the odd counter writes the other buffer: literal (9,10), copy
    // one word from the previous frame at byte 2, swapped copy of byte 0 of
    // the current frame, fill two words with 3, end.
    const std::vector<uint8_t> iu{0x00, 9, 10,
                                  0x21, 0x00, 0x80,
                                  0x01, 0xfd, 0x7f,
                                  0x60, 2, 3,
                                  0x80};
    CHECK(decoder.decode('U', iu.data(), iu.size(), written, error) && written);
    const auto& inter = decoder.texture();
    const std::array<uint8_t, 6> even{9, 5, 10, 3, 3, 0}; // Byte 10 was never written.
    const std::array<uint8_t, 6> odd{10, 7, 9, 3, 3, 0};
    for (size_t i = 0; i < 6; ++i) CHECK(inter[i] == even[i] && inter[256 + i] == odd[i]);

    // An unknown I? kind only advances the retail buffer parity.
    CHECK(decoder.decode('W', nullptr, 0, written, error) && !written);
    CHECK(decoder.frame_counter() == 3);
    return true;
}

bool expect_error(bool ok, const std::string& error, const char* label) {
    if (ok || error.empty()) {
        std::cerr << label << " was accepted\n";
        return false;
    }
    return true;
}

bool test_malformed() {
    std::string error;
    bool written = false;
    od::port::Hnm4Decoder decoder(256, 256);
    const std::vector<uint8_t> truncated_copy{0x21, 0x00};
    CHECK(expect_error(decoder.decode('U', truncated_copy.data(), truncated_copy.size(),
                                      written, error), error, "truncated IU copy"));
    const std::vector<uint8_t> no_end{0x00, 1, 2};
    CHECK(expect_error(decoder.decode('U', no_end.data(), no_end.size(), written, error),
                       error, "IU without end code"));
    const std::vector<uint8_t> zero_fill{0x60, 0, 1, 0x80};
    CHECK(expect_error(decoder.decode('U', zero_fill.data(), zero_fill.size(), written,
                                      error), error, "zero-count IU fill"));
    const std::vector<uint8_t> outside{0x21, 0x00, 0x00, 0x80}; // Source at -0x8000.
    CHECK(expect_error(decoder.decode('U', outside.data(), outside.size(), written, error),
                       error, "IU source before the frame"));
    const std::vector<uint8_t> overrun{0x40, 0x00, 0x80, 0x00, 1, 2, 0x80};
    CHECK(expect_error(decoder.decode('U', overrun.data(), overrun.size(), written, error),
                       error, "IU literal past the frame"));
    // Short match before any output.
    const std::vector<uint8_t> early_match{0, 0, 0, 0, 0, 0, 0, 0x00, 0xff};
    CHECK(expect_error(decoder.decode('Z', early_match.data(), early_match.size(), written,
                                      error), error, "IZ match before the frame"));
    const std::vector<uint8_t> short_bits{0, 0, 0, 0, 0xff, 0xff};
    CHECK(expect_error(decoder.decode('Z', short_bits.data(), short_bits.size(), written,
                                      error), error, "truncated IZ bit word"));
    CHECK(expect_error(decoder.decode('Z', short_bits.data(), 2, written, error), error,
                       "IZ without header word"));
    const std::vector<uint8_t> pl_range{200, 100, 0, 0, 0};
    CHECK(expect_error(decoder.update_palette(pl_range.data(), pl_range.size(), error),
                       error, "PL range overflow"));
    const std::vector<uint8_t> pl_open{0, 1, 1, 2, 3};
    CHECK(expect_error(decoder.update_palette(pl_open.data(), pl_open.size(), error),
                       error, "PL without terminator"));
    const std::vector<uint8_t> pl_short{0, 2, 1, 2, 3, 4};
    CHECK(expect_error(decoder.update_palette(pl_short.data(), pl_short.size(), error),
                       error, "truncated PL entries"));
    od::port::Hnm4Decoder wrong_size(320, 200);
    const std::vector<uint8_t> end_only{0x80};
    CHECK(expect_error(wrong_size.decode('U', end_only.data(), end_only.size(), written,
                                         error), error, "non-256x256 frame"));
    return true;
}

struct Expected {
    const char* path;
    uint32_t frames;
    uint64_t all_indices, all_palettes;
    std::vector<std::array<uint64_t, 3>> samples; // frame, index hash, palette hash
};

// Oracle values: na_game_tool 0.6.0 `-ifmt hnm4 ... -ofmt imgseq f%04d.bmp`,
// rows flipped top-down; its VGA palette (v<<2)|(v>>4) masked with 0xfc to
// the retail v<<2. The all-frame hashes chain every frame in order.
bool check_file(od::port::VfsContext& vfs, const Expected& expected) {
    od::port::VideoState video(vfs, true);
    od::port::VideoError error;
    if (od::port::VID_Open(video, expected.path, error) != 1 ||
        video.family() != od::port::VideoFamily::hnm4 || video.kind_flags() != 1) {
        std::cerr << expected.path << " open: " << error.message << '\n'; return false;
    }
    if (video.total_frames() != expected.frames) {
        std::cerr << expected.path << " header frame count differs\n"; return false;
    }
    std::unique_ptr<std::ofstream> dump;
    if (const char* path = std::getenv("DREAMS_HNM4_HASHES"))
        dump = std::make_unique<std::ofstream>(std::string(path) + "." +
                                               std::string(expected.path + 10) + ".tsv");
    uint64_t all_indices = fnv_seed, all_palettes = fnv_seed;
    size_t sample = 0;
    uint32_t frame = 0;
    while (!video.ended()) {
        od::port::VideoStep step;
        if (!od::port::VID_DecodeFrame(video, step, error) || !step.image_ready) {
            std::cerr << expected.path << " frame " << frame << ": " << error.message << '\n';
            return false;
        }
        const auto& texture = video.hnm4().texture();
        const auto& palette = video.hnm4().palette();
        const uint64_t index_hash = fnv(texture.data(), texture.size());
        const uint64_t palette_hash = fnv(palette.data(), palette.size());
        all_indices = fnv(texture.data(), texture.size(), all_indices);
        all_palettes = fnv(palette.data(), palette.size(), all_palettes);
        if (dump)
            *dump << frame << '\t' << std::hex << std::setw(16) << std::setfill('0')
                  << index_hash << '\t' << std::setw(16) << palette_hash << std::dec << '\n';
        if (sample < expected.samples.size() && expected.samples[sample][0] == frame) {
            if (index_hash != expected.samples[sample][1] ||
                palette_hash != expected.samples[sample][2]) {
                std::cerr << expected.path << " frame " << frame << " differs from NihAV\n";
                return false;
            }
            ++sample;
        }
        ++frame;
    }
    if (frame != expected.frames || video.decoded_frames() != expected.frames ||
        sample != expected.samples.size()) {
        std::cerr << expected.path << " decoded " << frame << " frames\n"; return false;
    }
    if (all_indices != expected.all_indices || all_palettes != expected.all_palettes) {
        std::cerr << expected.path << " frame sequence differs from NihAV\n"; return false;
    }
    od::port::VID_Close(video);
    if (video.stream_open() || !video.hnm4().texture().empty()) {
        std::cerr << "HNM4 close left state behind\n"; return false;
    }
    std::cout << expected.path << ": " << frame << " frames match NihAV\n";
    return true;
}

int check_disc(const char* cue_env, const std::vector<Expected>& files) {
    const char* cue = std::getenv(cue_env);
    if (!cue) return 77;
    od::disc::Error disc_error;
    auto opened = od::disc::Image::open(cue, disc_error);
    if (!opened) { std::cerr << disc_error.message << '\n'; return 1; }
    std::shared_ptr<const od::disc::Image> image(std::move(opened));
    od::port::VfsContext vfs(image);
    for (const auto& file : files)
        if (!check_file(vfs, file)) return 1;
    return 0;
}
} // namespace

int main() {
    if (!test_synthetic_codec() || !test_malformed()) return 1;
    // Every physical HNM4 file on both discs. TF_ALL has 14 PL/IZ key
    // frames; its samples cover the first palette change and the end.
    const std::vector<Expected> disc1{
        {"DATA/ANIM/E11_EAU.HNM", 75, 0x2dd29df446d6c685ull, 0x4e21c588d63b14a5ull,
         {{0, 0x65410aab922d916cull, 0xee8a7f6fa83f2ea5ull},
          {74, 0xa46c08252d785deeull, 0xee8a7f6fa83f2ea5ull}}},
        {"DATA/ANIM/E12_EAU.HNM", 65, 0xae7c0926715c3f8full, 0x18444d21cd577521ull, {}},
        {"DATA/ANIM/FD_SOUFL.HNM", 201, 0x5af189a9249070f8ull, 0xec169253dfe08c59ull, {}},
        {"DATA/ANIM/H03AN001.HNM", 70, 0xcbd1f17136d72d5eull, 0x1bc25de04a103225ull, {}},
        {"DATA/ANIM/H04AN001.HNM", 100, 0xc0627bb31925cfbeull, 0x0b46998603142265ull, {}},
        {"DATA/ANIM/M05FEU_H.HNM", 15, 0x545079c3aba084efull, 0x3adc5e48774e7a15ull, {}}};
    const std::vector<Expected> disc2{
        {"DATA/ANIM/TF_ALL.HNM", 851, 0x5542e093f6661600ull, 0xec02a8897e455215ull,
         {{0, 0xeec8cf29739df21bull, 0x58fdde878b818d79ull},
          {1, 0xe8a23c18208cfdc4ull, 0x58fdde878b818d79ull},
          {2, 0xf04a7fe83d1b66ceull, 0x58fdde878b818d79ull},
          {63, 0xd684992f66dae859ull, 0x58fdde878b818d79ull},
          {64, 0xe1fe423bad1976e8ull, 0x880642a92ea59ebdull},
          {425, 0xb67b578ba3d9f755ull, 0xd06fb7bddeca61d5ull},
          {850, 0x58ba56fa315a2325ull, 0xdfe960580244294dull}}},
        {"DATA/ANIM/CYB1_TR.HNM", 220, 0xc20c56573a249b2dull, 0xa970e36da41c57e5ull, {}},
        {"DATA/ANIM/CYB2_TR.HNM", 87, 0xefb45df1f0037819ull, 0xa7a134d856881c29ull, {}},
        {"DATA/ANIM/CYB3_TR.HNM", 100, 0x921f0b51f1d784afull, 0x5967451df5da3b05ull, {}},
        {"DATA/ANIM/END_BIL.HNM", 101, 0xf38aac9818613602ull, 0xf336e8f8da3810a1ull, {}},
        {"DATA/ANIM/FD_SOUFL.HNM", 201, 0x5af189a9249070f8ull, 0xec169253dfe08c59ull, {}},
        {"DATA/ANIM/H02HNM01.HNM", 100, 0xa78b9e5e40557cd2ull, 0x07016bec89257d65ull, {}},
        {"DATA/ANIM/H03AN001.HNM", 70, 0xcbd1f17136d72d5eull, 0x1bc25de04a103225ull, {}},
        {"DATA/ANIM/L12_HNM1.HNM", 50, 0x5cc29371b64a707dull, 0xbe70b9fcc6594d75ull, {}},
        {"DATA/ANIM/L14_HNM1.HNM", 50, 0x5fd70c27bac0be03ull, 0x711fbae12d1865b5ull, {}},
        {"DATA/ANIM/M01DRA.HNM", 111, 0xf30d58a22bee638full, 0x821efd710a105fd5ull, {}},
        {"DATA/ANIM/MOTEURB.HNM", 99, 0xac39c16325cd9444ull, 0x35c1b29ef7a48ec5ull, {}},
        {"DATA/ANIM/ORGA_01.HNM", 367, 0x92aebbfbba2e11b9ull, 0xeb80b95fab01bc9dull, {}},
        {"DATA/ANIM/PAS_JOHN.HNM", 100, 0x084d969fa7214647ull, 0xac62bfa2d307e3e5ull, {}}};
    const int result1 = check_disc("DREAMS_CUE1", disc1);
    if (result1 == 1) return 1;
    const int result2 = check_disc("DREAMS_CUE2", disc2);
    if (result2 == 1) return 1;
    if (result1 == 77 && result2 == 77) {
        std::cout << "DREAMS_CUE1/DREAMS_CUE2 are not configured\n";
        return 77;
    }
    return 0;
}

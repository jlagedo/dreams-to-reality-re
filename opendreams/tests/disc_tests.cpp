#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include <lib9660.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using od::disc::EntryKind;
using od::disc::Error;
using od::disc::ErrorCode;
using od::disc::FileId;
using od::disc::Identity;
using od::disc::Image;
using od::disc::TrackStatus;
namespace fs = std::filesystem;

bool check(bool condition, const char* expression, int line) {
    if (!condition) std::cerr << "line " << line << ": " << expression << "\n";
    return condition;
}
#define CHECK(value) do { if (!check((value), #value, __LINE__)) return false; } while (0)

void little16(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
}

void dual16(uint8_t* out, uint16_t value) {
    little16(out, value);
    out[2] = static_cast<uint8_t>(value >> 8);
    out[3] = static_cast<uint8_t>(value);
}

void dual32(uint8_t* out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        out[i] = static_cast<uint8_t>(value >> (i * 8));
        out[4 + i] = static_cast<uint8_t>(value >> ((3 - i) * 8));
    }
}

size_t record(uint8_t* sector, size_t at, uint32_t extent, uint32_t size,
              uint8_t flags, const std::string& identifier) {
    const size_t length = 33 + identifier.size() + (identifier.size() % 2 == 0 ? 1 : 0);
    sector[at] = static_cast<uint8_t>(length);
    dual32(sector + at + 2, extent);
    dual32(sector + at + 10, size);
    sector[at + 25] = flags;
    dual16(sector + at + 28, 1);
    sector[at + 32] = static_cast<uint8_t>(identifier.size());
    std::memcpy(sector + at + 33, identifier.data(), identifier.size());
    return at + length;
}

enum class Markers { one, two, both, neither };
struct Options {
    Markers markers = Markers::one;
    uint32_t data_index = 0;
    bool boot_before_pvd = false;
    bool duplicate_name = false;
    bool bad_extent = false;
    bool bad_record = false;
    bool missing_audio = false;
    bool invalid_audio = false;
    bool unicode_data_name = false;
};

struct Fixture {
    fs::path cue;
    fs::path data;
    fs::path audio;
};

struct RawBridge {
    l9660_fs fs{};
    std::ifstream* source = nullptr;
    uint64_t sectors = 0;
};

bool raw_sector(l9660_fs* fs, void* output, uint32_t sector) {
    auto* bridge = reinterpret_cast<RawBridge*>(fs);
    if (sector >= bridge->sectors) return false;
    bridge->source->clear();
    bridge->source->seekg(static_cast<std::streamoff>(sector) * 2352 + 16);
    bridge->source->read(static_cast<char*>(output), 2048);
    return bridge->source->gcount() == 2048;
}

Fixture make_fixture(const fs::path& directory, const Options& options = {}) {
    fs::create_directories(directory);
    std::vector<uint8_t> iso(32 * 2048);
    const unsigned pvd_sector = options.boot_before_pvd ? 17 : 16;
    if (options.boot_before_pvd) {
        auto* boot = iso.data() + 16 * 2048;
        boot[0] = 0;
        std::memcpy(boot + 1, "CD001", 5);
        boot[6] = 1;
    }
    auto* pvd = iso.data() + pvd_sector * 2048;
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    std::memcpy(pvd + 40, "TEST_DISC", 9);
    dual32(pvd + 80, 32);
    dual16(pvd + 128, 2048);
    record(pvd, 156, 20, 4096, 2, std::string(1, '\0'));
    auto* terminator = iso.data() + (pvd_sector + 1) * 2048;
    terminator[0] = 255;
    std::memcpy(terminator + 1, "CD001", 5);
    terminator[6] = 1;

    auto* root = iso.data() + 20 * 2048;
    size_t pos = record(root, 0, 20, 4096, 2, std::string(1, '\0'));
    pos = record(root, pos, 20, 4096, 2, std::string(1, '\1'));
    pos = record(root, pos, 22, 2048, 2, "DATA");
    pos = record(root, pos, options.bad_extent ? 40 : 23, 2300, 0, "DREAMS.DAT;1");
    record(root, pos, 27, 2048, 2, "EMPTY");
    auto* root_second = iso.data() + 21 * 2048;
    pos = record(root_second, 0, 26, 4, 0, "EXTRA.TXT;1");
    if (options.duplicate_name)
        record(root_second, pos, 26, 4, 0, "extra.txt;1");
    if (options.bad_record) root[0] = 1;

    auto* data_dir = iso.data() + 22 * 2048;
    pos = record(data_dir, 0, 22, 2048, 2, std::string(1, '\0'));
    pos = record(data_dir, pos, 20, 4096, 2, std::string(1, '\1'));
    if (options.markers == Markers::one || options.markers == Markers::both)
        pos = record(data_dir, pos, 25, 5, 0, "1CD.ID;1");
    if (options.markers == Markers::two || options.markers == Markers::both)
        record(data_dir, pos, 28, 5, 0, "2CD.ID;1");
    auto* empty_dir = iso.data() + 27 * 2048;
    pos = record(empty_dir, 0, 27, 2048, 2, std::string(1, '\0'));
    record(empty_dir, pos, 20, 4096, 2, std::string(1, '\1'));

    for (size_t i = 0; i < 2300; ++i)
        iso[23 * 2048 + i] = static_cast<uint8_t>(i % 251);
    std::memcpy(iso.data() + 25 * 2048, "kjk\r\n", 5);
    std::memcpy(iso.data() + 26 * 2048, "more", 4);
    std::memcpy(iso.data() + 28 * 2048, "kjk\r\n", 5);

    const std::string data_name = options.unicode_data_name ? u8"Träck 01.bin" :
        "Track 01.bin";
    Fixture fixture{directory / "Dreams test.cue", directory / fs::u8path(data_name),
                    directory / "Track 02.bin"};
    {
        std::ofstream out(fixture.data, std::ios::binary);
        const std::array<char, 2352> empty{};
        for (uint32_t i = 0; i < options.data_index; ++i)
            out.write(empty.data(), empty.size());
        for (size_t i = 0; i < 32; ++i) {
            std::array<char, 2352> raw{};
            std::memcpy(raw.data() + 16, iso.data() + i * 2048, 2048);
            out.write(raw.data(), raw.size());
        }
    }
    if (!options.missing_audio) {
        std::ofstream out(fixture.audio, std::ios::binary);
        const size_t bytes = options.invalid_audio ? 2353 : 152 * 2352;
        out.seekp(static_cast<std::streamoff>(bytes - 1));
        out.put('\0');
    }
    {
        std::ofstream out(fixture.cue, std::ios::binary);
        out << "FILE \"" << data_name << "\" BINARY\r\n"
               "  TRACK 01 MODE1/2352\r\n"
               "    INDEX 01 00:00:0" << options.data_index << "\r\n"
               "FILE \"Track 02.bin\" BINARY\r\n"
               "  TRACK 02 AUDIO\r\n"
               "    INDEX 00 00:00:00\r\n"
               "    INDEX 01 00:02:01\r\n";
    }
    return fixture;
}

bool test_mount_and_read(const fs::path& base) {
    const auto fixture = make_fixture(base / fs::u8path(u8"disc çã 空"));
    std::ifstream source(fixture.data, std::ios::binary);
    RawBridge raw{};
    raw.source = &source;
    raw.sectors = 32;
    CHECK(l9660_openfs(&raw.fs, raw_sector) == L9660_OK);
    l9660_dir direct_root{};
    CHECK(l9660_fs_open_root(&direct_root, &raw.fs) == L9660_OK);
    l9660_file direct_marker{};
    CHECK(l9660_openat(&direct_marker, &direct_root, "DATA/1CD.ID") == L9660_OK);
    CHECK(l9660_seek(&direct_marker, L9660_SEEK_SET, 2) == L9660_OK);
    std::array<char, 3> direct_tail{};
    size_t direct_count = 0;
    CHECK(l9660_read(&direct_marker, direct_tail.data(), direct_tail.size(),
                     &direct_count) == L9660_OK);
    CHECK(direct_count == direct_tail.size());
    CHECK(std::memcmp(direct_tail.data(), "k\r\n", 3) == 0);
    CHECK(l9660_seek(&direct_marker, L9660_SEEK_SET, -1) == L9660_EINVAL);

    Error error;
    auto image = Image::open(fixture.cue, error);
    CHECK(image != nullptr);
    CHECK(!error);
    CHECK(image->identity() == Identity::disc1);
    CHECK(image->file_count() == 3);
    CHECK(image->directory_count() == 2);
    CHECK(image->entries().size() == 5);
    CHECK(image->tracks().size() == 2);
    CHECK(image->tracks()[1].status == TrackStatus::available);
    CHECK(image->tracks()[1].index00_frames == uint32_t{0});
    CHECK(image->tracks()[1].index01_frames == 151);
    CHECK(image->tracks()[1].program_sectors == 1);
    CHECK(image->volume_id() == "TEST_DISC");
    std::vector<FileId> root_children;
    CHECK(image->children("", root_children, error));
    CHECK(root_children.size() == 4);
    std::vector<FileId> data_children;
    CHECK(image->children("DATA", data_children, error));
    CHECK(data_children.size() == 1);
    CHECK(image->entry(data_children.front())->path == "DATA/1CD.ID");

    FileId marker;
    CHECK(image->find("data\\1cd.id", marker, error));
    const auto* marker_entry = image->entry(marker);
    CHECK(marker_entry != nullptr);
    CHECK(marker_entry->path == "DATA/1CD.ID");
    CHECK(marker_entry->iso_path == "DATA/1CD.ID;1");
    std::array<char, 5> marker_bytes{};
    CHECK(image->read_at(marker, 0, marker_bytes.data(), marker_bytes.size(), error));
    CHECK(std::memcmp(marker_bytes.data(), "kjk\r\n", 5) == 0);

    FileId data;
    CHECK(image->find("dreams.dat;1", data, error));
    CHECK(image->entry(data)->byte_size == 2300);
    std::array<uint8_t, 32> bytes{};
    CHECK(image->read_at(data, 2040, bytes.data(), bytes.size(), error));
    for (size_t i = 0; i < bytes.size(); ++i)
        CHECK(bytes[i] == static_cast<uint8_t>((2040 + i) % 251));
    CHECK(!image->read_at(data, 2299, bytes.data(), 2, error));
    CHECK(error.code == ErrorCode::out_of_range);
    CHECK(image->read_at(data, 2300, nullptr, 0, error));
    CHECK(image->find("EXTRA.TXT", data, error)); // Second root directory sector.
    CHECK(image->entry(data)->kind == EntryKind::file);
    CHECK(!image->children("DREAMS.DAT", data_children, error));
    CHECK(error.code == ErrorCode::not_directory);
    CHECK(!Image::open(fixture.cue.parent_path() / "missing.cue", error));
    CHECK(error.code == ErrorCode::cue_io);
    CHECK(image->read_at(marker, 0, marker_bytes.data(), marker_bytes.size(), error));

    fs::remove(fixture.audio);
    auto replaced = Image::open(fixture.cue, error);
    CHECK(replaced != nullptr);
    CHECK(replaced->tracks()[1].status == TrackStatus::missing);
    CHECK(replaced->mount_id() != image->mount_id());
    CHECK(!replaced->read_at(marker, 0, marker_bytes.data(), marker_bytes.size(), error));
    CHECK(error.code == ErrorCode::stale_file);
    return true;
}

bool test_markers_and_offsets(const fs::path& base) {
    Error error;
    Options options;
    options.markers = Markers::two;
    options.data_index = 2;
    options.boot_before_pvd = true;
    options.unicode_data_name = true;
    auto fixture = make_fixture(base / "disc2-offset", options);
    auto image = Image::open(fixture.cue, error);
    CHECK(image != nullptr);
    CHECK(image->identity() == Identity::disc2);
    CHECK(image->tracks()[0].index01_frames == 2);
    FileId marker;
    CHECK(image->find("DATA/2CD.ID", marker, error));
    std::array<char, 5> bytes{};
    CHECK(image->read_at(marker, 0, bytes.data(), bytes.size(), error));
    CHECK(std::memcmp(bytes.data(), "kjk\r\n", 5) == 0);

    options = {};
    options.markers = Markers::both;
    fixture = make_fixture(base / "both", options);
    image = Image::open(fixture.cue, error);
    CHECK(image != nullptr);
    CHECK(image->identity() == Identity::ambiguous);
    options.markers = Markers::neither;
    fixture = make_fixture(base / "neither", options);
    image = Image::open(fixture.cue, error);
    CHECK(image != nullptr);
    CHECK(image->identity() == Identity::unknown);
    return true;
}

bool test_failures(const fs::path& base) {
    Error error;
    Options options;
    options.bad_extent = true;
    auto fixture = make_fixture(base / "bad-extent", options);
    CHECK(!Image::open(fixture.cue, error));
    CHECK(error.code == ErrorCode::invalid_iso);
    options = {};
    options.bad_record = true;
    fixture = make_fixture(base / "bad-record", options);
    CHECK(!Image::open(fixture.cue, error));
    CHECK(error.code == ErrorCode::invalid_iso);
    options = {};
    options.duplicate_name = true;
    fixture = make_fixture(base / "duplicate", options);
    auto image = Image::open(fixture.cue, error);
    CHECK(image != nullptr);
    FileId file;
    CHECK(!image->find("extra.txt", file, error));
    CHECK(error.code == ErrorCode::ambiguous);

    options = {};
    options.invalid_audio = true;
    fixture = make_fixture(base / "invalid-audio", options);
    image = Image::open(fixture.cue, error);
    CHECK(image != nullptr);
    CHECK(image->tracks()[1].status == TrackStatus::invalid);
    image.reset();
    fs::remove(fixture.data);
    CHECK(!Image::open(fixture.cue, error));
    CHECK(error.code == ErrorCode::data_io);

    fixture = make_fixture(base / "unsupported");
    {
        std::ofstream out(fixture.cue, std::ios::app);
        out << "PREGAP 00:02:00\n";
    }
    CHECK(!Image::open(fixture.cue, error));
    CHECK(error.code == ErrorCode::unsupported_layout);

    fixture = make_fixture(base / "bad-index");
    {
        std::ofstream out(fixture.cue, std::ios::trunc);
        out << "FILE \"Track 01.bin\" BINARY\n"
               "TRACK 01 MODE1/2352\nINDEX 01 99:00:00\n";
    }
    CHECK(!Image::open(fixture.cue, error));
    CHECK(error.code == ErrorCode::data_io);

    fixture = make_fixture(base / "shared-bin");
    {
        std::ofstream out(fixture.cue, std::ios::trunc);
        out << "FILE \"Track 01.bin\" BINARY\n"
               "TRACK 01 MODE1/2352\nINDEX 01 00:00:00\n"
               "TRACK 02 AUDIO\nINDEX 01 00:01:00\n";
    }
    CHECK(!Image::open(fixture.cue, error));
    CHECK(error.code == ErrorCode::unsupported_layout);
    return true;
}

bool compare_reference(Image& image, const fs::path& root, const char* path) {
    Error error;
    FileId file;
    if (!image.find(path, file, error)) return false;
    std::ifstream reference(root / fs::u8path(path), std::ios::binary | std::ios::ate);
    if (!reference || reference.tellg() < 0 ||
        static_cast<uint64_t>(reference.tellg()) != image.entry(file)->byte_size)
        return false;
    reference.seekg(0);
    std::array<char, 4096> expected{}, actual{};
    uint64_t offset = 0;
    while (offset < image.entry(file)->byte_size) {
        const size_t count = static_cast<size_t>(std::min<uint64_t>(
            expected.size(), image.entry(file)->byte_size - offset));
        reference.read(expected.data(), static_cast<std::streamsize>(count));
        if (reference.gcount() != static_cast<std::streamsize>(count) ||
            !image.read_at(file, offset, actual.data(), count, error) ||
            std::memcmp(expected.data(), actual.data(), count) != 0)
            return false;
        offset += count;
    }
    return true;
}

int corpus() {
    const char* one = std::getenv("DREAMS_CUE1");
    const char* two = std::getenv("DREAMS_CUE2");
    if (!one || !two || !*one || !*two) {
        std::cout << "SKIP: set DREAMS_CUE1 and DREAMS_CUE2 for corpus checks\n";
        return 77;
    }
    const std::array<const char*, 2> cues{one, two};
    const std::array<size_t, 2> files{1219, 409};
    const std::array<size_t, 2> directories{27, 26};
    const std::array<size_t, 2> tracks{12, 14};
    const std::array<uint64_t, 2> dreams_sizes{138879, 138835};
    std::array<std::unique_ptr<Image>, 2> images;
    for (size_t i = 0; i < cues.size(); ++i) {
        Error error;
        auto image = Image::open(fs::u8path(cues[i]), error);
        if (!image || image->file_count() != files[i] ||
            image->directory_count() != directories[i] ||
            image->tracks().size() != tracks[i] ||
            image->identity() != (i == 0 ? Identity::disc1 : Identity::disc2)) {
            std::cerr << "Disc " << i + 1 << " failed: " << error.message << "\n";
            return 1;
        }
        for (const auto& track : image->tracks()) {
            if (track.status != TrackStatus::available) return 1;
        }
        FileId data;
        if (!image->find("DREAMS.DAT", data, error) ||
            image->entry(data)->byte_size != dreams_sizes[i]) return 1;
        std::array<uint8_t, 8> bytes{};
        if (!image->read_at(data, 0, bytes.data(), bytes.size(), error) ||
            bytes != std::array<uint8_t, 8>{0, 0, 0, 0, 0x44, 0x08, 0, 0}) return 1;
        if (i == 1 && image->tracks()[4].index01_frames != 151) return 1;
        const char* reference = std::getenv(i == 0 ? "DREAMS_DISC1" : "DREAMS_DISC2");
        if (reference && *reference) {
            const auto root = fs::u8path(reference);
            if (!compare_reference(*image, root, "DREAMS.DAT") ||
                !compare_reference(*image, root, "DATA/ICONE/ICONES.BF")) {
                std::cerr << "Disc " << i + 1 << " reference bytes differ\n";
                return 1;
            }
        }
        std::cout << "Disc " << i + 1 << ": " << image->file_count() << " files, "
                  << image->directory_count() << " directories, "
                  << image->tracks().size() << " tracks\n";
        images[i] = std::move(image);
    }
    Error error;
    FileId intro1, intro2;
    if (!images[0]->find("DATA/HNM/INTRO.HNM", intro1, error) ||
        !images[1]->find("DATA/HNM/INTRO.HNM", intro2, error) ||
        images[0]->entry(intro1)->byte_size == images[1]->entry(intro2)->byte_size)
        return 1;
    std::array<uint8_t, 1> byte{};
    if (images[1]->read_at(intro1, 0, byte.data(), byte.size(), error) ||
        error.code != ErrorCode::stale_file) return 1;
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--corpus") == 0) return corpus();
    if (argc != 1) return 2;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path base = fs::temp_directory_path() /
        ("opendreams-disc-tests-" + std::to_string(stamp));
    fs::create_directories(base);
    const bool mount_okay = test_mount_and_read(base);
    const bool markers_okay = mount_okay && test_markers_and_offsets(base);
    const bool okay = markers_okay && test_failures(base);
    std::error_code ignored;
    fs::remove_all(base, ignored);
    return okay ? 0 : 1;
}

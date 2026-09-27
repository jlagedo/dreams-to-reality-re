#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/dan.h"
#include "port/drd.h"
#include "port/fsb.h"
#include "port/dsn.h"
#include "port/stream.h"
#include "port/vfs.h"
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

void little32(uint8_t* out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out[i] = static_cast<uint8_t>(value >> (i * 8));
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
    bool bf_archives = false;
    bool dsn_header = false;
    bool dan_archive = false;
    bool drd_bank = false;
    bool fsb_bank = false;
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
    pos = record(root, pos, 27, 2048, 2, "EMPTY");
    if (options.bf_archives) {
        pos = record(root, pos, 29, 598, 0, "ONE.BF;1");
        pos = record(root, pos, 30, 598, 0, "TWO.BF;1");
    }
    if (options.dsn_header) record(root, pos, 31, 128, 0, "TEST.DSN;1");
    if (options.dan_archive) record(root, pos, 31, 94, 0, "TEST.DAN;1");
    if (options.drd_bank) record(root, pos, 31, 128, 0, "TEST.DRD;1");
    if (options.fsb_bank) record(root, pos, 31, 56, 0, "TEST.FSB;1");
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

    if (options.bf_archives) {
        const auto write_bf = [&](size_t sector, const char* first_payload,
                                  const char* second_payload, const char* second_name) {
            auto* bf = iso.data() + sector * 2048;
            std::memcpy(bf, "UBIK", 4);
            bf[4] = 2;
            bf[8] = 64; // Table offset.
            bf[12] = 2; // Two 267-byte rows.
            std::memcpy(bf + 16, first_payload, 5);
            std::memcpy(bf + 32, second_payload, 6);
            std::memcpy(bf + 64, "ONE.TXT", 7);
            bf[64 + 259] = 16;
            bf[64 + 263] = 5;
            std::memcpy(bf + 64 + 267, second_name, std::strlen(second_name));
            bf[64 + 267 + 259] = 32;
            bf[64 + 267 + 263] = 6;
        };
        write_bf(29, "first", "second", "TWO.TXT");
        write_bf(30, "new!!", "third!", "THREE.TXT");
    }
    if (options.dsn_header) {
        auto* dsn = iso.data() + 31 * 2048;
        std::memcpy(dsn, "DSNF", 4);
        little32(dsn + 5, 128);
        little32(dsn + 10, 69); // 31 * 2 names + 7.
        little16(dsn + 14, 2);
        std::memcpy(dsn + 16, "ROOM_A", 6);
        std::memcpy(dsn + 27, "ROOM_B", 6);
        for (uint32_t word = 0; word < 5; ++word) {
            little32(dsn + 38 + word * 4, word + 1);
            little32(dsn + 58 + word * 4, word + 10);
        }
        dsn[78] = 1; // First packed-body tag after the two header tables.
    }
    if (options.dan_archive) {
        auto* dan = iso.data() + 31 * 2048;
        std::memcpy(dan, "DANF", 4);
        little32(dan + 5, 94);
        little32(dan + 10, 46); // 55-byte body offset minus nine.
        little16(dan + 14, 1);
        std::memcpy(dan + 16, "MODEL", 5);
        little16(dan + 27, 2);
        std::memcpy(dan + 29, "CLIP000.3DA", 11);
        std::memcpy(dan + 42, "CLIP002.3DA", 11);
        dan[55] = 1; little32(dan + 56, 9); std::memcpy(dan + 60, "modl", 4);
        dan[64] = 2; little32(dan + 65, 9); std::memcpy(dan + 69, "tex0", 4);
        dan[73] = 3; little32(dan + 74, 10); std::memcpy(dan + 78, "first", 5);
        dan[83] = 3; little32(dan + 84, 11); std::memcpy(dan + 88, "second", 6);
    }
    if (options.drd_bank) {
        auto* drd = iso.data() + 31 * 2048;
        std::memcpy(drd, "DRDF", 4);
        little32(drd + 4, 128);
        little32(drd + 8, 2);
        little32(drd + 12, 55);
        drd[16] = 0; little32(drd + 17, 13);
        little32(drd + 21, 29); little32(drd + 25, 84);
        auto* first = drd + 29;
        first[0] = 1; little32(first + 1, 55); little32(first + 5, 1);
        first[9] = 2; little32(first + 10, 21);
        std::memcpy(first + 14, "RIFF", 4); little32(first + 18, 8);
        std::memcpy(first + 22, "WAVE", 4);
        first[30] = 3; little32(first + 31, 16);
        first[39] = 6; std::memcpy(first + 40, "Hello", 5);
        first[46] = 4; little32(first + 47, 9);
        std::memcpy(first + 51, "FACE", 4);
        auto* second = drd + 84;
        second[0] = 1; little32(second + 1, 44); little32(second + 5, 1);
        second[9] = 2; little32(second + 10, 21);
        std::memcpy(second + 14, "RIFF", 4); little32(second + 18, 8);
        std::memcpy(second + 22, "WAVE", 4);
        second[30] = 3; little32(second + 31, 14);
        second[39] = 4; std::memcpy(second + 40, "Bye", 3);
    }
    if (options.fsb_bank) {
        auto* fsb = iso.data() + 31 * 2048;
        std::memcpy(fsb, "DREAMS FSB  ", 12);
        little32(fsb + 12, 2);
        little32(fsb + 16, 16);
        little32(fsb + 20, 16);
        std::memcpy(fsb + 24, "RIFF", 4); little32(fsb + 28, 8);
        std::memcpy(fsb + 32, "WAVE", 4); fsb[39] = 'A';
        std::memcpy(fsb + 40, "RIFF", 4); little32(fsb + 44, 8);
        std::memcpy(fsb + 48, "WAVE", 4); fsb[55] = 'B';
    }

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

bool test_vfs_and_bf(const fs::path& base) {
    Options options;
    options.bf_archives = true;
    const auto fixture = make_fixture(base / "vfs-and-bf", options);
    Error disc_error;
    auto opened = Image::open(fixture.cue, disc_error);
    CHECK(opened != nullptr);
    std::shared_ptr<const Image> image(std::move(opened));
    od::port::VfsContext vfs(image);
    od::port::VfsError error;
    int32_t handle = 0;
    CHECK(od::port::VFS_Open(vfs, "x:\\dreams.dat", 0x200, handle, error));
    CHECK(handle > 0);
    uint64_t position = 0;
    CHECK(od::port::VFS_Seek(vfs, handle, 2040, 0, position, error));
    CHECK(position == 2040);
    std::array<uint8_t, 32> bytes{};
    size_t count = 0;
    CHECK(od::port::VFS_Read(vfs, handle, bytes.data(), bytes.size(), count, error));
    CHECK(count == bytes.size());
    for (size_t i = 0; i < bytes.size(); ++i)
        CHECK(bytes[i] == static_cast<uint8_t>((2040 + i) % 251));
    CHECK(od::port::VFS_Seek(vfs, handle, -10, 2, position, error));
    CHECK(position == 2290);
    CHECK(od::port::VFS_Read(vfs, handle, bytes.data(), bytes.size(), count, error));
    CHECK(count == 10);
    CHECK(od::port::VFS_Close(vfs, handle, error));
    CHECK(!od::port::VFS_Read(vfs, handle, bytes.data(), 1, count, error));
    CHECK(error.code == od::port::VfsErrorCode::invalid_handle);
    CHECK(!od::port::VFS_Open(vfs, "DREAMS.DAT", 0x20, handle, error));
    CHECK(error.code == od::port::VfsErrorCode::invalid_mode);
    CHECK(od::port::VFS_Open(vfs, "X:\\CRYO\\DREAMS\\DATA\\1CD.ID", 0x200,
                             handle, error));
    CHECK(od::port::VFS_Read(vfs, handle, bytes.data(), 5, count, error));
    CHECK(count == 5 && std::memcmp(bytes.data(), "kjk\r\n", 5) == 0);
    CHECK(od::port::VFS_Close(vfs, handle, error));

    std::vector<od::port::BfEntry> rows;
    CHECK(od::port::BF_Mount(vfs, "ONE.BF", rows, error));
    CHECK(rows.size() == 2 && vfs.registered_member_count() == 2);
    CHECK(rows[0].row == 0 && rows[0].name == "ONE.TXT");
    CHECK(rows[1].row == 1 && rows[1].name == "TWO.TXT");
    CHECK(od::port::VFS_Open(vfs, "one.txt", 0x200, handle, error));
    CHECK(handle == -1);
    CHECK(od::port::VFS_Read(vfs, handle, bytes.data(), bytes.size(), count, error));
    CHECK(count == 5 && std::memcmp(bytes.data(), "first", 5) == 0);
    CHECK(od::port::VFS_Seek(vfs, handle, 0, 2, position, error));
    CHECK(position == 4); // Retail member seek clamps to length - 1.
    CHECK(od::port::VFS_Read(vfs, handle, bytes.data(), 1, count, error));
    CHECK(count == 1 && bytes[0] == 't');
    CHECK(od::port::VFS_Close(vfs, handle, error));
    CHECK(od::port::VFS_Open(vfs, "TWO.TXT", 0x200, handle, error));
    CHECK(handle == -2);
    CHECK(od::port::VFS_Read(vfs, handle, bytes.data(), bytes.size(), count, error));
    CHECK(count == 6 && std::memcmp(bytes.data(), "second", 6) == 0);

    CHECK(od::port::BF_Mount(vfs, "TWO.BF", rows, error));
    CHECK(rows.size() == 2 && vfs.registered_member_count() == 3);
    CHECK(rows[0].name == "ONE.TXT" && rows[1].name == "THREE.TXT");
    CHECK(od::port::VFS_Open(vfs, "ONE.TXT", 0x200, handle, error));
    CHECK(handle == -1); // Later mount replaces the same table slot.
    CHECK(od::port::VFS_Read(vfs, handle, bytes.data(), bytes.size(), count, error));
    CHECK(count == 5 && std::memcmp(bytes.data(), "new!!", 5) == 0);
    CHECK(od::port::VFS_Open(vfs, "THREE.TXT", 0x200, handle, error));
    CHECK(handle == -3);
    CHECK(od::port::VFS_Read(vfs, handle, bytes.data(), bytes.size(), count, error));
    CHECK(count == 6 && std::memcmp(bytes.data(), "third!", 6) == 0);
    od::port::VFS_FreeArchives(vfs);
    CHECK(vfs.registered_member_count() == 0);
    CHECK(!od::port::VFS_Read(vfs, handle, bytes.data(), 1, count, error));
    CHECK(error.code == od::port::VfsErrorCode::invalid_handle);
    od::port::VfsContext independent(image);
    CHECK(!od::port::VFS_Open(independent, "ONE.TXT", 0x200, handle, error));
    CHECK(error.code == od::port::VfsErrorCode::missing_file);
    od::port::VfsContext precedence(image);
    od::port::VFS_AddArchiveEntries(precedence,
                                    {{0, "EXTRA.TXT", 16, 5, rows[0].archive_file}});
    CHECK(od::port::VFS_Open(precedence, "EXTRA.TXT", 0x200, handle, error));
    CHECK(handle > 0); // A loose ISO file wins over a member with the same name.
    CHECK(od::port::VFS_Read(precedence, handle, bytes.data(), 4, count, error));
    CHECK(count == 4 && std::memcmp(bytes.data(), "more", 4) == 0);
    CHECK(od::port::VFS_Close(precedence, handle, error));
    od::port::VFS_AddArchiveEntries(precedence,
                                    {{1, "EMPTY.DAT", 0, 0, rows[0].archive_file}});
    CHECK(od::port::VFS_Open(precedence, "EMPTY.DAT", 0x200, handle, error));
    CHECK(handle < 0);
    CHECK(od::port::VFS_Seek(precedence, handle, 0, 2, position, error));
    CHECK(position == 0);
    CHECK(od::port::VFS_Read(precedence, handle, bytes.data(), 1, count, error));
    CHECK(count == 0);

    const auto bad = make_fixture(base / "bad-bf", options);
    {
        std::fstream raw(bad.data, std::ios::binary | std::ios::in | std::ios::out);
        raw.seekp(29 * 2352 + 16 + 8); // UBIK table offset in the ISO payload.
        const std::array<char, 4> outside{'\xff', '\xff', '\xff', '\x7f'};
        raw.write(outside.data(), outside.size());
    }
    auto bad_image = Image::open(bad.cue, disc_error);
    CHECK(bad_image != nullptr);
    od::port::VfsContext invalid(std::shared_ptr<const Image>(std::move(bad_image)));
    CHECK(!od::port::BF_Mount(invalid, "ONE.BF", rows, error));
    CHECK(error.code == od::port::VfsErrorCode::malformed_archive);
    CHECK(rows.empty() && invalid.registered_member_count() == 0);
    return true;
}

bool test_stream_and_dsn(const fs::path& base) {
    Options options;
    options.dsn_header = true;
    const auto fixture = make_fixture(base / "stream-dsn", options);
    Error disc_error;
    auto opened = Image::open(fixture.cue, disc_error);
    CHECK(opened != nullptr);
    std::shared_ptr<const Image> image(std::move(opened));
    od::port::VfsContext vfs(image);
    od::port::StreamError stream_error;
    auto stream = od::port::STRM_Create(vfs, 64, 64, 32, stream_error);
    CHECK(stream != nullptr && stream->ring_size() == 64);
    CHECK(od::port::STRM_Open(*stream, "DREAMS.DAT", stream_error));
    CHECK(od::port::STRM_Fill(*stream, stream_error));
    CHECK(stream->available() == 32);
    const uint8_t* bytes = nullptr;
    CHECK(!od::port::STRM_Peek(*stream, 33, bytes, stream_error));
    CHECK(stream_error.code == od::port::StreamErrorCode::invalid_peek);
    CHECK(od::port::STRM_Peek(*stream, 10, bytes, stream_error));
    for (size_t i = 0; i < 10; ++i) CHECK(bytes[i] == i % 251);
    CHECK(od::port::STRM_Commit(*stream, stream_error));
    CHECK(od::port::STRM_Fill(*stream, stream_error));
    CHECK(od::port::STRM_Peek(*stream, 50, bytes, stream_error));
    for (size_t i = 0; i < 50; ++i) CHECK(bytes[i] == (i + 10) % 251);
    CHECK(od::port::STRM_Commit(*stream, stream_error));
    CHECK(od::port::STRM_Fill(*stream, stream_error));
    CHECK(od::port::STRM_Peek(*stream, 8, bytes, stream_error));
    for (size_t i = 0; i < 8; ++i) CHECK(bytes[i] == (i + 60) % 251);
    CHECK(od::port::STRM_Commit(*stream, stream_error));
    CHECK(od::port::STRM_Close(*stream, stream_error));
    od::port::STRM_Free(stream);
    CHECK(stream == nullptr);

    stream = od::port::STRM_Create(vfs, 0x57800, 0x57800, 0x8000, stream_error);
    CHECK(stream != nullptr && stream->ring_size() == 0x58000);
    od::port::DsnState state;
    CHECK(od::port::DSN_InitState(state, *stream));
    od::port::DsnError dsn_error;
    CHECK(od::port::DSN_LoadHeader(state, "TEST.DSN", dsn_error));
    CHECK(state.loaded() && state.declared_size() == 128 && state.span() == 69);
    CHECK(state.name_count() == 2 && state.body_offset() == 78);
    CHECK(state.objects().size() == 2);
    CHECK(state.objects()[0].name == "ROOM_A" && state.objects()[0].words[4] == 5);
    CHECK(state.objects()[1].name == "ROOM_B" && state.objects()[1].words[0] == 10);
    CHECK(od::port::STRM_Peek(*stream, 1, bytes, stream_error));
    CHECK(bytes[0] == 1); // The port leaves the stream at the first body tag.
    CHECK(od::port::STRM_Commit(*stream, stream_error));
    od::port::DSN_ResetState(state);
    CHECK(!state.loaded() && state.stream() == stream.get() && state.objects().empty());
    od::port::STRM_Free(stream);

    const auto bad = make_fixture(base / "bad-dsn", options);
    {
        std::fstream raw(bad.data, std::ios::binary | std::ios::in | std::ios::out);
        raw.seekp(31 * 2352 + 16);
        raw.write("NOPE", 4);
    }
    auto bad_image = Image::open(bad.cue, disc_error);
    CHECK(bad_image != nullptr);
    od::port::VfsContext invalid(std::shared_ptr<const Image>(std::move(bad_image)));
    stream = od::port::STRM_Create(invalid, 0x57800, 0x57800, 0x8000, stream_error);
    CHECK(stream != nullptr && od::port::DSN_InitState(state, *stream));
    CHECK(!od::port::DSN_LoadHeader(state, "TEST.DSN", dsn_error));
    CHECK(dsn_error.code == od::port::DsnErrorCode::invalid_header);
    CHECK(!state.loaded());
    od::port::STRM_Free(stream);

    const auto bad_count = make_fixture(base / "bad-dsn-count", options);
    {
        std::fstream raw(bad_count.data, std::ios::binary | std::ios::in | std::ios::out);
        raw.seekp(31 * 2352 + 16 + 14);
        raw.put(33);
    }
    bad_image = Image::open(bad_count.cue, disc_error);
    CHECK(bad_image != nullptr);
    od::port::VfsContext invalid_count(std::shared_ptr<const Image>(std::move(bad_image)));
    stream = od::port::STRM_Create(invalid_count, 0x57800, 0x57800, 0x8000,
                                    stream_error);
    CHECK(stream != nullptr && od::port::DSN_InitState(state, *stream));
    CHECK(!od::port::DSN_LoadHeader(state, "TEST.DSN", dsn_error));
    CHECK(dsn_error.code == od::port::DsnErrorCode::invalid_header);
    od::port::STRM_Free(stream);
    return true;
}

bool test_dan_archive(const fs::path& base) {
    Options options;
    options.dan_archive = true;
    const auto fixture = make_fixture(base / "dan-archive", options);
    Error disc_error;
    auto opened = Image::open(fixture.cue, disc_error);
    CHECK(opened != nullptr);
    std::shared_ptr<const Image> image(std::move(opened));
    od::port::VfsContext vfs(image);
    od::port::DanArchive dan(vfs);
    od::port::DanError error;
    CHECK(od::port::DAN_OpenArchive(dan, "test.dan", error));
    CHECK(dan.is_open() && dan.declared_size() == 94 && dan.span() == 46);
    CHECK(dan.body_offset() == 55 && dan.names().size() == 1);
    CHECK(dan.names()[0].text == "MODEL");
    CHECK(od::port::DAN_GetAnimCount(dan) == 2);
    CHECK(od::port::DAN_GetAnimName(dan, 0) == "CLIP000.3DA");
    CHECK(od::port::DAN_GetAnimName(dan, 1) == "CLIP002.3DA");
    CHECK(od::port::DAN_GetAnimName(dan, 2).empty());
    CHECK(od::port::DAN_ReadAnimChunks(dan, error));
    CHECK(dan.animations_loaded() && dan.animation_chunks().size() == 2);
    CHECK(dan.animation_chunks()[0].file_offset == 73);
    CHECK(dan.animation_chunks()[0].payload_size == 5);
    CHECK(dan.animation_chunks()[1].file_offset == 83);
    CHECK(dan.animation_chunks()[1].payload_size == 6);
    CHECK(std::memcmp(dan.animation_work().data(), "firstsecond", 11) == 0);
    od::port::DAN_CloseArchive(dan);
    CHECK(!dan.is_open() && od::port::DAN_GetAnimCount(dan) == 0);
    CHECK(!od::port::DAN_OpenArchive(dan, "MISSING.DAN", error));
    CHECK(error.code == od::port::DanErrorCode::missing_file);

    const auto bad = make_fixture(base / "bad-dan-chunk", options);
    {
        std::fstream raw(bad.data, std::ios::binary | std::ios::in | std::ios::out);
        raw.seekp(31 * 2352 + 16 + 74); // First tag-3 chunk size.
        const std::array<char, 4> outside{'\xff', '\xff', '\xff', '\x7f'};
        raw.write(outside.data(), outside.size());
    }
    auto bad_image = Image::open(bad.cue, disc_error);
    CHECK(bad_image != nullptr);
    od::port::VfsContext invalid(std::shared_ptr<const Image>(std::move(bad_image)));
    od::port::DanArchive bad_dan(invalid);
    CHECK(od::port::DAN_OpenArchive(bad_dan, "TEST.DAN", error));
    CHECK(!od::port::DAN_ReadAnimChunks(bad_dan, error));
    CHECK(error.code == od::port::DanErrorCode::invalid_chunk);
    CHECK(!bad_dan.animations_loaded());
    return true;
}

bool test_drd_bank(const fs::path& base) {
    Options options;
    options.drd_bank = true;
    const auto fixture = make_fixture(base / "drd-bank", options);
    Error disc_error;
    auto opened = Image::open(fixture.cue, disc_error);
    CHECK(opened != nullptr);
    std::shared_ptr<const Image> image(std::move(opened));
    od::port::VfsContext vfs(image);
    od::port::DrdBank drd(vfs);
    od::port::DrdError error;
    CHECK(od::port::DRD_Open(drd, "TEST.DRD", error));
    CHECK(drd.is_open() && drd.entry_count() == 2);
    CHECK(drd.entry_offset(0) == 29 && drd.entry_size(0) == 55);
    CHECK(drd.entry_offset(1) == 84 && drd.entry_size(1) == 44);
    CHECK(od::port::DRD_LoadEntry(drd, 0, error));
    CHECK(drd.current_index() == 0 && od::port::DRD_GetLineCount(drd) == 1);
    CHECK(drd.line_text(0) == "Hello" && drd.lines()[0].ticks_15hz == 0);
    CHECK(drd.wave().size == 16 && std::memcmp(drd.wave().data, "RIFF", 4) == 0);
    const uint8_t* reused_wave = drd.wave().data;
    const auto portrait = od::port::DRD_GetPortrait(drd);
    CHECK(portrait.size == 4 && std::memcmp(portrait.data, "FACE", 4) == 0);
    CHECK(od::port::DRD_LoadEntry(drd, 0, error));
    CHECK(drd.wave().data == reused_wave);
    CHECK(od::port::DRD_LoadEntry(drd, 1, error));
    CHECK(drd.wave().data == reused_wave && drd.line_text(0) == "Bye");
    CHECK(od::port::DRD_GetPortrait(drd).size == 0);
    od::port::DRD_Close(drd);
    CHECK(!drd.is_open() && drd.entry_count() == 0);

    const auto bad = make_fixture(base / "bad-drd-table", options);
    {
        std::fstream raw(bad.data, std::ios::binary | std::ios::in | std::ios::out);
        raw.seekp(31 * 2352 + 16 + 21);
        raw.put(30); // Entry zero must start immediately after the offset table.
    }
    auto bad_image = Image::open(bad.cue, disc_error);
    CHECK(bad_image != nullptr);
    od::port::VfsContext invalid(std::shared_ptr<const Image>(std::move(bad_image)));
    od::port::DrdBank bad_drd(invalid);
    CHECK(!od::port::DRD_Open(bad_drd, "TEST.DRD", error));
    CHECK(error.code == od::port::DrdErrorCode::invalid_header);
    return true;
}

bool test_fsb_bank(const fs::path& base) {
    Options options;
    options.fsb_bank = true;
    const auto fixture = make_fixture(base / "fsb-bank", options);
    Error disc_error;
    auto opened = Image::open(fixture.cue, disc_error);
    CHECK(opened != nullptr);
    std::shared_ptr<const Image> image(std::move(opened));
    od::port::VfsContext vfs(image);
    od::port::FsbBank fsb(vfs);
    od::port::FsbError error;
    CHECK(od::port::FSB_Load(fsb, "TEST.FSB", error));
    CHECK(fsb.loaded() && fsb.clips().size() == 2);
    CHECK(fsb.clips()[0].file_offset == 24 && fsb.clips()[0].byte_size == 16);
    CHECK(fsb.clips()[1].file_offset == 40 && fsb.clips()[1].byte_size == 16);
    const auto first = od::port::FSB_GetSample(fsb, 0);
    const auto second = od::port::FSB_GetSample(fsb, 1);
    CHECK(first.size == 16 && second.size == 16);
    CHECK(std::memcmp(first.data, "RIFF", 4) == 0 && first.data[15] == 'A');
    CHECK(std::memcmp(second.data, "RIFF", 4) == 0 && second.data[15] == 'B');
    CHECK(od::port::FSB_GetSample(fsb, 2).size == 0);
    od::port::FSB_Free(fsb);
    CHECK(!fsb.loaded() && fsb.clips().empty());
    CHECK(!od::port::FSB_Load(fsb, "MISSING.FSB", error));
    CHECK(error.code == od::port::FsbErrorCode::missing_file);

    const auto bad = make_fixture(base / "bad-fsb", options);
    {
        std::fstream raw(bad.data, std::ios::binary | std::ios::in | std::ios::out);
        raw.seekp(31 * 2352 + 16 + 16);
        const std::array<char, 4> outside{'\xff', '\xff', '\xff', '\x7f'};
        raw.write(outside.data(), outside.size());
    }
    auto bad_image = Image::open(bad.cue, disc_error);
    CHECK(bad_image != nullptr);
    od::port::VfsContext invalid(std::shared_ptr<const Image>(std::move(bad_image)));
    od::port::FsbBank bad_fsb(invalid);
    CHECK(!od::port::FSB_Load(bad_fsb, "TEST.FSB", error));
    CHECK(error.code == od::port::FsbErrorCode::invalid_table);
    CHECK(!bad_fsb.loaded());
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

bool compare_reference(const Image& image, const fs::path& root, const char* path) {
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

uint32_t crc32_update(uint32_t value, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        value ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0u);
    }
    return value;
}

std::string uppercase_path(std::string path) {
    for (char& c : path)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return path;
}

bool validate_dan_corpus(const Image& source, od::port::VfsContext& vfs,
                         size_t disc_index) {
    std::vector<const od::disc::Entry*> files;
    for (const auto& entry : source.entries()) {
        const std::string path = uppercase_path(entry.path);
        if (entry.kind == EntryKind::file && path.size() >= 4 &&
            path.substr(path.size() - 4) == ".DAN") files.push_back(&entry);
    }
    std::sort(files.begin(), files.end(), [](const auto* a, const auto* b) {
        return uppercase_path(a->path) < uppercase_path(b->path);
    });
    const size_t expected_files = disc_index == 0 ? 111 : 80;
    const size_t expected_names = disc_index == 0 ? 170 : 142;
    const size_t expected_frames = disc_index == 0 ? 550 : 498;
    const size_t expected_payload_bytes = disc_index == 0 ? 1939827 : 1754479;
    const uint32_t expected_metadata_crc = disc_index == 0 ? 0x05c1a513u : 0xc9215b67u;
    const uint32_t expected_payload_crc = disc_index == 0 ? 0x38fec8d1u : 0xa25c1768u;
    if (files.size() != expected_files) return false;
    od::port::DanArchive dan(vfs);
    od::port::DanError error;
    uint32_t metadata_crc = 0xffffffffu, payload_crc = 0xffffffffu;
    size_t names = 0, frames = 0, payload_bytes = 0;
    for (const auto* file : files) {
        if (!od::port::DAN_OpenArchive(dan, file->path, error) ||
            dan.declared_size() != file->byte_size ||
            dan.body_offset() != 9u + dan.span() ||
            !od::port::DAN_ReadAnimChunks(dan, error) || !dan.animations_loaded() ||
            dan.animation_chunks().size() != od::port::DAN_GetAnimCount(dan)) {
            std::cerr << "DAN " << file->path << ": " << error.message << "\n";
            return false;
        }
        names += dan.names().size();
        frames += dan.clips().size();
        const std::string path = uppercase_path(file->path);
        metadata_crc = crc32_update(metadata_crc, path.data(), path.size());
        const uint8_t zero = 0;
        metadata_crc = crc32_update(metadata_crc, &zero, 1);
        std::array<uint8_t, 12> fields{};
        little32(fields.data(), dan.declared_size());
        little32(fields.data() + 4, dan.span());
        little16(fields.data() + 8, static_cast<uint16_t>(dan.names().size()));
        little16(fields.data() + 10, static_cast<uint16_t>(dan.clips().size()));
        metadata_crc = crc32_update(metadata_crc, fields.data(), fields.size());
        for (const auto& name : dan.names())
            metadata_crc = crc32_update(metadata_crc, name.raw.data(), name.raw.size());
        std::array<uint8_t, 2> frame_count{};
        little16(frame_count.data(), static_cast<uint16_t>(dan.clips().size()));
        metadata_crc = crc32_update(metadata_crc, frame_count.data(), frame_count.size());
        for (size_t i = 0; i < dan.clips().size(); ++i) {
            if (od::port::DAN_GetAnimName(dan, i) != dan.clips()[i].text) return false;
            metadata_crc = crc32_update(metadata_crc, dan.clips()[i].raw.data(),
                                        dan.clips()[i].raw.size());
        }
        for (const auto& chunk : dan.animation_chunks()) {
            if (chunk.work_offset + chunk.payload_size > dan.animation_work().size())
                return false;
            payload_crc = crc32_update(payload_crc,
                dan.animation_work().data() + chunk.work_offset, chunk.payload_size);
            payload_bytes += chunk.payload_size;
        }
    }
    od::port::DAN_CloseArchive(dan);
    metadata_crc ^= 0xffffffffu;
    payload_crc ^= 0xffffffffu;
    if (names != expected_names || frames != expected_frames ||
        payload_bytes != expected_payload_bytes ||
        metadata_crc != expected_metadata_crc || payload_crc != expected_payload_crc) {
        std::cerr << "Disc " << disc_index + 1 << " DAN values differ from Python oracle\n";
        return false;
    }
    std::cout << "Disc " << disc_index + 1 << ": " << files.size()
              << " DAN archives, " << frames << " animation chunks\n";
    return true;
}

bool validate_drd_corpus(od::port::VfsContext& vfs, size_t disc_index) {
    od::port::DrdBank drd(vfs);
    od::port::DrdError error;
    if (disc_index == 1) {
        if (od::port::DRD_Open(drd, "DATA/3DC/DIALOG.DRD", error) ||
            error.code != od::port::DrdErrorCode::missing_file) return false;
        return true;
    }
    if (!od::port::DRD_Open(drd, "DATA/3DC/DIALOG.DRD", error) ||
        drd.entry_count() != 178 || drd.entry_offset(0) != 733 ||
        drd.entry_offset(122) != 16838184 ||
        drd.entry_offset(123) != 16956475) {
        std::cerr << "DRD open: " << error.message << "\n";
        return false;
    }
    uint32_t offset_crc = 0xffffffffu, wave_crc = 0xffffffffu;
    uint32_t portrait_crc = 0xffffffffu, caption_crc = 0xffffffffu;
    size_t lines = 0, portrait_count = 0, wave_bytes = 0, portrait_bytes = 0;
    for (size_t i = 0; i < drd.entry_count(); ++i) {
        std::array<uint8_t, 8> fields{};
        little32(fields.data(), static_cast<uint32_t>(drd.entry_offset(i)));
        little32(fields.data() + 4, static_cast<uint32_t>(drd.entry_size(i)));
        offset_crc = crc32_update(offset_crc, fields.data(), fields.size());
        if (!od::port::DRD_LoadEntry(drd, i, error)) {
            std::cerr << "DRD entry " << i << ": " << error.message << "\n";
            return false;
        }
        const auto wave = drd.wave();
        if (!wave.data || wave.size < 12 ||
            std::memcmp(wave.data, "RIFF", 4) != 0) return false;
        wave_crc = crc32_update(wave_crc, wave.data, wave.size);
        wave_bytes += wave.size;
        const auto portrait = od::port::DRD_GetPortrait(drd);
        portrait_crc = crc32_update(portrait_crc, portrait.data, portrait.size);
        portrait_bytes += portrait.size;
        if (portrait.size != 0) ++portrait_count;
        lines += od::port::DRD_GetLineCount(drd);
        for (size_t j = 0; j < od::port::DRD_GetLineCount(drd); ++j) {
            const auto& line = drd.lines()[j];
            if (line.ticks_15hz != static_cast<uint32_t>(
                    static_cast<uint64_t>(line.centiseconds) * 15 / 100)) return false;
            std::array<uint8_t, 4> timing{};
            little32(timing.data(), line.centiseconds);
            caption_crc = crc32_update(caption_crc, timing.data(), timing.size());
            const auto text = drd.line_text(j);
            caption_crc = crc32_update(caption_crc, text.data(), text.size());
            const uint8_t zero = 0;
            caption_crc = crc32_update(caption_crc, &zero, 1);
        }
    }
    offset_crc ^= 0xffffffffu;
    wave_crc ^= 0xffffffffu;
    portrait_crc ^= 0xffffffffu;
    caption_crc ^= 0xffffffffu;
    if (lines != 589 || portrait_count != 169 || wave_bytes != 17848529 ||
        portrait_bytes != 6719761 || offset_crc != 0xac7173e2u ||
        wave_crc != 0x290bc860u || portrait_crc != 0xcdf34aa1u ||
        caption_crc != 0x0c29112au) {
        std::cerr << "DRD payloads differ from Python oracle\n";
        return false;
    }
    od::port::DRD_Close(drd);
    std::cout << "Disc 1: 178 DRD entries, 589 captions, 169 portraits\n";
    return true;
}

bool validate_fsb_corpus(od::port::VfsContext& vfs, size_t disc_index) {
    od::port::FsbBank fsb(vfs);
    od::port::FsbError error;
    if (!od::port::FSB_Load(fsb, "DATA/SOUND/FSB.DAT", error) ||
        fsb.clips().size() != 24) {
        std::cerr << "Disc " << disc_index + 1 << " FSB: " << error.message << "\n";
        return false;
    }
    uint32_t table_crc = 0xffffffffu, payload_crc = 0xffffffffu;
    uint64_t payload_bytes = 0;
    for (size_t i = 0; i < fsb.clips().size(); ++i) {
        const auto& clip = fsb.clips()[i];
        const auto sample = od::port::FSB_GetSample(fsb, i);
        if (clip.index != i || sample.size != clip.byte_size ||
            sample.size < 12 || std::memcmp(sample.data, "RIFF", 4) != 0)
            return false;
        std::array<uint8_t, 8> fields{};
        little32(fields.data(), static_cast<uint32_t>(clip.file_offset));
        little32(fields.data() + 4, clip.byte_size);
        table_crc = crc32_update(table_crc, fields.data(), fields.size());
        payload_crc = crc32_update(payload_crc, sample.data, sample.size);
        payload_bytes += sample.size;
    }
    table_crc ^= 0xffffffffu;
    payload_crc ^= 0xffffffffu;
    if (payload_bytes != 741258 || table_crc != 0x6b8c8d72u ||
        payload_crc != 0x0b02b576u ||
        fsb.clips().back().file_offset + fsb.clips().back().byte_size != 741370)
        return false;
    od::port::FSB_Free(fsb);
    std::cout << "Disc " << disc_index + 1 << ": 24 FSB sound clips\n";
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
    std::array<std::shared_ptr<const Image>, 2> images;
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
        std::shared_ptr<const Image> source(std::move(image));
        od::port::VfsContext vfs(source);
        od::port::VfsError vfs_error;
        if (!validate_dan_corpus(*source, vfs, i)) return 1;
        if (!validate_drd_corpus(vfs, i)) return 1;
        if (!validate_fsb_corpus(vfs, i)) return 1;
        int32_t handle = 0;
        if (!od::port::VFS_Open(vfs, "X:\\DREAMS.DAT", 0x200, handle, vfs_error))
            return 1;
        size_t count = 0;
        if (!od::port::VFS_Read(vfs, handle, bytes.data(), bytes.size(), count,
                                vfs_error) || count != bytes.size() ||
            bytes != std::array<uint8_t, 8>{0, 0, 0, 0, 0x44, 0x08, 0, 0} ||
            !od::port::VFS_Close(vfs, handle, vfs_error)) return 1;
        std::vector<od::port::BfEntry> members;
        if (!od::port::BF_Mount(vfs, "DATA/ICONE/ICONES.BF", members, vfs_error) ||
            members.size() != (i == 0 ? 5u : 6u)) {
            std::cerr << "Disc " << i + 1 << " BF mount failed: "
                      << vfs_error.message << "\n";
            return 1;
        }
        const std::array<const char*, 5> common_names{
            "MAGIE.ALP", "ANIM.ALP", "PYRAM.ALP", "TOUCHES.SPR", "INTERF.ALP"};
        const std::array<uint64_t, 5> disc1_offsets{
            16, 116505, 156962, 251723, 265748};
        const std::array<uint64_t, 6> disc2_offsets{
            16, 116505, 156962, 251723, 319127, 333152};
        const std::array<uint64_t, 5> disc1_sizes{
            116489, 40457, 94761, 14025, 105275};
        const std::array<uint64_t, 6> disc2_sizes{
            116489, 40457, 94761, 67404, 14025, 105275};
        for (size_t j = 0; j < members.size(); ++j) {
            const char* expected_name = i == 1 && j == 3 ? "TITRES.SPR" :
                common_names[j - (i == 1 && j > 3 ? 1 : 0)];
            const uint64_t expected_offset = i == 0 ? disc1_offsets[j] : disc2_offsets[j];
            const uint64_t expected_size = i == 0 ? disc1_sizes[j] : disc2_sizes[j];
            if (members[j].row != j || members[j].name != expected_name ||
                members[j].offset != expected_offset ||
                members[j].byte_size != expected_size ||
                !od::port::VFS_Open(vfs, members[j].name, 0x200, handle, vfs_error))
                return 1;
            std::array<uint8_t, 4096> actual{}, expected{};
            uint64_t offset = 0;
            while (offset < members[j].byte_size) {
                const size_t needed = static_cast<size_t>(std::min<uint64_t>(
                    actual.size(), members[j].byte_size - offset));
                if (!od::port::VFS_Read(vfs, handle, actual.data(), needed, count,
                                        vfs_error) || count != needed ||
                    !source->read_at(members[j].archive_file,
                                     members[j].offset + offset,
                                     expected.data(), needed, error) ||
                    std::memcmp(actual.data(), expected.data(), needed) != 0)
                    return 1;
                offset += needed;
            }
            if (!od::port::VFS_Close(vfs, handle, vfs_error)) return 1;
        }
        std::vector<const od::disc::Entry*> scenes;
        for (const auto& entry : source->entries()) {
            const std::string path = uppercase_path(entry.path);
            if (entry.kind == EntryKind::file && path.size() >= 4 &&
                path.substr(path.size() - 4) == ".DSN") scenes.push_back(&entry);
        }
        std::sort(scenes.begin(), scenes.end(), [](const auto* a, const auto* b) {
            return uppercase_path(a->path) < uppercase_path(b->path);
        });
        const size_t expected_scene_count = i == 0 ? 52 : 46;
        const size_t expected_object_count = i == 0 ? 1171 : 974;
        const uint32_t expected_scene_crc = i == 0 ? 0x09a8c16du : 0x071183a4u;
        if (scenes.size() != expected_scene_count) return 1;
        od::port::StreamError stream_error;
        auto stream = od::port::STRM_Create(vfs, 0x57800, 0x57800, 0x8000,
                                             stream_error);
        if (!stream) return 1;
        od::port::DsnState dsn;
        od::port::DSN_InitState(dsn, *stream);
        od::port::DsnError dsn_error;
        uint32_t scene_crc = 0xffffffffu;
        size_t object_count = 0;
        for (const auto* entry : scenes) {
            if (!od::port::DSN_LoadHeader(dsn, entry->path, dsn_error) ||
                !dsn.loaded() || dsn.declared_size() != entry->byte_size ||
                dsn.body_offset() != 16 + 31 * static_cast<size_t>(dsn.name_count()) ||
                dsn.objects().size() != dsn.name_count()) {
                std::cerr << "Disc " << i + 1 << " DSN " << entry->path << ": "
                          << dsn_error.message << "\n";
                return 1;
            }
            object_count += dsn.name_count();
            const std::string path = uppercase_path(entry->path);
            scene_crc = crc32_update(scene_crc, path.data(), path.size());
            const uint8_t zero = 0;
            scene_crc = crc32_update(scene_crc, &zero, 1);
            std::array<uint8_t, 10> fields{};
            little32(fields.data(), dsn.declared_size());
            little32(fields.data() + 4, dsn.span());
            little16(fields.data() + 8, dsn.name_count());
            scene_crc = crc32_update(scene_crc, fields.data(), fields.size());
            for (const auto& object : dsn.objects())
                scene_crc = crc32_update(scene_crc, object.raw_name.data(),
                                         object.raw_name.size());
            for (const auto& object : dsn.objects())
                scene_crc = crc32_update(scene_crc, object.raw_record.data(),
                                         object.raw_record.size());
        }
        scene_crc ^= 0xffffffffu;
        if (object_count != expected_object_count || scene_crc != expected_scene_crc) {
            std::cerr << "Disc " << i + 1 << " DSN headers differ from Python oracle\n";
            return 1;
        }
        od::port::STRM_Free(stream);
        std::cout << "Disc " << i + 1 << ": " << source->file_count() << " files, "
                  << source->directory_count() << " directories, "
                  << source->tracks().size() << " tracks, "
                  << scenes.size() << " DSN headers\n";
        images[i] = std::move(source);
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
    const bool vfs_okay = mount_okay && test_vfs_and_bf(base);
    const bool stream_okay = vfs_okay && test_stream_and_dsn(base);
    const bool dan_okay = stream_okay && test_dan_archive(base);
    const bool drd_okay = dan_okay && test_drd_bank(base);
    const bool fsb_okay = drd_okay && test_fsb_bank(base);
    const bool markers_okay = fsb_okay && test_markers_and_offsets(base);
    const bool okay = markers_okay && test_failures(base);
    std::error_code ignored;
    fs::remove_all(base, ignored);
    return okay ? 0 : 1;
}

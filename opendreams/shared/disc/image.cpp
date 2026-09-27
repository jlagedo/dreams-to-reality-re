#include "disc/image.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4200) // Upstream's C flexible directory-name array.
#endif
#include <lib9660.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <new>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace od::disc {
namespace {

constexpr uint64_t raw_sector_size = 2352;
constexpr uint64_t user_sector_size = 2048;
constexpr uint64_t user_data_offset = 16;
constexpr size_t max_cue_line = 4096;
constexpr size_t max_iso_entries = 100000;
std::atomic<uint64_t> next_mount_id{1};

void fail(Error& error, ErrorCode code, std::string message) {
    error.code = code;
    error.message = std::move(message);
}

void clear(Error& error) {
    error.code = ErrorCode::none;
    error.message.clear();
}

bool space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && space(value.front())) value.remove_prefix(1);
    while (!value.empty() && space(value.back())) value.remove_suffix(1);
    return value;
}

std::string upper_ascii(std::string_view value) {
    std::string result(value);
    for (char& c : result) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return result;
}

bool decimal(std::string_view value, uint32_t& result) {
    if (value.empty()) return false;
    uint64_t number = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return false;
        number = number * 10 + static_cast<unsigned>(c - '0');
        if (number > UINT32_MAX) return false;
    }
    result = static_cast<uint32_t>(number);
    return true;
}

bool cue_frames(std::string_view value, uint32_t& result) {
    const size_t first = value.find(':');
    const size_t second = first == std::string_view::npos
        ? first : value.find(':', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos ||
        value.find(':', second + 1) != std::string_view::npos)
        return false;
    uint32_t minutes = 0, seconds = 0, frames = 0;
    if (!decimal(value.substr(0, first), minutes) ||
        !decimal(value.substr(first + 1, second - first - 1), seconds) ||
        !decimal(value.substr(second + 1), frames) || seconds >= 60 || frames >= 75)
        return false;
    const uint64_t total = (static_cast<uint64_t>(minutes) * 60 + seconds) * 75 + frames;
    if (total > UINT32_MAX) return false;
    result = static_cast<uint32_t>(total);
    return true;
}

bool word(std::string_view& rest, std::string_view& token) {
    rest = trim(rest);
    if (rest.empty()) return false;
    size_t end = 0;
    while (end < rest.size() && !space(rest[end])) ++end;
    token = rest.substr(0, end);
    rest.remove_prefix(end);
    rest = trim(rest);
    return true;
}

std::string strip_version(std::string_view name) {
    const size_t semicolon = name.rfind(';');
    if (semicolon != std::string_view::npos && semicolon + 1 < name.size()) {
        bool digits = true;
        for (size_t i = semicolon + 1; i < name.size(); ++i)
            digits = digits && name[i] >= '0' && name[i] <= '9';
        if (digits) name = name.substr(0, semicolon);
    }
    return std::string(name);
}

std::string lookup_key(std::string_view path) {
    std::string key;
    std::string part;
    for (size_t i = 0; i <= path.size(); ++i) {
        const char c = i == path.size() ? '/' : path[i];
        if (c == '/' || c == '\\') {
            if (part.empty()) continue;
            part = strip_version(part);
            if (part.empty() || part == "." || part == "..") return {};
            if (!key.empty()) key += '/';
            key += upper_ascii(part);
            part.clear();
        } else {
            part += c;
        }
    }
    return key;
}

uint32_t little32(const uint8_t value[4]) {
    return static_cast<uint32_t>(value[0]) |
        (static_cast<uint32_t>(value[1]) << 8) |
        (static_cast<uint32_t>(value[2]) << 16) |
        (static_cast<uint32_t>(value[3]) << 24);
}

uint32_t big32(const uint8_t value[4]) {
    return (static_cast<uint32_t>(value[0]) << 24) |
        (static_cast<uint32_t>(value[1]) << 16) |
        (static_cast<uint32_t>(value[2]) << 8) |
        static_cast<uint32_t>(value[3]);
}

bool dual32(const l9660_duint32& value, uint32_t& result) {
    result = little32(value.le);
    return result == big32(value.be);
}

std::string fixed_text(const char* data, size_t size) {
    size_t used = 0;
    while (used < size && data[used] != '\0') ++used;
    std::string value(data, used);
    while (!value.empty() && value.back() == ' ') value.pop_back();
    return value;
}

bool cue_filename_path(std::string_view filename, std::filesystem::path& path) {
#ifdef _WIN32
    auto convert = [&](UINT code_page, DWORD flags) {
        const int count = static_cast<int>(filename.size()); // CUE lines are capped at 4096.
        const int required = MultiByteToWideChar(code_page, flags, filename.data(),
                                                 count, nullptr, 0);
        if (required <= 0) return false;
        std::wstring wide(static_cast<size_t>(required), L'\0');
        if (MultiByteToWideChar(code_page, flags, filename.data(), count,
                                wide.data(), required) != required)
            return false;
        path = std::filesystem::path(wide);
        return true;
    };
    return convert(CP_UTF8, MB_ERR_INVALID_CHARS) || convert(CP_ACP, 0);
#else
    path = std::filesystem::path(std::string(filename));
    return true;
#endif
}

struct PendingTrack {
    Track track;
    bool has_index00 = false;
    bool has_index01 = false;
};

bool parse_cue(const std::filesystem::path& cue, std::vector<Track>& tracks, Error& error) {
    std::ifstream input(cue, std::ios::binary);
    if (!input) {
        fail(error, ErrorCode::cue_io, "cannot open CUE: " + cue.u8string());
        return false;
    }

    std::vector<PendingTrack> parsed;
    std::string current_filename;
    bool file_waits_for_track = false;
    std::string line;
    size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line_number > 10000 || line.size() > max_cue_line) {
            fail(error, ErrorCode::cue_syntax, "CUE exceeds supported line limit");
            return false;
        }
        if (line_number == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0)
            line.erase(0, 3);
        std::string_view rest = trim(line);
        if (rest.empty()) continue;
        std::string_view directive;
        if (!word(rest, directive)) continue;
        const std::string command = upper_ascii(directive);
        auto syntax = [&](std::string message) {
            fail(error, ErrorCode::cue_syntax,
                 "CUE line " + std::to_string(line_number) + ": " + message);
            return false;
        };
        auto unsupported = [&](std::string message) {
            fail(error, ErrorCode::unsupported_layout,
                 "CUE line " + std::to_string(line_number) + ": " + message);
            return false;
        };

        if (command == "REM" || command == "TITLE" || command == "PERFORMER" ||
            command == "SONGWRITER" || command == "ISRC" || command == "CATALOG")
            continue;
        if (command == "FILE") {
            if (file_waits_for_track) return syntax("FILE has no TRACK");
            if (rest.empty()) return syntax("FILE needs a name and type");
            std::string_view filename;
            if (rest.front() == '"') {
                const size_t end = rest.find('"', 1);
                if (end == std::string_view::npos) return syntax("unclosed FILE name");
                filename = rest.substr(1, end - 1);
                rest.remove_prefix(end + 1);
                rest = trim(rest);
            } else if (!word(rest, filename)) {
                return syntax("FILE needs a name");
            }
            if (filename.empty() || upper_ascii(rest) != "BINARY")
                return unsupported("only FILE ... BINARY is supported");
            current_filename.assign(filename);
            file_waits_for_track = true;
            continue;
        }
        if (command == "TRACK") {
            if (!file_waits_for_track) return unsupported("one TRACK per FILE is required");
            std::string_view number_text, mode_text;
            uint32_t number = 0;
            if (!word(rest, number_text) || !word(rest, mode_text) || !rest.empty() ||
                !decimal(number_text, number) || number != parsed.size() + 1 || number > 99)
                return syntax("TRACK numbers must be consecutive from 01");
            const std::string mode = upper_ascii(mode_text);
            if ((number == 1 && mode != "MODE1/2352") ||
                (number > 1 && mode != "AUDIO"))
                return unsupported("expected MODE1/2352 first, then AUDIO tracks");
            PendingTrack track;
            track.track.number = number;
            track.track.mode = number == 1 ? TrackMode::mode1_2352 : TrackMode::audio;
            track.track.cue_filename = current_filename;
            parsed.push_back(std::move(track));
            file_waits_for_track = false;
            continue;
        }
        if (command == "INDEX") {
            if (parsed.empty() || file_waits_for_track)
                return syntax("INDEX without TRACK");
            std::string_view index_text, time_text;
            uint32_t index = 0, frames = 0;
            if (!word(rest, index_text) || !word(rest, time_text) || !rest.empty() ||
                !decimal(index_text, index) || index > 1 || !cue_frames(time_text, frames))
                return syntax("invalid INDEX number or MM:SS:FF");
            auto& track = parsed.back();
            if (index == 0) {
                if (track.has_index00 || track.has_index01)
                    return syntax("INDEX 00 must occur once before INDEX 01");
                track.has_index00 = true;
                track.track.index00_frames = frames;
            } else {
                if (track.has_index01) return syntax("duplicate INDEX 01");
                track.has_index01 = true;
                track.track.index01_frames = frames;
            }
            continue;
        }
        return unsupported("unsupported directive " + command);
    }
    if (!input.eof()) {
        fail(error, ErrorCode::cue_io, "error reading CUE: " + cue.u8string());
        return false;
    }
    if (file_waits_for_track || parsed.empty()) {
        fail(error, ErrorCode::cue_syntax, "CUE has no complete data track");
        return false;
    }
    for (auto& pending : parsed) {
        if (!pending.has_index01 ||
            (pending.has_index00 && *pending.track.index00_frames > pending.track.index01_frames)) {
            fail(error, ErrorCode::cue_syntax,
                 "TRACK " + std::to_string(pending.track.number) + " has invalid indexes");
            return false;
        }
        std::string host_name = pending.track.cue_filename;
        std::replace(host_name.begin(), host_name.end(), '\\', '/');
        std::filesystem::path file;
        if (!cue_filename_path(host_name, file)) {
            fail(error, ErrorCode::cue_syntax, "CUE FILE name cannot be decoded");
            return false;
        }
        pending.track.backing_path = (file.is_absolute() ? file : cue.parent_path() / file)
            .lexically_normal();
        std::error_code ec;
        const uint64_t size = std::filesystem::file_size(pending.track.backing_path, ec);
        if (ec) {
            pending.track.status = TrackStatus::missing;
            pending.track.note = "backing file is missing or unreadable";
        } else if (size % raw_sector_size != 0 ||
                   pending.track.index01_frames >= size / raw_sector_size) {
            pending.track.status = TrackStatus::invalid;
            pending.track.byte_size = size;
            pending.track.note = "backing file is truncated or INDEX 01 is out of range";
        } else {
            std::ifstream check_read(pending.track.backing_path, std::ios::binary);
            if (!check_read) {
                pending.track.status = TrackStatus::missing;
                pending.track.note = "backing file is unreadable";
            } else {
                pending.track.status = TrackStatus::available;
                pending.track.byte_size = size;
                pending.track.program_sectors = size / raw_sector_size -
                    pending.track.index01_frames;
            }
        }
        if (pending.track.number == 1 && pending.track.status != TrackStatus::available) {
            fail(error, ErrorCode::data_io,
                 "data track " + pending.track.note + ": " +
                 pending.track.backing_path.u8string());
            return false;
        }
        tracks.push_back(std::move(pending.track));
    }
    return true;
}

} // namespace

struct Image::Impl {
    struct Bridge {
        l9660_fs fs{};
        Impl* owner = nullptr;
    } bridge;
    static_assert(offsetof(Bridge, fs) == 0, "lib9660 callback bridge layout");
    std::filesystem::path cue;
    std::vector<Track> tracks;
    std::ifstream data;
    uint64_t raw_sectors = 0;
    uint32_t data_start = 0;
    uint32_t volume_sectors = 0;
    uint64_t mount = 0;
    Identity disc_identity = Identity::unknown;
    std::string volume;
    std::vector<Entry> items;
    std::unordered_map<std::string, std::vector<uint32_t>> by_path;
    size_t files = 0;
    size_t directories = 0;
    mutable std::mutex io_mutex;

    static bool sector_callback(l9660_fs* fs, void* buffer, uint32_t sector) {
        auto* bridge = reinterpret_cast<Bridge*>(fs);
        return bridge->owner->sector_read(sector, buffer);
    }

    bool sector_read(uint32_t sector, void* buffer) {
        const uint64_t physical = static_cast<uint64_t>(data_start) + sector;
        if (physical >= raw_sectors) return false;
        const uint64_t offset = physical * raw_sector_size + user_data_offset;
        if (offset + user_sector_size > tracks.front().byte_size ||
            offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
            return false;
        data.clear();
        data.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!data) return false;
        data.read(static_cast<char*>(buffer), static_cast<std::streamsize>(user_sector_size));
        return data.gcount() == static_cast<std::streamsize>(user_sector_size);
    }

    bool valid_extent(uint32_t sector, uint32_t length) const {
        const uint64_t count = (static_cast<uint64_t>(length) + user_sector_size - 1) /
            user_sector_size;
        return sector <= volume_sectors &&
            static_cast<uint64_t>(sector) + count <= volume_sectors;
    }

    bool walk(l9660_dir& dir, std::string parent_path, std::string parent_iso,
              uint32_t parent_index, std::vector<std::pair<uint32_t, uint32_t>>& ancestors,
              Error& error) {
        if (ancestors.size() > 64) {
            fail(error, ErrorCode::invalid_iso, "ISO directory nesting is too deep");
            return false;
        }
        for (;;) {
            l9660_dirent* record = nullptr;
            if (l9660_readdir(&dir, &record) != L9660_OK) {
                fail(error, ErrorCode::invalid_iso, "invalid ISO directory record");
                return false;
            }
            if (!record) return true;
            if (record->name_len == 1 && (record->name[0] == 0 || record->name[0] == 1))
                continue;
            if (record->flags & 0x80 || record->unit_size || record->gap_size) {
                fail(error, ErrorCode::unsupported_layout,
                     "interleaved or multi-extent ISO entries are unsupported");
                return false;
            }
            uint32_t extent = 0, size = 0;
            if (!dual32(record->sector, extent) || !dual32(record->size, size) ||
                static_cast<uint64_t>(extent) + record->xattr_length > UINT32_MAX) {
                fail(error, ErrorCode::invalid_iso, "ISO extent fields disagree or overflow");
                return false;
            }
            extent += record->xattr_length;
            if (!valid_extent(extent, size)) {
                fail(error, ErrorCode::invalid_iso, "ISO entry extent exceeds data track");
                return false;
            }
            const std::string raw_name(record->name, record->name_len);
            const std::string name = strip_version(raw_name);
            if (name.empty() || name.find_first_of("/\\") != std::string::npos ||
                name == "." || name == "..") {
                fail(error, ErrorCode::invalid_iso, "invalid ISO identifier");
                return false;
            }
            for (unsigned char c : raw_name) {
                if (c < 32 || c == 127) {
                    fail(error, ErrorCode::invalid_iso, "invalid ISO identifier byte");
                    return false;
                }
            }
            if (items.size() >= max_iso_entries) {
                fail(error, ErrorCode::invalid_iso, "ISO entry count exceeds limit");
                return false;
            }
            Entry item;
            item.id = {mount, static_cast<uint32_t>(items.size())};
            item.parent_index = parent_index;
            item.kind = (record->flags & 2) ? EntryKind::directory : EntryKind::file;
            item.path = parent_path.empty() ? name : std::string(parent_path) + "/" + name;
            item.iso_path = parent_iso.empty() ? raw_name :
                std::string(parent_iso) + "/" + raw_name;
            item.byte_size = size;
            item.extent_sector = extent;
            const uint32_t index = item.id.index;
            items.push_back(std::move(item));
            by_path[lookup_key(items.back().path)].push_back(index);
            if (items.back().kind == EntryKind::file) {
                ++files;
                continue;
            }
            ++directories;
            const std::pair<uint32_t, uint32_t> location{extent, size};
            if (std::find(ancestors.begin(), ancestors.end(), location) != ancestors.end()) {
                fail(error, ErrorCode::invalid_iso, "ISO directory cycle");
                return false;
            }
            l9660_dir child{};
            child.file.fs = &bridge.fs;
            child.file.first_sector = extent;
            child.file.length = size;
            ancestors.push_back(location);
            const bool okay = walk(child, items[index].path, items[index].iso_path,
                                   index, ancestors, error);
            ancestors.pop_back();
            if (!okay) return false;
        }
    }
};

Image::Image(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Image::~Image() = default;

std::unique_ptr<Image> Image::open(const std::filesystem::path& cue, Error& error) {
    clear(error);
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(cue, ec);
    if (ec) {
        fail(error, ErrorCode::cue_io, "cannot resolve CUE path");
        return nullptr;
    }
    std::unique_ptr<Impl> impl(new (std::nothrow) Impl());
    if (!impl) {
        fail(error, ErrorCode::data_io, "out of memory creating disc mount");
        return nullptr;
    }
    impl->cue = absolute.lexically_normal();
    if (!parse_cue(impl->cue, impl->tracks, error)) return nullptr;
    impl->data.open(impl->tracks.front().backing_path, std::ios::binary);
    if (!impl->data) {
        fail(error, ErrorCode::data_io, "cannot open data track");
        return nullptr;
    }
    impl->raw_sectors = impl->tracks.front().byte_size / raw_sector_size;
    impl->data_start = impl->tracks.front().index01_frames;
    impl->mount = next_mount_id.fetch_add(1, std::memory_order_relaxed);
    impl->bridge.owner = impl.get();
    if (l9660_openfs(&impl->bridge.fs, Impl::sector_callback) != L9660_OK) {
        fail(error, ErrorCode::invalid_iso, "data track has no valid ISO 9660 primary volume");
        return nullptr;
    }
    const auto* pvd = reinterpret_cast<const l9660_vdesc_primary*>(&impl->bridge.fs.pvd);
    if (!dual32(pvd->volume_space_size, impl->volume_sectors) ||
        impl->volume_sectors == 0 ||
        impl->volume_sectors > impl->raw_sectors - impl->data_start) {
        fail(error, ErrorCode::invalid_iso, "ISO volume size exceeds data track");
        return nullptr;
    }
    impl->volume = fixed_text(pvd->volume_id, sizeof pvd->volume_id);
    l9660_dir root{};
    uint32_t root_extent = 0, root_size = 0;
    if (l9660_fs_open_root(&root, &impl->bridge.fs) != L9660_OK ||
        !dual32(pvd->root_dir_ent.sector, root_extent) ||
        !dual32(pvd->root_dir_ent.size, root_size) ||
        pvd->root_dir_ent.xattr_length != 0 ||
        root.file.first_sector != root_extent || root.file.length != root_size ||
        !impl->valid_extent(root.file.first_sector, root.file.length)) {
        fail(error, ErrorCode::invalid_iso, "invalid ISO root directory extent");
        return nullptr;
    }
    std::vector<std::pair<uint32_t, uint32_t>> ancestors{
        {root.file.first_sector, root.file.length}};
    if (!impl->walk(root, {}, {}, UINT32_MAX, ancestors, error)) return nullptr;
    const auto marker1 = impl->by_path.find("DATA/1CD.ID");
    const auto marker2 = impl->by_path.find("DATA/2CD.ID");
    const bool duplicate_marker =
        (marker1 != impl->by_path.end() && marker1->second.size() != 1) ||
        (marker2 != impl->by_path.end() && marker2->second.size() != 1);
    const bool has1 = marker1 != impl->by_path.end() && marker1->second.size() == 1 &&
        impl->items[marker1->second.front()].kind == EntryKind::file;
    const bool has2 = marker2 != impl->by_path.end() && marker2->second.size() == 1 &&
        impl->items[marker2->second.front()].kind == EntryKind::file;
    impl->disc_identity = duplicate_marker || (has1 && has2) ? Identity::ambiguous :
        has1 ? Identity::disc1 : has2 ? Identity::disc2 : Identity::unknown;
    std::unique_ptr<Image> image(new (std::nothrow) Image(std::move(impl)));
    if (!image) fail(error, ErrorCode::data_io, "out of memory creating disc image");
    return image;
}

uint64_t Image::mount_id() const { return impl_->mount; }
Identity Image::identity() const { return impl_->disc_identity; }
const std::filesystem::path& Image::cue_path() const { return impl_->cue; }
const std::string& Image::volume_id() const { return impl_->volume; }
const std::vector<Track>& Image::tracks() const { return impl_->tracks; }
const std::vector<Entry>& Image::entries() const { return impl_->items; }
size_t Image::file_count() const { return impl_->files; }
size_t Image::directory_count() const { return impl_->directories; }

bool Image::find(std::string_view path, FileId& file, Error& error) const {
    clear(error);
    const auto key = lookup_key(path);
    const auto found = impl_->by_path.find(key);
    if (key.empty() || found == impl_->by_path.end()) {
        fail(error, ErrorCode::not_found, "disc path not found: " + std::string(path));
        return false;
    }
    if (found->second.size() != 1) {
        fail(error, ErrorCode::ambiguous, "disc path has multiple ISO entries: " +
             std::string(path));
        return false;
    }
    file = {impl_->mount, found->second.front()};
    return true;
}

bool Image::children(std::string_view directory, std::vector<FileId>& result,
                     Error& error) const {
    clear(error);
    uint32_t parent = UINT32_MAX;
    if (!directory.empty() && directory != "/") {
        FileId id;
        if (!find(directory, id, error)) return false;
        const Entry* item = entry(id);
        if (item->kind != EntryKind::directory) {
            fail(error, ErrorCode::not_directory, "disc entry is a file");
            return false;
        }
        parent = id.index;
    }
    result.clear();
    for (const auto& item : impl_->items) {
        if (item.parent_index == parent) result.push_back(item.id);
    }
    return true;
}

const Entry* Image::entry(FileId file) const {
    if (file.mount != impl_->mount || file.index >= impl_->items.size()) return nullptr;
    return &impl_->items[file.index];
}

bool Image::read_at(FileId file, uint64_t offset, void* buffer, size_t length,
                    Error& error) const {
    clear(error);
    const Entry* item = entry(file);
    if (!item) {
        fail(error, ErrorCode::stale_file, "file belongs to another disc mount");
        return false;
    }
    if (item->kind != EntryKind::file) {
        fail(error, ErrorCode::not_file, "disc entry is a directory");
        return false;
    }
    if (offset > item->byte_size || length > item->byte_size - offset ||
        (length && !buffer)) {
        fail(error, ErrorCode::out_of_range, "read exceeds disc file bounds");
        return false;
    }
    if (length == 0) return true;
    std::lock_guard<std::mutex> lock(impl_->io_mutex);
    std::array<uint8_t, 2048> sector{};
    auto* target = static_cast<uint8_t*>(buffer);
    size_t done = 0;
    while (done < length) {
        const uint64_t position = offset + done;
        const uint64_t logical = static_cast<uint64_t>(item->extent_sector) +
            position / user_sector_size;
        if (logical > UINT32_MAX ||
            !impl_->sector_read(static_cast<uint32_t>(logical), sector.data())) {
            fail(error, ErrorCode::data_io, "cannot read data-track sector");
            return false;
        }
        const size_t within = static_cast<size_t>(position % user_sector_size);
        const size_t count = std::min(length - done, sector.size() - within);
        std::memcpy(target + done, sector.data() + within, count);
        done += count;
    }
    return true;
}

bool Image::audio_track_size(unsigned number, uint64_t& size, Error& error) const {
    clear(error);
    size = 0;
    const Track* selected = nullptr;
    for (const auto& track : impl_->tracks)
        if (track.number == number) { selected = &track; break; }
    if (!selected || selected->mode != TrackMode::audio ||
        selected->status != TrackStatus::available) {
        fail(error, ErrorCode::not_found, "selected CUE audio track is unavailable");
        return false;
    }
    uint64_t sectors = selected->program_sectors;
    for (const auto& other : impl_->tracks) {
        if (other.number <= number || other.backing_path != selected->backing_path ||
            other.index01_frames <= selected->index01_frames) continue;
        sectors = std::min<uint64_t>(sectors,
            other.index01_frames - selected->index01_frames);
    }
    size = sectors * raw_sector_size;
    return true;
}

bool Image::read_audio_track_at(unsigned number, uint64_t offset, void* buffer,
                                size_t length, Error& error) const {
    uint64_t size = 0;
    if (!audio_track_size(number,size,error)) return false;
    if (offset > size || length > size-offset || (length && !buffer) ||
        length > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        fail(error, ErrorCode::out_of_range, "read exceeds selected audio track bounds");
        return false;
    }
    if (!length) return true;
    const Track* selected = nullptr;
    for (const auto& track : impl_->tracks)
        if (track.number == number) { selected = &track; break; }
    const uint64_t absolute = static_cast<uint64_t>(selected->index01_frames) *
        raw_sector_size + offset;
    if (absolute > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        fail(error, ErrorCode::out_of_range, "audio track offset exceeds host file limit");
        return false;
    }
    std::ifstream input(selected->backing_path,std::ios::binary);
    if (!input) {
        fail(error, ErrorCode::data_io, "cannot open selected CUE audio backing file");
        return false;
    }
    input.seekg(static_cast<std::streamoff>(absolute),std::ios::beg);
    input.read(static_cast<char*>(buffer),static_cast<std::streamsize>(length));
    if (input.gcount()!=static_cast<std::streamsize>(length)) {
        fail(error, ErrorCode::data_io, "selected CUE audio track has a short read");
        return false;
    }
    return true;
}

} // namespace od::disc

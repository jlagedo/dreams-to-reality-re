#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace od::disc {

enum class ErrorCode {
    none,
    cue_io,
    cue_syntax,
    unsupported_layout,
    data_io,
    invalid_iso,
    not_found,
    ambiguous,
    not_file,
    not_directory,
    out_of_range,
    stale_file,
};

struct Error {
    ErrorCode code = ErrorCode::none;
    std::string message;
    explicit operator bool() const { return code != ErrorCode::none; }
};

enum class Identity { unknown, disc1, disc2, ambiguous };
enum class TrackMode { mode1_2352, audio };
enum class TrackStatus { available, missing, invalid };
enum class EntryKind { directory, file };

struct Track {
    unsigned number = 0;
    TrackMode mode = TrackMode::audio;
    std::string cue_filename;
    std::filesystem::path backing_path;
    std::optional<uint32_t> index00_frames;
    uint32_t index01_frames = 0;
    uint64_t byte_size = 0;
    uint64_t program_sectors = 0;
    TrackStatus status = TrackStatus::missing;
    std::string note;
};

struct FileId {
    uint64_t mount = 0;
    uint32_t index = 0;
};

struct Entry {
    FileId id;
    uint32_t parent_index = UINT32_MAX; // UINT32_MAX means the ISO root.
    EntryKind kind = EntryKind::file;
    std::string path;     // ISO spelling, with the file-version suffix removed.
    std::string iso_path; // Original ISO identifier, including any ;1 suffix.
    uint64_t byte_size = 0;
    uint32_t extent_sector = 0;
};

// Owns one CUE/BIN source. Entries and tracks remain valid for this mount's
// lifetime; FileId also guards reads after a source is replaced by another.
class Image {
public:
    static std::unique_ptr<Image> open(const std::filesystem::path& cue, Error& error);
    ~Image();
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    uint64_t mount_id() const;
    Identity identity() const;
    const std::filesystem::path& cue_path() const;
    const std::string& volume_id() const;
    const std::vector<Track>& tracks() const;
    const std::vector<Entry>& entries() const;
    size_t file_count() const;
    size_t directory_count() const;

    bool find(std::string_view path, FileId& file, Error& error) const;
    // Empty path names the ISO root. A directory result preserves source IDs.
    bool children(std::string_view directory, std::vector<FileId>& result,
                  Error& error) const;
    const Entry* entry(FileId file) const;
    // An exact bounded read. A zero-byte read at EOF succeeds. On an I/O error,
    // bytes copied before the failed sector may already be in the buffer.
    bool read_at(FileId file, uint64_t offset, void* buffer, size_t length, Error& error) const;

private:
    struct Impl;
    explicit Image(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace od::disc

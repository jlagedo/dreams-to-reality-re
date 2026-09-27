#pragma once

#include "port/vfs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

enum class DanErrorCode {
    none,
    missing_file,
    invalid_state,
    invalid_header,
    invalid_chunk,
    source_error,
};

struct DanError {
    DanErrorCode code = DanErrorCode::none;
    std::string message;
    VfsError source;
    explicit operator bool() const { return code != DanErrorCode::none; }
};

struct DanName {
    std::array<uint8_t, 11> raw{};
    std::string text;
};

struct DanClip {
    std::array<uint8_t, 13> raw{};
    std::string text;
};

struct DanChunk {
    size_t index = 0;
    uint64_t file_offset = 0; // Start of the five-byte tag/size header.
    uint32_t payload_size = 0;
    size_t work_offset = 0;
};

// Retail's mutable DAN archive state, scoped to one selected VFS source.
// The VfsContext must outlive this archive.
// Getter views and work-buffer pointers are invalidated by the next open/close.
class DanArchive {
public:
    explicit DanArchive(VfsContext& vfs) : vfs_(&vfs) {}
    ~DanArchive();
    DanArchive(const DanArchive&) = delete;
    DanArchive& operator=(const DanArchive&) = delete;
    bool is_open() const { return open_; }
    bool animations_loaded() const { return animations_loaded_; }
    uint32_t declared_size() const { return declared_size_; }
    uint32_t span() const { return span_; }
    uint64_t body_offset() const { return body_offset_; }
    const std::vector<DanName>& names() const { return names_; }
    const std::vector<DanClip>& clips() const { return clips_; }
    const std::vector<DanChunk>& animation_chunks() const { return chunks_; }
    const std::vector<uint8_t>& animation_work() const { return work_; }
    std::string_view path() const { return path_; }

private:
    VfsContext* vfs_ = nullptr;
    int32_t handle_ = 0;
    uint64_t source_size_ = 0;
    uint64_t body_offset_ = 0;
    uint32_t declared_size_ = 0;
    uint32_t span_ = 0;
    std::string path_;
    std::vector<DanName> names_;
    std::vector<DanClip> clips_;
    std::vector<DanChunk> chunks_;
    std::vector<uint8_t> work_;
    bool open_ = false;
    bool animations_loaded_ = false;

    friend bool DAN_OpenArchive(DanArchive&, std::string_view, DanError&);
    friend bool DAN_ReadAnimChunks(DanArchive&, DanError&);
    friend size_t DAN_GetAnimCount(const DanArchive&);
    friend std::string_view DAN_GetAnimName(const DanArchive&, size_t);
    friend void DAN_CloseArchive(DanArchive&);
};

bool DAN_OpenArchive(DanArchive& archive, std::string_view path, DanError& error);
bool DAN_ReadAnimChunks(DanArchive& archive, DanError& error);
size_t DAN_GetAnimCount(const DanArchive& archive);
std::string_view DAN_GetAnimName(const DanArchive& archive, size_t index);
void DAN_CloseArchive(DanArchive& archive);

} // namespace od::port

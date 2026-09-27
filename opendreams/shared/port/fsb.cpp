#include "port/fsb.h"

#include <array>
#include <cstring>
#include <utility>

namespace od::port {
namespace {

bool fail(FsbError& error, FsbErrorCode code, std::string message) {
    error = {code, std::move(message), {}};
    return false;
}

bool source_fail(FsbError& error, const VfsError& source) {
    error = {FsbErrorCode::source_error, source.message, source};
    return false;
}

uint32_t little32(const uint8_t* value) {
    return static_cast<uint32_t>(value[0]) |
        (static_cast<uint32_t>(value[1]) << 8) |
        (static_cast<uint32_t>(value[2]) << 16) |
        (static_cast<uint32_t>(value[3]) << 24);
}

bool read_exact(VfsContext& vfs, int32_t handle, void* destination,
                size_t size, FsbError& error) {
    VfsError source;
    size_t count = 0;
    if (!VFS_Read(vfs, handle, destination, size, count, source))
        return source_fail(error, source);
    return count == size || fail(error, FsbErrorCode::invalid_table,
                                 "FSB bank has a truncated read");
}

} // namespace

void FSB_Free(FsbBank& bank) {
    std::vector<uint8_t>().swap(bank.blob_);
    std::vector<FsbClip>().swap(bank.clips_);
    bank.path_.clear();
    bank.loaded_ = false;
}

bool FSB_Load(FsbBank& bank, std::string_view path, FsbError& error) {
    error = {};
    FSB_Free(bank);
    VfsError source;
    int32_t handle = 0;
    if (!VFS_Open(*bank.vfs_, path, 0x200, handle, source)) {
        if (source.code == VfsErrorCode::missing_file)
            return fail(error, FsbErrorCode::missing_file, source.message);
        return source_fail(error, source);
    }
    if (handle < 0) {
        VFS_Close(*bank.vfs_, handle, source);
        return fail(error, FsbErrorCode::missing_file,
                    "retail FSB opener requires a loose disc file");
    }
    const auto close = [&]() {
        VfsError ignored;
        VFS_Close(*bank.vfs_, handle, ignored);
    };
    uint64_t file_size = 0, position = 0;
    if (!VFS_Seek(*bank.vfs_, handle, 0, 2, file_size, source) ||
        !VFS_Seek(*bank.vfs_, handle, 0, 0, position, source)) {
        close();
        return source_fail(error, source);
    }
    if (file_size < 16 || file_size > 256 * 1024 * 1024) {
        close();
        return fail(error, FsbErrorCode::invalid_header,
                    "FSB file size is outside supported bounds");
    }
    std::array<uint8_t, 16> header{};
    if (!read_exact(*bank.vfs_, handle, header.data(), header.size(), error)) {
        close();
        return false;
    }
    if (std::memcmp(header.data(), "DREAMS FSB  ", 12) != 0) {
        close();
        return fail(error, FsbErrorCode::invalid_header,
                    "FSB file has no DREAMS FSB marker");
    }
    const uint32_t count = little32(header.data() + 12);
    const uint64_t data_offset = 16 + static_cast<uint64_t>(count) * 4;
    if (count > 4096 || data_offset > file_size) {
        close();
        return fail(error, FsbErrorCode::invalid_table,
                    "FSB size table exceeds the file");
    }
    std::vector<uint8_t> raw_sizes(static_cast<size_t>(count) * 4);
    if (!read_exact(*bank.vfs_, handle, raw_sizes.data(), raw_sizes.size(), error)) {
        close();
        return false;
    }
    std::vector<FsbClip> clips;
    clips.reserve(count);
    uint64_t used = 0;
    const uint64_t blob_size = file_size - data_offset;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t length = little32(raw_sizes.data() + i * 4);
        if (length > blob_size - used) {
            close();
            return fail(error, FsbErrorCode::invalid_table,
                        "FSB clip sizes exceed the contiguous sample blob");
        }
        clips.push_back({i, data_offset + used, length, static_cast<size_t>(used)});
        used += length;
    }
    if (used != blob_size) {
        close();
        return fail(error, FsbErrorCode::invalid_table,
                    "FSB size table does not end at EOF");
    }
    std::vector<uint8_t> blob(static_cast<size_t>(blob_size));
    if (!read_exact(*bank.vfs_, handle, blob.data(), blob.size(), error)) {
        close();
        return false;
    }
    close();
    bank.blob_ = std::move(blob);
    bank.clips_ = std::move(clips);
    bank.path_ = std::string(path);
    bank.loaded_ = true;
    return true;
}

FsbSample FSB_GetSample(const FsbBank& bank, size_t index) {
    if (!bank.loaded_ || index >= bank.clips_.size()) return {};
    const auto& clip = bank.clips_[index];
    if (clip.byte_size == 0) return {};
    return {bank.blob_.data() + clip.blob_offset, clip.byte_size};
}

} // namespace od::port

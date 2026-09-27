#pragma once

#include "disc/image.h"
#include "port/file_roots.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

enum class VfsErrorCode {
    none,
    missing_file,
    invalid_mode,
    invalid_buffer,
    invalid_handle,
    invalid_seek,
    malformed_archive,
    source_error,
    too_many_handles,
};

struct VfsError {
    VfsErrorCode code = VfsErrorCode::none;
    std::string message;
    disc::Error source;
    explicit operator bool() const { return code != VfsErrorCode::none; }
};

// The physical row identity is kept even when retail registration replaces an
// earlier member with the same name. Offsets are relative to the owning BF file.
struct BfEntry {
    size_t row = 0;
    std::string name;
    uint64_t offset = 0;
    uint64_t byte_size = 0;
    disc::FileId archive_file;
};

// Retail used process-global descriptors and one process-global BF table. A
// context gives each selected disc the same state transitions independently.
// The shared Image keeps the backing CUE/BIN alive for every VFS handle.
class VfsContext {
public:
    explicit VfsContext(std::shared_ptr<const disc::Image> image,
                        FileRootState roots = {});
    const disc::Image* image() const { return image_.get(); }
    FileRootState& roots() { return roots_; }
    const FileRootState& roots() const { return roots_; }
    size_t registered_member_count() const { return members_.size(); }

private:
    struct Physical {
        disc::FileId file;
        uint64_t position = 0;
        bool open = false;
    };
    struct Member {
        BfEntry entry;
        uint64_t position = 0;
        bool opened = false;
    };
    std::shared_ptr<const disc::Image> image_;
    FileRootState roots_;
    std::vector<Physical> files_;
    std::vector<Member> members_;

    Physical* physical(int32_t handle);
    Member* member(int32_t handle);

    friend bool VFS_Open(VfsContext&, std::string_view, uint32_t, int32_t&, VfsError&);
    friend bool VFS_Read(VfsContext&, int32_t, void*, size_t, size_t&, VfsError&);
    friend bool VFS_Seek(VfsContext&, int32_t, int64_t, unsigned, uint64_t&, VfsError&);
    friend bool VFS_Close(VfsContext&, int32_t, VfsError&);
    friend int32_t VFS_FindMember(const VfsContext&, std::string_view);
    friend int32_t VFS_OpenMember(VfsContext&, std::string_view);
    friend void VFS_AddArchiveEntries(VfsContext&, const std::vector<BfEntry>&);
    friend void VFS_FreeArchives(VfsContext&);
    friend bool BF_Mount(VfsContext&, std::string_view, std::vector<BfEntry>&, VfsError&);
};

// Adapted WINDREAM/GDIDREAM retail entry points. A failed operation returns a
// recoverable error rather than taking the original fatal-error path. Physical
// handles are positive, BF member handles are -(table index + 1), as in retail.
bool VFS_Open(VfsContext& context, std::string_view path, uint32_t mode,
              int32_t& handle, VfsError& error);
bool VFS_Read(VfsContext& context, int32_t handle, void* buffer, size_t requested,
              size_t& bytes_read, VfsError& error);
bool VFS_Seek(VfsContext& context, int32_t handle, int64_t offset, unsigned whence,
              uint64_t& position, VfsError& error);
bool VFS_Close(VfsContext& context, int32_t handle, VfsError& error);

int32_t VFS_FindMember(const VfsContext& context, std::string_view name);
int32_t VFS_OpenMember(VfsContext& context, std::string_view name);
void VFS_AddArchiveEntries(VfsContext& context, const std::vector<BfEntry>& entries);
void VFS_FreeArchives(VfsContext& context);
bool BF_Mount(VfsContext& context, std::string_view path,
              std::vector<BfEntry>& rows, VfsError& error);

} // namespace od::port

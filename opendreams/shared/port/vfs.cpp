#include "port/vfs.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <limits>
#include <utility>

namespace od::port {
namespace {

bool fail(VfsError& error, VfsErrorCode code, std::string message) {
    error = {code, std::move(message), {}};
    return false;
}

bool source_fail(VfsError& error, const disc::Error& source) {
    error = {VfsErrorCode::source_error, source.message, source};
    return false;
}

char upper_ascii(char value) {
    return value >= 'a' && value <= 'z' ? static_cast<char>(value - 'a' + 'A') : value;
}

std::string upper_name(std::string_view name) {
    std::string result(name);
    for (char& value : result) value = upper_ascii(value);
    return result;
}

std::string slashes(std::string_view path) {
    std::string result(path);
    for (char& value : result) if (value == '\\') value = '/';
    return result;
}

bool starts_with_root(std::string_view path, std::string_view root) {
    if (root.empty() || path.size() < root.size()) return false;
    for (size_t i = 0; i < root.size(); ++i)
        if (upper_ascii(path[i]) != upper_ascii(root[i])) return false;
    return true;
}

std::string source_path(std::string_view path, const FileRootState& roots) {
    std::string result = slashes(path);
    const std::array<std::string, 3> prefixes = {
        slashes(FILE_GetInstallRoot(roots)), slashes(FILE_GetDataRoot(roots)),
        slashes(roots.disc_root)};
    size_t longest = 0;
    for (const auto& root : prefixes) {
        if (root.size() > longest && starts_with_root(result, root))
            longest = root.size();
    }
    if (longest != 0) return result.substr(longest);
    // A drive-qualified path for an unrelated source must not resolve through
    // the selected disc by accidentally treating its drive as an ISO folder.
    if (result.size() >= 2 && result[1] == ':') return {};
    return result;
}

uint32_t little32(const uint8_t* value) {
    return static_cast<uint32_t>(value[0]) |
        (static_cast<uint32_t>(value[1]) << 8) |
        (static_cast<uint32_t>(value[2]) << 16) |
        (static_cast<uint32_t>(value[3]) << 24);
}

bool seek_from(uint64_t base, int64_t offset, uint64_t& result) {
    if (offset >= 0) {
        const auto step = static_cast<uint64_t>(offset);
        if (base > UINT64_MAX - step) return false;
        result = base + step;
        return true;
    }
    const auto step = static_cast<uint64_t>(-(offset + 1)) + 1;
    if (base < step) return false;
    result = base - step;
    return true;
}

uint64_t clamp_member_seek(uint64_t base, int64_t offset, uint64_t size) {
    uint64_t result = 0;
    if (offset >= 0) {
        const auto step = static_cast<uint64_t>(offset);
        result = base > UINT64_MAX - step ? UINT64_MAX : base + step;
    } else {
        const auto step = static_cast<uint64_t>(-(offset + 1)) + 1;
        result = base < step ? 0 : base - step;
    }
    // Retail clamps a member seek to [0, length - 1]. For an empty member,
    // length - 1 underflows in retail; its safe adapted position is zero.
    return size == 0 ? 0 : std::min(result, size - 1);
}

bool exact_read(VfsContext& context, int32_t handle, void* buffer, size_t size,
                VfsError& error) {
    size_t count = 0;
    if (!VFS_Read(context, handle, buffer, size, count, error)) return false;
    return count == size || fail(error, VfsErrorCode::malformed_archive,
                                 "BF archive ends inside its header or member table");
}

} // namespace

VfsContext::VfsContext(std::shared_ptr<const disc::Image> image, FileRootState roots)
    : image_(std::move(image)), roots_(std::move(roots)) {}

VfsContext::Physical* VfsContext::physical(int32_t handle) {
    if (handle <= 0 || static_cast<size_t>(handle) > files_.size()) return nullptr;
    Physical& file = files_[static_cast<size_t>(handle) - 1];
    return file.open ? &file : nullptr;
}

VfsContext::Member* VfsContext::member(int32_t handle) {
    if (handle >= 0 || handle == INT32_MIN) return nullptr;
    const size_t index = static_cast<size_t>(-1 - handle);
    if (index >= members_.size()) return nullptr;
    Member& member = members_[index];
    return member.opened ? &member : nullptr;
}

bool VFS_Open(VfsContext& context, std::string_view path, uint32_t mode,
              int32_t& handle, VfsError& error) {
    error = {};
    handle = 0;
    if (!context.image_)
        return fail(error, VfsErrorCode::source_error, "no disc source selected");
    // The observed callers use 0x200 for binary read. Disc images have no
    // writable OS-file equivalent, including when a loose ISO file exists.
    if (mode != 0 && mode != 0x200)
        return fail(error, VfsErrorCode::invalid_mode, "VFS disc source is read-only");
    const std::string relative = source_path(path, context.roots_);
    if (relative.empty())
        return fail(error, VfsErrorCode::missing_file, "path is outside the selected disc");
    disc::Error source;
    disc::FileId file;
    if (context.image_->find(relative, file, source)) {
        const disc::Entry* entry = context.image_->entry(file);
        if (!entry || entry->kind != disc::EntryKind::file)
            return fail(error, VfsErrorCode::missing_file, "VFS path is not a file");
        if (context.files_.size() >= static_cast<size_t>(INT32_MAX))
            return fail(error, VfsErrorCode::too_many_handles, "too many VFS handles");
        context.files_.push_back({file, 0, true});
        handle = static_cast<int32_t>(context.files_.size());
        return true;
    }
    if (source.code != disc::ErrorCode::not_found) return source_fail(error, source);
    const int32_t index = VFS_OpenMember(context, path);
    if (index < 0)
        return fail(error, VfsErrorCode::missing_file,
                    "disc file or mounted BF member not found: " + std::string(path));
    handle = -(index + 1);
    return true;
}

bool VFS_Read(VfsContext& context, int32_t handle, void* buffer, size_t requested,
              size_t& bytes_read, VfsError& error) {
    error = {};
    bytes_read = 0;
    if (requested != 0 && !buffer)
        return fail(error, VfsErrorCode::invalid_buffer, "null VFS read buffer");
    if (!context.image_)
        return fail(error, VfsErrorCode::source_error, "no disc source selected");
    disc::FileId file;
    uint64_t start = 0;
    uint64_t remaining = 0;
    uint64_t* position = nullptr;
    if (handle < 0) {
        auto* member = context.member(handle);
        if (!member)
            return fail(error, VfsErrorCode::invalid_handle, "invalid BF member handle");
        file = member->entry.archive_file;
        position = &member->position;
        remaining = member->entry.byte_size - member->position;
        start = member->entry.offset + member->position;
    } else {
        auto* physical = context.physical(handle);
        if (!physical)
            return fail(error, VfsErrorCode::invalid_handle, "invalid VFS file handle");
        file = physical->file;
        position = &physical->position;
        const auto* entry = context.image_->entry(file);
        if (!entry)
            return fail(error, VfsErrorCode::invalid_handle, "stale VFS file handle");
        remaining = physical->position >= entry->byte_size ? 0 :
            entry->byte_size - physical->position;
        start = physical->position;
    }
    bytes_read = static_cast<size_t>(std::min<uint64_t>(requested, remaining));
    if (bytes_read == 0) return true;
    disc::Error source;
    if (!context.image_->read_at(file, start, buffer, bytes_read, source)) {
        bytes_read = 0;
        return source_fail(error, source);
    }
    *position += bytes_read;
    return true;
}

bool VFS_Seek(VfsContext& context, int32_t handle, int64_t offset, unsigned whence,
              uint64_t& position, VfsError& error) {
    error = {};
    position = 0;
    if (!context.image_)
        return fail(error, VfsErrorCode::source_error, "no disc source selected");
    if (whence > 2)
        return fail(error, VfsErrorCode::invalid_seek, "VFS seek whence must be 0, 1 or 2");
    if (handle < 0) {
        auto* member = context.member(handle);
        if (!member)
            return fail(error, VfsErrorCode::invalid_handle, "invalid BF member handle");
        const uint64_t base = whence == 0 ? 0 :
            whence == 1 ? member->position : member->entry.byte_size;
        member->position = clamp_member_seek(base, offset, member->entry.byte_size);
        position = member->position;
        return true;
    }
    auto* physical = context.physical(handle);
    if (!physical)
        return fail(error, VfsErrorCode::invalid_handle, "invalid VFS file handle");
    const auto* entry = context.image_->entry(physical->file);
    if (!entry)
        return fail(error, VfsErrorCode::invalid_handle, "stale VFS file handle");
    const uint64_t base = whence == 0 ? 0 :
        whence == 1 ? physical->position : entry->byte_size;
    if (!seek_from(base, offset, position))
        return fail(error, VfsErrorCode::invalid_seek, "VFS file seek is out of range");
    physical->position = position;
    return true;
}

bool VFS_Close(VfsContext& context, int32_t handle, VfsError& error) {
    error = {};
    if (handle < 0) {
        // Retail validates but does not clear an opened member row here.
        if (!context.member(handle))
            return fail(error, VfsErrorCode::invalid_handle, "invalid BF member handle");
        return true;
    }
    auto* physical = context.physical(handle);
    if (!physical)
        return fail(error, VfsErrorCode::invalid_handle, "invalid VFS file handle");
    physical->open = false;
    return true;
}

int32_t VFS_FindMember(const VfsContext& context, std::string_view name) {
    const std::string key = upper_name(name);
    for (size_t i = 0; i < context.members_.size(); ++i)
        if (context.members_[i].entry.name == key) return static_cast<int32_t>(i);
    return -1;
}

int32_t VFS_OpenMember(VfsContext& context, std::string_view name) {
    const int32_t index = VFS_FindMember(context, name);
    if (index >= 0) {
        auto& member = context.members_[static_cast<size_t>(index)];
        member.opened = true;
        member.position = 0;
    }
    return index;
}

void VFS_AddArchiveEntries(VfsContext& context, const std::vector<BfEntry>& entries) {
    const bool first_batch = context.members_.empty();
    for (const auto& entry : entries) {
        const int32_t existing = first_batch ? -1 : VFS_FindMember(context, entry.name);
        if (existing < 0) context.members_.push_back({entry, 0, false});
        else context.members_[static_cast<size_t>(existing)] = {entry, 0, false};
    }
}

void VFS_FreeArchives(VfsContext& context) {
    context.members_.clear();
}

bool BF_Mount(VfsContext& context, std::string_view path,
              std::vector<BfEntry>& rows, VfsError& error) {
    error = {};
    rows.clear();
    int32_t handle = 0;
    if (!VFS_Open(context, path, 0x200, handle, error)) return false;
    const auto close = [&]() {
        VfsError ignored;
        VFS_Close(context, handle, ignored);
    };
    if (handle < 0) {
        close();
        return fail(error, VfsErrorCode::malformed_archive,
                    "nested BF archive members are unsupported");
    }
    const auto* physical = context.physical(handle);
    const disc::FileId archive_file = physical->file;
    const auto* archive = context.image_->entry(archive_file);
    std::array<uint8_t, 16> header{};
    if (!exact_read(context, handle, header.data(), header.size(), error)) {
        close();
        return false;
    }
    if (std::memcmp(header.data(), "UBIK", 4) != 0) {
        close();
        return fail(error, VfsErrorCode::malformed_archive, "BF archive has no UBIK magic");
    }
    const uint64_t table_offset = little32(header.data() + 8);
    const uint64_t count = little32(header.data() + 12);
    if (count > 100000) {
        close();
        return fail(error, VfsErrorCode::too_many_handles,
                    "BF archive declares too many members");
    }
    if (table_offset > archive->byte_size ||
        count > (archive->byte_size - table_offset) / 267) {
        close();
        return fail(error, VfsErrorCode::malformed_archive,
                    "BF member table exceeds its physical file");
    }
    uint64_t position = 0;
    if (!VFS_Seek(context, handle, static_cast<int64_t>(table_offset), 0, position, error)) {
        close();
        return false;
    }
    rows.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        std::array<uint8_t, 267> raw{};
        if (!exact_read(context, handle, raw.data(), raw.size(), error)) {
            close();
            rows.clear();
            return false;
        }
        size_t name_size = 0;
        while (name_size < 259 && raw[name_size] != 0) ++name_size;
        const uint64_t offset = little32(raw.data() + 259);
        const uint64_t length = little32(raw.data() + 263);
        if (name_size == 0 || offset > archive->byte_size ||
            length > archive->byte_size - offset) {
            close();
            rows.clear();
            return fail(error, VfsErrorCode::malformed_archive,
                        "BF member name or payload range is invalid");
        }
        rows.push_back({static_cast<size_t>(i),
                        std::string(reinterpret_cast<const char*>(raw.data()), name_size),
                        offset, length, archive_file});
    }
    close();
    VFS_AddArchiveEntries(context, rows);
    return true;
}

} // namespace od::port

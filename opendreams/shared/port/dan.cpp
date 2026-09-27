#include "port/dan.h"
#include "port/lz.h"

#include <array>
#include <cstring>
#include <utility>

namespace od::port {
namespace {

bool fail(DanError& error, DanErrorCode code, std::string message) {
    error = {code, std::move(message), {}};
    return false;
}

bool source_fail(DanError& error, const VfsError& source) {
    error = {DanErrorCode::source_error, source.message, source};
    return false;
}

uint16_t little16(const uint8_t* value) {
    return static_cast<uint16_t>(value[0]) |
        static_cast<uint16_t>(value[1] << 8);
}

uint32_t little32(const uint8_t* value) {
    return static_cast<uint32_t>(value[0]) |
        (static_cast<uint32_t>(value[1]) << 8) |
        (static_cast<uint32_t>(value[2]) << 16) |
        (static_cast<uint32_t>(value[3]) << 24);
}

bool read_exact(VfsContext& vfs, int32_t handle, void* destination,
                size_t size, DanError& error) {
    VfsError source;
    size_t count = 0;
    if (!VFS_Read(vfs, handle, destination, size, count, source))
        return source_fail(error, source);
    return count == size || fail(error, DanErrorCode::invalid_header,
                                 "DAN archive ends inside its header or chunk");
}

bool read_at(VfsContext& vfs, int32_t handle, uint64_t offset,
             void* destination, size_t size, DanError& error) {
    VfsError source;
    uint64_t position = 0;
    if (!VFS_Seek(vfs, handle, static_cast<int64_t>(offset), 0, position, source))
        return source_fail(error, source);
    return read_exact(vfs, handle, destination, size, error);
}

std::string slot_text(const uint8_t* data, size_t capacity) {
    size_t used = 0;
    while (used < capacity && data[used] != 0) ++used;
    return std::string(reinterpret_cast<const char*>(data), used);
}

std::string upper_ascii(std::string_view value) {
    std::string result(value);
    for (char& ch : result)
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - ('a' - 'A'));
    return result;
}

std::string_view basename_stem(std::string_view path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
    const size_t dot = path.find_last_of('.');
    return dot == std::string_view::npos ? path : path.substr(0, dot);
}

} // namespace

DanArchive::~DanArchive() { DAN_CloseArchive(*this); }

void DAN_CloseArchive(DanArchive& archive) {
    if (archive.open_) {
        VfsError ignored;
        VFS_Close(*archive.vfs_, archive.handle_, ignored);
    }
    archive.handle_ = 0;
    archive.source_size_ = 0;
    archive.body_offset_ = 0;
    archive.declared_size_ = 0;
    archive.span_ = 0;
    archive.path_.clear();
    archive.names_.clear();
    archive.clips_.clear();
    archive.chunks_.clear();
    archive.texture_chunks_.clear();
    archive.work_.clear();
    archive.texture_work_.clear();
    archive.model_chunk_end_ = 0;
    archive.open_ = false;
    archive.animations_loaded_ = false;
    archive.textures_loaded_ = false;
}

bool DAN_OpenArchive(DanArchive& archive, std::string_view path, DanError& error) {
    error = {};
    DAN_CloseArchive(archive);
    VfsError source;
    int32_t handle = 0;
    if (!VFS_Open(*archive.vfs_, path, 0x200, handle, source)) {
        if (source.code == VfsErrorCode::missing_file)
            return fail(error, DanErrorCode::missing_file, source.message);
        return source_fail(error, source);
    }
    if (handle < 0) {
        VFS_Close(*archive.vfs_, handle, source);
        return fail(error, DanErrorCode::missing_file,
                    "retail DAN opener requires a loose disc file");
    }
    archive.handle_ = handle;
    archive.open_ = true;
    const auto fail_close = [&]() {
        DAN_CloseArchive(archive);
        return false;
    };
    uint64_t size = 0, position = 0;
    if (!VFS_Seek(*archive.vfs_, handle, 0, 2, size, source) ||
        !VFS_Seek(*archive.vfs_, handle, 0, 0, position, source)) {
        source_fail(error, source);
        return fail_close();
    }
    archive.source_size_ = size;
    std::array<uint8_t, 9> first{};
    if (!read_exact(*archive.vfs_, handle, first.data(), first.size(), error))
        return fail_close();
    if (std::memcmp(first.data(), "DANF", 4) != 0 || first[4] != 0) {
        fail(error, DanErrorCode::invalid_header, "DAN archive has no DANF header");
        return fail_close();
    }
    archive.declared_size_ = little32(first.data() + 5);
    if (archive.declared_size_ != size) {
        fail(error, DanErrorCode::invalid_header,
             "DAN declared size differs from its physical file");
        return fail_close();
    }
    std::array<uint8_t, 5> second{};
    if (!read_exact(*archive.vfs_, handle, second.data(), second.size(), error))
        return fail_close();
    if (second[0] != 0) {
        fail(error, DanErrorCode::invalid_header, "DAN second header flag must be zero");
        return fail_close();
    }
    archive.span_ = little32(second.data() + 1);
    std::array<uint8_t, 2> count_bytes{};
    if (!read_exact(*archive.vfs_, handle, count_bytes.data(), count_bytes.size(), error))
        return fail_close();
    const size_t names = little16(count_bytes.data());
    if (names > 64) {
        fail(error, DanErrorCode::invalid_header,
             "DAN material count exceeds the retail directory");
        return fail_close();
    }
    archive.names_.reserve(names);
    for (size_t i = 0; i < names; ++i) {
        DanName name;
        if (!read_exact(*archive.vfs_, handle, name.raw.data(), name.raw.size(), error))
            return fail_close();
        name.text = slot_text(name.raw.data(), name.raw.size());
        archive.names_.push_back(std::move(name));
    }
    if (!read_exact(*archive.vfs_, handle, count_bytes.data(), count_bytes.size(), error))
        return fail_close();
    const size_t clips = little16(count_bytes.data());
    if (clips > 64) {
        fail(error, DanErrorCode::invalid_header,
             "DAN clip count exceeds the retail directory");
        return fail_close();
    }
    archive.clips_.reserve(clips);
    for (size_t i = 0; i < clips; ++i) {
        DanClip clip;
        if (!read_exact(*archive.vfs_, handle, clip.raw.data(), clip.raw.size(), error))
            return fail_close();
        clip.text = slot_text(clip.raw.data(), clip.raw.size());
        archive.clips_.push_back(std::move(clip));
    }
    archive.body_offset_ = 16 + names * 11 + 2 + clips * 13;
    if (archive.body_offset_ > size || archive.body_offset_ != 9u + archive.span_) {
        fail(error, DanErrorCode::invalid_header,
             "DAN directory span does not reach the packed body");
        return fail_close();
    }
    archive.work_.resize(0x96000); // Retail animation work allocation.
    archive.path_ = std::string(path);
    return true;
}

bool DAN_ReadAnimChunks(DanArchive& archive, DanError& error) {
    error = {};
    archive.chunks_.clear();
    archive.animations_loaded_ = false;
    if (!archive.open_)
        return fail(error, DanErrorCode::invalid_state, "no DAN archive is open");
    uint64_t position = archive.body_offset_;
    size_t used = 0;
    bool reached_animations = false;
    while (position < archive.source_size_) {
        if (archive.source_size_ - position < 5)
            return fail(error, DanErrorCode::invalid_chunk,
                        "DAN body ends inside a chunk header");
        std::array<uint8_t, 5> header{};
        if (!read_at(*archive.vfs_, archive.handle_, position,
                     header.data(), header.size(), error)) return false;
        const uint8_t tag = header[0];
        const uint32_t size = little32(header.data() + 1);
        if (size < 5 || size > archive.source_size_ - position)
            return fail(error, DanErrorCode::invalid_chunk,
                        "DAN chunk size exceeds its physical file");
        if (tag == 3) {
            reached_animations = true;
            const uint32_t payload = size - 5;
            if (archive.chunks_.size() >= archive.clips_.size() ||
                payload > archive.work_.size() - used)
                return fail(error, DanErrorCode::invalid_chunk,
                            "DAN animation chunks exceed the declared directory or work buffer");
            if (!read_at(*archive.vfs_, archive.handle_, position + 5,
                         archive.work_.data() + used, payload, error)) return false;
            archive.chunks_.push_back({archive.chunks_.size(), position, payload, used});
            used += payload;
        } else if ((tag != 1 && tag != 2) || reached_animations) {
            return fail(error, DanErrorCode::invalid_chunk,
                        "DAN body has an unexpected tag or chunk order");
        }
        position += size;
    }
    if (archive.chunks_.size() != archive.clips_.size())
        return fail(error, DanErrorCode::invalid_chunk,
                    "DAN animation chunk count differs from its clip directory");
    archive.animations_loaded_ = true;
    return true;
}

bool DAN_Read3DC(DanArchive& archive, std::string_view logical_name,
                 std::vector<uint8_t>& model, DanError& error) {
    error = {};
    model.clear();
    archive.model_chunk_end_ = 0;
    archive.texture_chunks_.clear();
    archive.texture_work_.clear();
    archive.textures_loaded_ = false;
    if (!archive.open_)
        return fail(error, DanErrorCode::invalid_state, "no DAN archive is open");
    if (upper_ascii(basename_stem(logical_name)) !=
        upper_ascii(basename_stem(archive.path_)))
        return fail(error, DanErrorCode::missing_file,
                    "logical .3DC name does not match the open DAN archive");
    if (archive.source_size_ - archive.body_offset_ < 5)
        return fail(error, DanErrorCode::invalid_chunk,
                    "DAN model chunk has no complete header");
    std::array<uint8_t, 5> header{};
    if (!read_at(*archive.vfs_, archive.handle_, archive.body_offset_,
                 header.data(), header.size(), error)) return false;
    const uint32_t size = little32(header.data() + 1);
    if (header[0] != 1 || size < 5 || size > archive.source_size_ - archive.body_offset_ ||
        size - 5 > 0x96000)
        return fail(error, DanErrorCode::invalid_chunk,
                    "DAN model chunk has an invalid type or length");
    std::vector<uint8_t> packed(size - 5);
    if (!read_at(*archive.vfs_, archive.handle_, archive.body_offset_ + 5,
                 packed.data(), packed.size(), error)) return false;
    std::string lz_error;
    if (!LZ_Unpack(packed.data(), packed.size(), model, lz_error))
        return fail(error, DanErrorCode::invalid_chunk,
                    "DAN model decompression failed: " + lz_error);
    archive.model_chunk_end_ = archive.body_offset_ + size;
    return true;
}

bool DAN_ReadTextureChunks(DanArchive& archive, DanError& error) {
    error = {};
    archive.texture_chunks_.clear();
    archive.texture_work_.clear();
    archive.textures_loaded_ = false;
    if (!archive.open_ || !archive.model_chunk_end_)
        return fail(error, DanErrorCode::invalid_state,
                    "read the DAN model before its texture chunks");
    uint64_t position = archive.model_chunk_end_;
    for (size_t index = 0; index < archive.names_.size(); ++index) {
        if (position > archive.source_size_ || archive.source_size_ - position < 5)
            return fail(error, DanErrorCode::invalid_chunk,
                        "DAN texture chunk has no complete header");
        std::array<uint8_t, 5> header{};
        if (!read_at(*archive.vfs_, archive.handle_, position,
                     header.data(), header.size(), error)) return false;
        const uint32_t size = little32(header.data() + 1);
        if (header[0] != 2 || size < 5 || size > archive.source_size_ - position ||
            size - 5 > 0x96000 - archive.texture_work_.size())
            return fail(error, DanErrorCode::invalid_chunk,
                        "DAN texture chunks exceed the declared directory or work buffer");
        const size_t offset = archive.texture_work_.size();
        archive.texture_work_.resize(offset + size - 5);
        if (!read_at(*archive.vfs_, archive.handle_, position + 5,
                     archive.texture_work_.data() + offset, size - 5, error)) return false;
        archive.texture_chunks_.push_back({index, position, size - 5, offset});
        position += size;
    }
    archive.textures_loaded_ = true;
    return true;
}

bool DAN_Load3DM(DanArchive& archive, std::string_view name,
                 std::vector<uint8_t>& bank, DanError& error) {
    error = {};
    bank.clear();
    if (!archive.open_ || !archive.textures_loaded_)
        return fail(error, DanErrorCode::invalid_state,
                    "DAN texture chunks are not loaded");
    const std::string requested = upper_ascii(basename_stem(name));
    for (size_t index = 0; index < archive.names_.size(); ++index) {
        if (upper_ascii(basename_stem(archive.names_[index].text)) != requested)
            continue;
        const DanChunk& chunk = archive.texture_chunks_[index];
        std::string lz_error;
        if (!LZ_Unpack(archive.texture_work_.data() + chunk.work_offset,
                       chunk.payload_size, bank, lz_error))
            return fail(error, DanErrorCode::invalid_chunk,
                        "DAN texture decompression failed: " + lz_error);
        return true;
    }
    return fail(error, DanErrorCode::missing_file,
                "texture name is absent from the open DAN archive");
}

size_t DAN_GetAnimCount(const DanArchive& archive) {
    return archive.clips_.size();
}

std::string_view DAN_GetAnimName(const DanArchive& archive, size_t index) {
    return index < archive.clips_.size() ? std::string_view(archive.clips_[index].text)
                                         : std::string_view{};
}

} // namespace od::port

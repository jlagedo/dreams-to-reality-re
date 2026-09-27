#pragma once

#include "port/vfs.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

enum class FsbErrorCode {
    none,
    missing_file,
    invalid_header,
    invalid_table,
    source_error,
};

struct FsbError {
    FsbErrorCode code = FsbErrorCode::none;
    std::string message;
    VfsError source;
    explicit operator bool() const { return code != FsbErrorCode::none; }
};

struct FsbSample {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

struct FsbClip {
    size_t index = 0;
    uint64_t file_offset = 0;
    uint32_t byte_size = 0;
    size_t blob_offset = 0;
};

// Retail's contiguous sample allocation plus (pointer,size) clip table,
// owned by one selected-source bank rather than process globals.
class FsbBank {
public:
    explicit FsbBank(VfsContext& vfs) : vfs_(&vfs) {}
    bool loaded() const { return loaded_; }
    const std::vector<FsbClip>& clips() const { return clips_; }
    std::string_view path() const { return path_; }

private:
    VfsContext* vfs_ = nullptr;
    std::vector<uint8_t> blob_;
    std::vector<FsbClip> clips_;
    std::string path_;
    bool loaded_ = false;

    friend bool FSB_Load(FsbBank&, std::string_view, FsbError&);
    friend FsbSample FSB_GetSample(const FsbBank&, size_t);
    friend void FSB_Free(FsbBank&);
};

bool FSB_Load(FsbBank& bank, std::string_view path, FsbError& error);
FsbSample FSB_GetSample(const FsbBank& bank, size_t index);
void FSB_Free(FsbBank& bank);

} // namespace od::port

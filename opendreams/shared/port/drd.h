#pragma once

#include "port/vfs.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

enum class DrdErrorCode {
    none,
    missing_file,
    invalid_state,
    invalid_header,
    invalid_entry,
    source_error,
};

struct DrdError {
    DrdErrorCode code = DrdErrorCode::none;
    std::string message;
    VfsError source;
    explicit operator bool() const { return code != DrdErrorCode::none; }
};

struct DrdBytes {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

struct DrdLine {
    uint32_t centiseconds = 0;
    uint32_t ticks_15hz = 0;
    size_t text_offset = 0; // In the reusable entry buffer.
    size_t text_length = 0;
};

// Retail's one-open-bank, one-reusable-entry state for a selected VFS source.
// The VfsContext must outlive the bank. Views are invalid after another load
// or close, so a catalog caller snapshots the fields it needs.
class DrdBank {
public:
    explicit DrdBank(VfsContext& vfs) : vfs_(&vfs) {}
    ~DrdBank();
    DrdBank(const DrdBank&) = delete;
    DrdBank& operator=(const DrdBank&) = delete;

    bool is_open() const { return open_; }
    size_t entry_count() const { return offsets_.size(); }
    int current_index() const { return current_index_; }
    uint64_t entry_offset(size_t index) const;
    uint64_t entry_size(size_t index) const;
    DrdBytes wave() const;
    const std::vector<DrdLine>& lines() const { return lines_; }
    uint32_t entry_duration_ticks() const { return entry_duration_ticks_; }
    std::string_view line_text(size_t index) const;
    std::string_view path() const { return path_; }

private:
    VfsContext* vfs_ = nullptr;
    int32_t handle_ = 0;
    uint64_t source_size_ = 0;
    uint32_t max_entry_size_ = 0;
    std::vector<uint32_t> offsets_;
    std::vector<uint8_t> entry_buffer_;
    std::vector<DrdLine> lines_;
    size_t wave_offset_ = 0;
    size_t wave_size_ = 0;
    size_t portrait_offset_ = 0;
    size_t portrait_size_ = 0;
    uint32_t entry_duration_ticks_ = 0;
    std::string path_;
    int current_index_ = -1;
    bool open_ = false;

    friend bool DRD_Open(DrdBank&, std::string_view, DrdError&);
    friend bool DRD_LoadEntry(DrdBank&, size_t, DrdError&);
    friend size_t DRD_GetLineCount(const DrdBank&);
    friend uint32_t DRD_GetEntryDuration(const DrdBank&);
    friend uint32_t DRD_GetLineDuration(const DrdBank&, size_t);
    friend DrdBytes DRD_GetPortrait(const DrdBank&);
    friend void DRD_Close(DrdBank&);
};

bool DRD_Open(DrdBank& bank, std::string_view relative_path, DrdError& error);
bool DRD_LoadEntry(DrdBank& bank, size_t index, DrdError& error);
bool DRD_SelectEntry(DrdBank& bank, size_t index, DrdError& error);
size_t DRD_GetLineCount(const DrdBank& bank);
uint32_t DRD_GetEntryDuration(const DrdBank& bank);
uint32_t DRD_GetLineDuration(const DrdBank& bank, size_t index);
DrdBytes DRD_GetPortrait(const DrdBank& bank);
void DRD_Close(DrdBank& bank);

} // namespace od::port

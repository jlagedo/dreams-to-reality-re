#include "port/drd.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <limits>
#include <utility>

namespace od::port {
namespace {

bool fail(DrdError& error, DrdErrorCode code, std::string message) {
    error = {code, std::move(message), {}};
    return false;
}

bool source_fail(DrdError& error, const VfsError& source) {
    error = {DrdErrorCode::source_error, source.message, source};
    return false;
}

uint32_t little32(const uint8_t* value) {
    return static_cast<uint32_t>(value[0]) |
        (static_cast<uint32_t>(value[1]) << 8) |
        (static_cast<uint32_t>(value[2]) << 16) |
        (static_cast<uint32_t>(value[3]) << 24);
}

bool read_exact(VfsContext& vfs, int32_t handle, void* destination,
                size_t size, DrdErrorCode code, DrdError& error) {
    VfsError source;
    size_t count = 0;
    if (!VFS_Read(vfs, handle, destination, size, count, source))
        return source_fail(error, source);
    return count == size || fail(error, code, "DIALOG.DRD has a truncated read");
}

} // namespace

DrdBank::~DrdBank() { DRD_Close(*this); }

uint64_t DrdBank::entry_offset(size_t index) const {
    return index < offsets_.size() ? offsets_[index] : 0;
}

uint64_t DrdBank::entry_size(size_t index) const {
    if (index >= offsets_.size()) return 0;
    const uint64_t end = index + 1 < offsets_.size() ? offsets_[index + 1] : source_size_;
    return end - offsets_[index];
}

DrdBytes DrdBank::wave() const {
    return current_index_ >= 0 && wave_size_ != 0
        ? DrdBytes{entry_buffer_.data() + wave_offset_, wave_size_} : DrdBytes{};
}

std::string_view DrdBank::line_text(size_t index) const {
    if (current_index_ < 0 || index >= lines_.size()) return {};
    const auto& line = lines_[index];
    return {reinterpret_cast<const char*>(entry_buffer_.data() + line.text_offset),
            line.text_length};
}

void DRD_Close(DrdBank& bank) {
    if (bank.open_) {
        VfsError ignored;
        VFS_Close(*bank.vfs_, bank.handle_, ignored);
    }
    bank.handle_ = 0;
    bank.source_size_ = 0;
    bank.max_entry_size_ = 0;
    bank.offsets_.clear();
    bank.entry_buffer_.clear();
    bank.lines_.clear();
    bank.wave_offset_ = bank.wave_size_ = 0;
    bank.portrait_offset_ = bank.portrait_size_ = 0;
    bank.entry_duration_ticks_ = 0;
    bank.path_.clear();
    bank.current_index_ = -1;
    bank.open_ = false;
}

bool DRD_Open(DrdBank& bank, std::string_view relative_path, DrdError& error) {
    error = {};
    DRD_Close(bank);
    std::string path;
    if (relative_path.size() >= 2 && relative_path[1] == ':')
        path = std::string(relative_path);
    else
        path = std::string(FILE_GetInstallRoot(bank.vfs_->roots())) +
               std::string(relative_path);
    VfsError source;
    int32_t handle = 0;
    if (!VFS_Open(*bank.vfs_, path, 0x200, handle, source)) {
        if (source.code == VfsErrorCode::missing_file)
            return fail(error, DrdErrorCode::missing_file, source.message);
        return source_fail(error, source);
    }
    if (handle < 0) {
        VFS_Close(*bank.vfs_, handle, source);
        return fail(error, DrdErrorCode::missing_file,
                    "retail DRD opener requires a loose disc file");
    }
    bank.handle_ = handle;
    bank.open_ = true;
    const auto fail_close = [&]() {
        DRD_Close(bank);
        return false;
    };
    uint64_t size = 0, position = 0;
    if (!VFS_Seek(*bank.vfs_, handle, 0, 2, size, source) ||
        !VFS_Seek(*bank.vfs_, handle, 0, 0, position, source)) {
        source_fail(error, source);
        return fail_close();
    }
    bank.source_size_ = size;
    std::array<uint8_t, 16> header{};
    if (!read_exact(*bank.vfs_, handle, header.data(), header.size(),
                    DrdErrorCode::invalid_header, error)) return fail_close();
    const uint32_t declared_size = little32(header.data() + 4);
    const uint32_t count = little32(header.data() + 8);
    bank.max_entry_size_ = little32(header.data() + 12);
    if (std::memcmp(header.data(), "DRDF", 4) != 0 || declared_size != size ||
        count == 0 || count > 4096 || bank.max_entry_size_ < 14 ||
        bank.max_entry_size_ > 32 * 1024 * 1024 ||
        bank.max_entry_size_ > size) {
        fail(error, DrdErrorCode::invalid_header, "invalid DRDF bank header");
        return fail_close();
    }
    std::array<uint8_t, 5> table_header{};
    if (!read_exact(*bank.vfs_, handle, table_header.data(), table_header.size(),
                    DrdErrorCode::invalid_header, error)) return fail_close();
    const uint64_t table_size = 5 + static_cast<uint64_t>(count) * 4;
    const uint64_t table_end = 16 + table_size;
    if (table_header[0] != 0 || little32(table_header.data() + 1) != table_size ||
        table_end >= size) {
        fail(error, DrdErrorCode::invalid_header,
             "DRDF offset table header is invalid");
        return fail_close();
    }
    std::vector<uint8_t> raw_offsets(static_cast<size_t>(count) * 4);
    if (!read_exact(*bank.vfs_, handle, raw_offsets.data(), raw_offsets.size(),
                    DrdErrorCode::invalid_header, error)) return fail_close();
    bank.offsets_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t offset = little32(raw_offsets.data() + i * 4);
        if ((i == 0 && offset != table_end) ||
            (i != 0 && offset <= bank.offsets_.back()) || offset >= size) {
            fail(error, DrdErrorCode::invalid_header,
                 "DRDF entry offsets are out of order or range");
            return fail_close();
        }
        bank.offsets_.push_back(offset);
    }
    bank.entry_buffer_.resize(bank.max_entry_size_);
    bank.path_ = std::string(relative_path);
    return true;
}

bool DRD_LoadEntry(DrdBank& bank, size_t index, DrdError& error) {
    error = {};
    if (!bank.open_ || index >= bank.offsets_.size())
        return fail(error, DrdErrorCode::invalid_state,
                    "DRDF entry is outside the open bank");
    if (bank.current_index_ == static_cast<int>(index)) return true;
    bank.current_index_ = -1;
    bank.lines_.clear();
    bank.wave_size_ = bank.portrait_size_ = 0;
    bank.entry_duration_ticks_ = 0;
    VfsError source;
    uint64_t position = 0;
    if (!VFS_Seek(*bank.vfs_, bank.handle_, bank.offsets_[index], 0,
                  position, source)) return source_fail(error, source);
    std::array<uint8_t, 9> header{};
    if (!read_exact(*bank.vfs_, bank.handle_, header.data(), header.size(),
                    DrdErrorCode::invalid_entry, error)) return false;
    const uint32_t entry_size = little32(header.data() + 1);
    const uint32_t line_count = little32(header.data() + 5);
    const uint64_t expected_size = bank.entry_size(index);
    if (header[0] != 1 || entry_size != expected_size || entry_size < 19 ||
        entry_size > bank.max_entry_size_ || line_count > 64)
        return fail(error, DrdErrorCode::invalid_entry,
                    "DRDF entry tag, size or line count is invalid");
    const size_t remaining = entry_size - 9;
    if (!read_exact(*bank.vfs_, bank.handle_, bank.entry_buffer_.data(), remaining,
                    DrdErrorCode::invalid_entry, error)) return false;
    const auto* bytes = bank.entry_buffer_.data();
    if (bytes[0] != 2 || remaining < 10)
        return fail(error, DrdErrorCode::invalid_entry,
                    "DRDF entry lacks a WAVE tag");
    const uint32_t wave_block = little32(bytes + 1);
    if (wave_block < 13 || static_cast<uint64_t>(wave_block) + 5 > remaining ||
        std::memcmp(bytes + 5, "RIFF", 4) != 0)
        return fail(error, DrdErrorCode::invalid_entry,
                    "DRDF WAVE block is out of range");
    const uint64_t riff_size = static_cast<uint64_t>(little32(bytes + 9)) + 8;
    if (riff_size > wave_block - 5)
        return fail(error, DrdErrorCode::invalid_entry,
                    "DRDF RIFF length exceeds its WAVE block");
    bank.wave_offset_ = 5;
    bank.wave_size_ = static_cast<size_t>(riff_size);
    const uint8_t* wave = bytes + bank.wave_offset_;
    if (riff_size < 44 || std::memcmp(wave+12,"fmt ",4)!=0 ||
        std::memcmp(wave+36,"data",4)!=0)
        return fail(error, DrdErrorCode::invalid_entry,
                    "DRDF voice lacks the retail 44-byte WAVE layout");
    const uint32_t byte_rate = little32(wave+28);
    const uint16_t block_align = static_cast<uint16_t>(wave[32] | (wave[33]<<8));
    const uint32_t data_bytes = little32(wave+40);
    if (!byte_rate || !block_align || data_bytes > riff_size-44)
        return fail(error, DrdErrorCode::invalid_entry,
                    "DRDF voice duration fields exceed its WAVE block");
    const double entry_ticks = static_cast<double>(data_bytes) * 15.0 /
        (static_cast<double>(byte_rate) * block_align);
    if (entry_ticks > std::numeric_limits<uint32_t>::max())
        return fail(error, DrdErrorCode::invalid_entry,
                    "DRDF voice duration exceeds the retail counter");
    bank.entry_duration_ticks_ = static_cast<uint32_t>(std::nearbyint(entry_ticks));
    const size_t text_at = wave_block;
    if (bytes[text_at] != 3)
        return fail(error, DrdErrorCode::invalid_entry,
                    "DRDF entry lacks a timed-caption tag");
    const uint32_t text_block = little32(bytes + text_at + 1);
    if (text_block < 5 || text_block > remaining - text_at)
        return fail(error, DrdErrorCode::invalid_entry,
                    "DRDF caption block is out of range");
    size_t cursor = text_at + 5;
    const size_t text_end = text_at + text_block;
    bank.lines_.reserve(line_count);
    for (uint32_t i = 0; i < line_count; ++i) {
        if (cursor + 5 > text_end)
            return fail(error, DrdErrorCode::invalid_entry,
                        "DRDF caption line header is truncated");
        const uint32_t centiseconds = little32(bytes + cursor);
        const size_t length = bytes[cursor + 4];
        cursor += 5;
        if (length == 0 || length > text_end - cursor)
            return fail(error, DrdErrorCode::invalid_entry,
                        "DRDF caption line text is truncated");
        size_t used = 0;
        while (used < length && bytes[cursor + used] != 0) ++used;
        const uint32_t ticks = static_cast<uint32_t>(
            (static_cast<uint64_t>(centiseconds) * 15) / 100);
        bank.lines_.push_back({centiseconds, ticks, cursor, used});
        cursor += length;
    }
    if (cursor != text_end)
        return fail(error, DrdErrorCode::invalid_entry,
                    "DRDF caption payload has unexplained trailing bytes");
    if (text_end < remaining) {
        if (remaining - text_end < 5 || bytes[text_end] != 4)
            return fail(error, DrdErrorCode::invalid_entry,
                        "DRDF portrait tag is invalid");
        const uint32_t portrait_block = little32(bytes + text_end + 1);
        if (portrait_block < 5 || portrait_block != remaining - text_end)
            return fail(error, DrdErrorCode::invalid_entry,
                        "DRDF portrait block is out of range");
        bank.portrait_offset_ = text_end + 5;
        bank.portrait_size_ = portrait_block - 5;
    }
    bank.current_index_ = static_cast<int>(index);
    return true;
}

size_t DRD_GetLineCount(const DrdBank& bank) {
    return bank.current_index_ >= 0 ? bank.lines_.size() : 0;
}

bool DRD_SelectEntry(DrdBank& bank, size_t index, DrdError& error) {
    return DRD_LoadEntry(bank,index,error);
}

uint32_t DRD_GetEntryDuration(const DrdBank& bank) {
    return bank.current_index_ >= 0 ? bank.entry_duration_ticks_ : 0;
}

uint32_t DRD_GetLineDuration(const DrdBank& bank, size_t index) {
    if (bank.current_index_ < 0 || index >= bank.lines_.size()) return 0;
    const uint32_t start = bank.lines_[index].ticks_15hz;
    const uint32_t end = index+1 < bank.lines_.size()
        ? bank.lines_[index+1].ticks_15hz : DRD_GetEntryDuration(bank);
    return end >= start ? end-start : 0;
}

DrdBytes DRD_GetPortrait(const DrdBank& bank) {
    return bank.current_index_ >= 0 && bank.portrait_size_ != 0
        ? DrdBytes{bank.entry_buffer_.data() + bank.portrait_offset_,
                   bank.portrait_size_} : DrdBytes{};
}

} // namespace od::port

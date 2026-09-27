#include "port/ddat.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace od::port {
namespace {

bool fail(DdatError& error, DdatErrorCode code, std::string message) {
    error = {code, std::move(message), {}};
    return false;
}

bool source_fail(DdatError& error, const VfsError& source) {
    error = {DdatErrorCode::source_error, source.message, source};
    return false;
}

uint32_t little32(const uint8_t* value) {
    return static_cast<uint32_t>(value[0]) |
        (static_cast<uint32_t>(value[1]) << 8) |
        (static_cast<uint32_t>(value[2]) << 16) |
        (static_cast<uint32_t>(value[3]) << 24);
}

void write_name(uint8_t* cell, const char* name) {
    std::memcpy(cell, name, std::strlen(name) + 1);
}

} // namespace

bool RLE_UnpackZeros(const uint8_t* source, size_t source_size,
                     uint8_t* destination, size_t capacity, size_t& written,
                     DdatError& error) {
    error = {};
    written = 0;
    if ((source_size != 0 && !source) || (capacity != 0 && !destination))
        return fail(error, DdatErrorCode::invalid_input,
                    "null zero-run input or output buffer");
    for (size_t i = 0; i < source_size; ++i) {
        if (source[i] != 0) {
            if (written == capacity)
                return fail(error, DdatErrorCode::output_overflow,
                            "zero-run output exceeds its buffer");
            destination[written++] = source[i];
            continue;
        }
        if (++i == source_size)
            return fail(error, DdatErrorCode::truncated_zero_run,
                        "zero-run escape has no count byte");
        const size_t run = source[i];
        if (run > capacity - written)
            return fail(error, DdatErrorCode::output_overflow,
                        "zero-run output exceeds its buffer");
        if (run != 0) std::memset(destination + written, 0, run);
        written += run;
    }
    return true;
}

bool DdatBank::has_record(std::string_view name) const {
    if (!has_source_) return false;
    return std::find(names_.begin(), names_.end(), name) != names_.end();
}

std::string_view DdatBank::record_name(size_t index) const {
    if (index >= record_count) return {};
    return has_source_ ? std::string_view(names_[index]) : std::string_view("EMPTY");
}

const std::array<uint8_t, DdatBank::record_size>* DdatBank::empty_record(size_t index) const {
    return index < empty_records_.size() ? &empty_records_[index] : nullptr;
}

bool DdatBank::set_active_record(std::string_view name) {
    if (!has_record(name)) return false;
    active_name_ = std::string(name);
    return true;
}

bool DdatBank::decode(size_t index, std::array<uint8_t, record_size>& output,
                      DdatError& error) const {
    output.fill(0);
    const size_t start = data_offset + offsets_[index];
    const size_t length = offsets_[index + 1] - offsets_[index];
    size_t written = 0;
    if (!RLE_UnpackZeros(source_.data() + start, length,
                         output.data(), output.size(), written, error)) return false;
    if (written != record_size)
        return fail(error, DdatErrorCode::malformed_bank,
                    "DREAMS.DAT record " + std::to_string(index) +
                    " does not expand to 0x2200 bytes");
    return true;
}

void DDAT_InitEmptyRecords(DdatBank& bank) {
    bank = {};
    write_name(bank.boot_record_.data(), "EMPTY");
    for (size_t i = 0; i < 8; ++i)
        write_name(bank.boot_record_.data() + 0x200 + i * 0x80, "EMPTY");
    for (size_t i = 0; i < 16; ++i) {
        const size_t start = 0x600 + i * 0xc0;
        write_name(bank.boot_record_.data() + start, "EMPTY");
        write_name(bank.boot_record_.data() + start + 0xc, "EMPTY");
    }
    bank.empty_records_.assign(DdatBank::record_count, bank.boot_record_);
    bank.working_record_ = bank.boot_record_;
    bank.active_name_ = "EMPTY";
    bank.previous_name_ = "EMPTY";
}

bool DDAT_LoadBytes(DdatBank& bank, std::vector<uint8_t> bytes,
                    DdatError& error) {
    error = {};
    if (bytes.size() < DdatBank::data_offset ||
        bytes.size() > DdatBank::max_file_size)
        return fail(error, DdatErrorCode::malformed_bank,
                    "DREAMS.DAT is outside the retail bank size");
    DdatBank candidate;
    candidate.fallback_enabled_ = bank.fallback_enabled_;
    candidate.source_ = std::move(bytes);
    for (size_t i = 0; i < candidate.offsets_.size(); ++i)
        candidate.offsets_[i] = little32(candidate.source_.data() + i * 4);
    if (candidate.offsets_[0] != 0 ||
        static_cast<uint64_t>(candidate.offsets_.back()) + DdatBank::data_offset !=
            candidate.source_.size())
        return fail(error, DdatErrorCode::malformed_bank,
                    "DREAMS.DAT index does not span the file");
    for (size_t i = 0; i < DdatBank::record_count; ++i) {
        if (candidate.offsets_[i] >= candidate.offsets_[i + 1])
            return fail(error, DdatErrorCode::malformed_bank,
                        "DREAMS.DAT record offsets are empty or reversed");
        const size_t start = DdatBank::data_offset + candidate.offsets_[i];
        const size_t end = DdatBank::data_offset + candidate.offsets_[i + 1];
        const auto first = candidate.source_.begin() + static_cast<ptrdiff_t>(start);
        const auto last = candidate.source_.begin() + static_cast<ptrdiff_t>(end);
        const auto terminator = std::find(first, last, uint8_t{0});
        if (terminator == last || terminator == first)
            return fail(error, DdatErrorCode::malformed_bank,
                        "DREAMS.DAT record name is missing or unterminated");
        candidate.names_[i].assign(reinterpret_cast<const char*>(&*first),
                                   static_cast<size_t>(terminator - first));
    }
    if (!candidate.decode(0, candidate.boot_record_, error)) return false;
    candidate.working_record_ = candidate.boot_record_;
    candidate.active_name_ = candidate.names_[0];
    candidate.previous_name_ = candidate.names_[0];
    candidate.has_source_ = true;
    bank = std::move(candidate);
    return true;
}

bool DDAT_Load(VfsContext& vfs, DdatBank& bank, DdatError& error) {
    error = {};
    VfsError source;
    int32_t handle = 0;
    if (!VFS_Open(vfs, "DREAMS.DAT", 0x200, handle, source)) {
        if (source.code == VfsErrorCode::missing_file) {
            DDAT_InitEmptyRecords(bank);
            return true;
        }
        return source_fail(error, source);
    }
    const auto close = [&]() {
        VfsError ignored;
        VFS_Close(vfs, handle, ignored);
    };
    uint64_t size = 0;
    if (!VFS_Seek(vfs, handle, 0, 2, size, source)) {
        close();
        return source_fail(error, source);
    }
    if (size < DdatBank::data_offset || size > DdatBank::max_file_size) {
        close();
        return fail(error, DdatErrorCode::malformed_bank,
                    "DREAMS.DAT is outside the retail bank size");
    }
    uint64_t position = 0;
    if (!VFS_Seek(vfs, handle, 0, 0, position, source)) {
        close();
        return source_fail(error, source);
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    size_t count = 0;
    if (!VFS_Read(vfs, handle, bytes.data(), bytes.size(), count, source)) {
        close();
        return source_fail(error, source);
    }
    close();
    if (count != bytes.size())
        return fail(error, DdatErrorCode::malformed_bank,
                    "DREAMS.DAT ended before its reported file size");
    return DDAT_LoadBytes(bank, std::move(bytes), error);
}

bool DDAT_LoadRecord(DdatBank& bank, std::string_view name,
                     const uint8_t*& record, DdatError& error) {
    error = {};
    record = nullptr;
    bank.used_previous_fallback_ = false;
    if (!bank.has_source_) {
        bank.working_record_ = bank.boot_record_;
        bank.used_previous_fallback_ = true;
        record = bank.working_record_.data();
        return true;
    }
    size_t index = DdatBank::record_count;
    if (name != "EMPTY") {
        for (size_t i = 0; i < DdatBank::record_count; ++i)
            if (bank.names_[i] == name) { index = i; break; }
    }
    if (index != DdatBank::record_count) {
        bank.previous_name_ = bank.active_name_;
    } else if (bank.fallback_enabled_) {
        for (size_t i = 0; i < DdatBank::record_count; ++i)
            if (bank.names_[i] == bank.previous_name_) { index = i; break; }
        bank.used_previous_fallback_ = index != DdatBank::record_count;
    }
    if (index == DdatBank::record_count)
        return fail(error, DdatErrorCode::unknown_record,
                    "DREAMS.DAT record not found: " + std::string(name));
    if (!bank.decode(index, bank.working_record_, error)) return false;
    record = bank.working_record_.data();
    return true;
}

} // namespace od::port

#pragma once

#include "port/vfs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

enum class DdatErrorCode {
    none,
    invalid_input,
    truncated_zero_run,
    output_overflow,
    malformed_bank,
    unknown_record,
    source_error,
};

struct DdatError {
    DdatErrorCode code = DdatErrorCode::none;
    std::string message;
    VfsError source;
    explicit operator bool() const { return code != DdatErrorCode::none; }
};

class DdatBank {
public:
    static constexpr size_t record_count = 150;
    static constexpr size_t record_size = 0x2200;
    static constexpr size_t data_offset = 0x400;
    static constexpr size_t max_file_size = 0x25400; // Retail read buffer.

    bool has_source() const { return has_source_; }
    bool used_previous_fallback() const { return used_previous_fallback_; }
    bool has_record(std::string_view name) const;
    std::string_view record_name(size_t index) const;
    const std::array<uint8_t, record_size>& boot_record() const { return boot_record_; }
    const std::array<uint8_t, record_size>* empty_record(size_t index) const;
    void set_previous_fallback_enabled(bool enabled) { fallback_enabled_ = enabled; }
    // The game switches its active record pointer outside DDAT_LoadRecord.
    // This setter represents that external state transition for later runtime calls.
    bool set_active_record(std::string_view name);

private:
    std::vector<uint8_t> source_;
    std::array<uint32_t, record_count + 1> offsets_{};
    std::array<std::string, record_count> names_{};
    std::array<uint8_t, record_size> boot_record_{};
    std::array<uint8_t, record_size> working_record_{};
    std::vector<std::array<uint8_t, record_size>> empty_records_;
    std::string active_name_;
    std::string previous_name_;
    bool has_source_ = false;
    bool fallback_enabled_ = true;
    bool used_previous_fallback_ = false;

    bool decode(size_t index, std::array<uint8_t, record_size>& output,
                DdatError& error) const;

    friend void DDAT_InitEmptyRecords(DdatBank&);
    friend bool DDAT_LoadBytes(DdatBank&, std::vector<uint8_t>, DdatError&);
    friend bool DDAT_LoadRecord(DdatBank&, std::string_view, const uint8_t*&, DdatError&);
};

// Adapted retail entry points. The zero-run routine writes 00 N as N zeros;
// explicit output bounds and errors replace unchecked retail memory writes.
bool RLE_UnpackZeros(const uint8_t* source, size_t source_size,
                     uint8_t* destination, size_t capacity, size_t& written,
                     DdatError& error);
void DDAT_InitEmptyRecords(DdatBank& bank);
bool DDAT_Load(VfsContext& vfs, DdatBank& bank, DdatError& error);
bool DDAT_LoadRecord(DdatBank& bank, std::string_view name,
                     const uint8_t*& record, DdatError& error);

// New checked byte boundary used by DDAT_Load and data-free fixtures.
bool DDAT_LoadBytes(DdatBank& bank, std::vector<uint8_t> bytes,
                    DdatError& error);

} // namespace od::port

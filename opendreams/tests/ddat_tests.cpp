#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/ddat.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char* expression, int line) {
    if (!condition) std::cerr << "line " << line << ": " << expression << "\n";
    return condition;
}
#define CHECK(value) do { if (!check((value), #value, __LINE__)) return false; } while (0)

void little32(uint8_t* destination, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        destination[i] = static_cast<uint8_t>(value >> (8 * i));
}

std::vector<uint8_t> synthetic_bank() {
    std::vector<uint8_t> bytes(od::port::DdatBank::data_offset);
    for (size_t i = 0; i < od::port::DdatBank::record_count; ++i) {
        little32(bytes.data() + i * 4,
                 static_cast<uint32_t>(bytes.size() - od::port::DdatBank::data_offset));
        const std::string name = "Project" + std::to_string(i);
        bytes.insert(bytes.end(), name.begin(), name.end());
        size_t zeros = od::port::DdatBank::record_size - name.size();
        while (zeros != 0) {
            const auto run = static_cast<uint8_t>(zeros > 255 ? 255 : zeros);
            bytes.push_back(0);
            bytes.push_back(run);
            zeros -= run;
        }
    }
    little32(bytes.data() + od::port::DdatBank::record_count * 4,
             static_cast<uint32_t>(bytes.size() - od::port::DdatBank::data_offset));
    return bytes;
}

bool fixtures() {
    od::port::DdatError error;
    const std::array<uint8_t, 7> compressed{'A', 0, 3, 'B', 0, 0, 'C'};
    std::array<uint8_t, 8> output{};
    size_t written = 0;
    CHECK(od::port::RLE_UnpackZeros(compressed.data(), compressed.size(),
                                     output.data(), output.size(), written, error));
    CHECK(written == 6);
    CHECK(output[0] == 'A' && output[1] == 0 && output[3] == 0 &&
          output[4] == 'B' && output[5] == 'C');
    const std::array<uint8_t, 1> trailing{0};
    CHECK(!od::port::RLE_UnpackZeros(trailing.data(), trailing.size(),
                                      output.data(), output.size(), written, error));
    CHECK(error.code == od::port::DdatErrorCode::truncated_zero_run);
    const std::array<uint8_t, 2> long_run{0, 9};
    CHECK(!od::port::RLE_UnpackZeros(long_run.data(), long_run.size(),
                                      output.data(), output.size(), written, error));
    CHECK(error.code == od::port::DdatErrorCode::output_overflow);

    od::port::DdatBank bank;
    auto encoded = synthetic_bank();
    CHECK(od::port::DDAT_LoadBytes(bank, encoded, error));
    CHECK(bank.has_source());
    CHECK(bank.record_name(149) == "Project149");
    CHECK(bank.has_record("Project62"));
    CHECK(std::memcmp(bank.boot_record().data(), "Project0", 8) == 0);
    const uint8_t* record = nullptr;
    CHECK(od::port::DDAT_LoadRecord(bank, "Project62", record, error));
    CHECK(record != nullptr && std::memcmp(record, "Project62", 9) == 0);
    CHECK(record[0x21ff] == 0 && !bank.used_previous_fallback());
    const uint8_t* reused_buffer = record;
    CHECK(bank.set_active_record("Project4"));
    CHECK(od::port::DDAT_LoadRecord(bank, "Project5", record, error));
    CHECK(record == reused_buffer && std::memcmp(record, "Project5", 8) == 0);
    CHECK(od::port::DDAT_LoadRecord(bank, "Missing", record, error));
    CHECK(std::memcmp(record, "Project4", 8) == 0);
    CHECK(bank.used_previous_fallback());
    bank.set_previous_fallback_enabled(false);
    CHECK(!od::port::DDAT_LoadRecord(bank, "Missing", record, error));
    CHECK(error.code == od::port::DdatErrorCode::unknown_record);

    little32(encoded.data() + od::port::DdatBank::record_count * 4, 1);
    CHECK(!od::port::DDAT_LoadBytes(bank, std::move(encoded), error));
    CHECK(error.code == od::port::DdatErrorCode::malformed_bank);
    CHECK(bank.has_record("Project62")); // Failed reload preserves the old bank.
    od::port::DDAT_InitEmptyRecords(bank);
    CHECK(!bank.has_source() && bank.record_name(0) == "EMPTY");
    CHECK(std::memcmp(bank.boot_record().data(), "EMPTY", 5) == 0);
    CHECK(std::memcmp(bank.boot_record().data() + 0x200, "EMPTY", 5) == 0);
    CHECK(std::memcmp(bank.boot_record().data() + 0x600 + 0xc, "EMPTY", 5) == 0);
    CHECK(bank.empty_record(149) != nullptr);
    CHECK(std::memcmp(bank.empty_record(149)->data(), "EMPTY", 5) == 0);
    CHECK(od::port::DDAT_LoadRecord(bank, "Project0", record, error));
    CHECK(std::memcmp(record, "EMPTY", 5) == 0 && bank.used_previous_fallback());
    return true;
}

uint32_t crc32(uint32_t current, const uint8_t* bytes, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        current ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            current = (current >> 1) ^ ((current & 1) ? 0xedb88320u : 0u);
    }
    return current;
}

int corpus() {
    const std::array<const char*, 2> cues{std::getenv("DREAMS_CUE1"),
                                           std::getenv("DREAMS_CUE2")};
    if (!cues[0] || !cues[1] || !*cues[0] || !*cues[1]) {
        std::cout << "SKIP: set DREAMS_CUE1 and DREAMS_CUE2 for DDAT corpus\n";
        return 77;
    }
    const std::array<uint32_t, 2> expected_crc{0xf18ef135u, 0xfd8d3b99u};
    std::array<std::array<uint32_t, od::port::DdatBank::record_count>, 2> record_crc{};
    for (size_t disc = 0; disc < cues.size(); ++disc) {
        od::disc::Error image_error;
        auto opened = od::disc::Image::open(std::filesystem::u8path(cues[disc]), image_error);
        if (!opened) {
            std::cerr << image_error.message << "\n";
            return 1;
        }
        std::shared_ptr<const od::disc::Image> image(std::move(opened));
        od::port::VfsContext vfs(image);
        od::port::DdatBank bank;
        od::port::DdatError error;
        if (!od::port::DDAT_Load(vfs, bank, error) || !bank.has_source()) {
            std::cerr << "DDAT load: " << error.message << "\n";
            return 1;
        }
        uint32_t checksum = 0xffffffffu;
        for (size_t i = 0; i < od::port::DdatBank::record_count; ++i) {
            const std::string name = "Project" + std::to_string(i);
            const uint8_t* record = nullptr;
            if (bank.record_name(i) != name || !bank.has_record(name) ||
                !od::port::DDAT_LoadRecord(bank, name, record, error) ||
                bank.used_previous_fallback() || !record ||
                std::memcmp(record, name.data(), name.size()) != 0) {
                std::cerr << "Disc " << disc + 1 << " record " << i << ": "
                          << error.message << "\n";
                return 1;
            }
            checksum = crc32(checksum, record, od::port::DdatBank::record_size);
            record_crc[disc][i] = crc32(0xffffffffu, record,
                od::port::DdatBank::record_size) ^ 0xffffffffu;
        }
        checksum ^= 0xffffffffu;
        if (checksum != expected_crc[disc]) {
            std::cerr << "Disc " << disc + 1 << " DDAT CRC differs from Python oracle\n";
            return 1;
        }
        std::cout << "Disc " << disc + 1 << ": 150 DDAT records, CRC "
                  << std::hex << checksum << std::dec << "\n";
    }
    std::vector<size_t> different;
    for (size_t i = 0; i < od::port::DdatBank::record_count; ++i)
        if (record_crc[0][i] != record_crc[1][i]) different.push_back(i);
    if (different != std::vector<size_t>{31, 41, 55, 69, 75, 87}) {
        std::cerr << "DDAT source-copy differences do not match the measured corpus\n";
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--corpus") == 0) return corpus();
    if (argc != 1) return 2;
    return fixtures() ? 0 : 1;
}

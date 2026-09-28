#include "port/save_game.h"

#include <cstring>
#include <fstream>

namespace od::port {
namespace {

int32_t le32(const uint8_t* bytes) {
    return static_cast<int32_t>(static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24));
}

std::filesystem::path game_directory(const std::filesystem::path& save_root) {
    return save_root / "data" / "game";
}

} // namespace

std::string SaveIndex::name(size_t slot) const {
    if (slot >= slot_count) return {};
    const auto& raw = names[slot];
    size_t length = 0;
    while (length < raw.size() && raw[length]) ++length;
    return std::string(raw.data(), length);
}

bool GAME_LoadIndex(const std::filesystem::path& save_root, SaveIndex& index,
                    std::string& error) {
    error.clear();
    index = {};
    index.file_number.fill(-1); // Set before the open, as retail does.
    std::ifstream file(game_directory(save_root) / "game.dat", std::ios::binary);
    if (!file) {
        error = "no save index; all ten slots are empty";
        return false;
    }
    std::array<uint8_t, SaveIndex::file_size> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    const size_t read = static_cast<size_t>(file.gcount());
    if (read < bytes.size())
        error = "save index is " + std::to_string(read) +
                " bytes; the missing tail reads as zero";
    for (size_t slot = 0; slot < SaveIndex::slot_count; ++slot) {
        std::memcpy(index.names[slot].data(), bytes.data() + slot * SaveIndex::name_size,
                    SaveIndex::name_size);
        index.status[slot] = le32(bytes.data() + 0x0dc + slot * 4);
        index.recency[slot] = le32(bytes.data() + 0x104 + slot * 4);
        // A short read never reaches the file-number array: keep -1 there.
        if (read >= 0x12c + (slot + 1) * 4)
            index.file_number[slot] = le32(bytes.data() + 0x12c + slot * 4);
    }
    return true;
}

bool GAME_LoadSaveIcon(const std::filesystem::path& save_root,
                       const SaveIndex& index, const char* name, SaveIcon& icon) {
    icon = {};
    if (!name) return false;
    for (size_t slot = 0; slot < SaveIndex::slot_count; ++slot) {
        if (std::strncmp(index.names[slot].data(), name, SaveIndex::name_size) != 0)
            continue;
        const std::string file = "game" + std::to_string(index.file_number[slot]) + ".ico";
        std::ifstream input(game_directory(save_root) / file, std::ios::binary);
        std::array<uint8_t, 0x2000> bytes{};
        if (!input) return false;
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes.size())) return false;
        for (size_t i = 0; i < icon.pixels.size(); ++i)
            icon.pixels[i] = static_cast<uint16_t>(bytes[i * 2] | (bytes[i * 2 + 1] << 8));
        icon.loaded = true;
        return true;
    }
    return false;
}

} // namespace od::port

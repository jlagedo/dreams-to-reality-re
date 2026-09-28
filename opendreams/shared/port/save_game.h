#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace od::port {

// The retail 340-byte data\game\game.dat index (GAME_LoadIndex/GAME_SaveIndex):
// +0x000 char[10][22] names, +0x0dc i32[10] status (non-zero = protected),
// +0x104 i32[10] recency (0 = empty, larger = newer), +0x12c i32[10] file
// number n of game<n>.dat/.ico. Globals 0x5dabf8, 0x5dab98, 0x5dabc0, 0x52eb70.
struct SaveIndex {
    static constexpr size_t slot_count = 10;
    static constexpr size_t name_size = 22;
    static constexpr size_t file_size = 340;
    std::array<std::array<char, name_size>, slot_count> names{};
    std::array<int32_t, slot_count> status{};
    std::array<int32_t, slot_count> recency{};
    std::array<int32_t, slot_count> file_number{};

    bool empty(size_t slot) const { return slot >= slot_count || names[slot][0] == 0; }
    std::string name(size_t slot) const;
};

// GAME_LoadSaveIcon's 0x2000-byte buffer (0x5d8b98): a 64x64 RGB565 copy of
// the renderer frame written by GAME_SaveThumbnail. Zero when unavailable.
struct SaveIcon {
    std::array<uint16_t, 64 * 64> pixels{};
    bool loaded = false;
};

// GAME_LoadIndex (0x40f3aa), run once from UI_InitIcons during GAME_Init.
// save_root replaces FILE_GetInstallRoot(); the file is save_root/data/game/
// game.dat. A missing file leaves ten empty slots and returns false, as the
// retail -1. Retail copies whatever a short read left; the port zero-fills the
// missing tail and reports it in `error` while keeping the bytes it read.
bool GAME_LoadIndex(const std::filesystem::path& save_root, SaveIndex& index,
                    std::string& error);

// GAME_LoadSaveIcon (0x40fc1f). The first slot whose name equals `name`
// selects game<file_number>.ico; an empty name therefore finds the first empty
// slot and file -1, whose open fails. Failure zero-fills the icon.
bool GAME_LoadSaveIcon(const std::filesystem::path& save_root,
                       const SaveIndex& index, const char* name, SaveIcon& icon);

} // namespace od::port

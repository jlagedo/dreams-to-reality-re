#pragma once

#include <array>
#include <cstdint>

namespace od::port {

// Visited-level snapshots at 0x5e2b08: eight 0x510-byte slots (32 entity
// snapshots of 0x28 bytes, then the level name char[16] at +0x500), the same
// 0x2880-byte block GAME_SaveGame/GAME_LoadGame write and read.
struct LevelStateTable {
    static constexpr size_t slot_size = 0x510;
    static constexpr size_t name_offset = 0x500;
    std::array<std::array<uint8_t, slot_size>, 8> slots{};
    uint32_t ring_index = 0;     // 0x49da84, walked as (index - k) & 7
    uint32_t skip_next_save = 0; // 0x49da88, set by the death restart only
};

// The player inventory at 0x61520c (0x3b8 bytes).
struct Inventory {
    std::array<std::array<char, 16>, 32> names{}; // +0x004, kept by resets
    uint32_t selected = 0;                        // +0x204
    std::array<int32_t, 3> hotkey_item{};         // +0x208
    std::array<float, 32> item_value{};           // +0x214
    std::array<int32_t, 32> bind_value{};         // +0x294
    std::array<int32_t, 32> item_word{};          // +0x314
    std::array<uint8_t, 32> item_flag{};          // +0x394
    int32_t count = 0;                            // +0x3b4
};

// Retail game-state globals reached before and during New Game. Initial
// values follow GAME_Init/GAME_InitSubsystems; roles marked unresolved keep
// their retail value without a consumer yet.
struct GameState {
    float health = 100.0f;           // 0x4fbab0, GAME_InitSubsystems 0x415ff6
    float magic = 20.0f;             // 0x4fbab4, 0x415fec
    int32_t unresolved_4fbaa8 = 0;   // 0x4fbaa8, read by SCENE_InitLevel
    Inventory inventory;
    std::array<int32_t, 3> hotkeys_a{{-1, -1, -1}}; // 0x626f14
    std::array<int32_t, 3> hotkeys_b{{-1, -1, -1}}; // 0x626f20
    std::array<int32_t, 3> hotkeys_c{};             // 0x626f08, UI_InitIcons 0
    std::array<int32_t, 2> hud_icon{{-1, -1}};      // 0x4a2e85 / 0x4a2e81
    // Flag words of four 0xa4-stride records (0x4a014c, 0x4a01f0, 0x4a0294,
    // 0x4a0338) written by ENT_ResetInventory; their owner is unresolved.
    std::array<uint32_t, 4> record_flags{};
    LevelStateTable levels;
    uint8_t elder_latch = 1;          // 0x49da28, static 1
    float transition = 0.0f;          // 0x5e5480
    uint8_t movie_no_decode = 0;      // 0x49d5b8
};

// ENT_ResetInventory (0x42a448). Keeps the owner and item names. The +0x204
// word is set to -1 and its low byte is then cleared by each item iteration,
// leaving 0xffffff00 as retail does.
void ENT_ResetInventory(GameState& state);

// SCENE_ClearLevelStates (0x41ae72): clears only each slot's first name byte
// and the ring index; stale snapshot bytes are unreachable afterwards.
void SCENE_ClearLevelStates(LevelStateTable& levels);

// BOOT_Run's New Game reset block (0x43673d-0x4367c9) around DDAT_Load:
// level states, inventory, hotkey sets 0x626f14/0x626f20, the HUD icon cache,
// the 15.0 transition value and the movie decode flag. Health, magic, the
// elder latch and 0x626f08 are not touched.
void BOOT_ResetNewGame(GameState& state);

} // namespace od::port

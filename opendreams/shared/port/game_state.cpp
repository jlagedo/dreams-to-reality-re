#include "port/game_state.h"

namespace od::port {

void ENT_ResetInventory(GameState& state) {
    Inventory& inventory = state.inventory;
    inventory.count = 0;                   // 0x42a478
    inventory.selected = 0xffffffffu;      // 0x42a489
    inventory.hotkey_item.fill(-1);
    for (size_t item = 0; item < 32; ++item) {
        inventory.selected &= 0xffffff00u; // 0x42a4d5, once per item
        inventory.item_value[item] = 50.0f;
        inventory.bind_value[item] = 0;
        inventory.item_word[item] = 0;
        inventory.item_flag[item] = 0;
    }
    state.record_flags = {{0x10200u, 0x20100u, 0x20100u, 0x20100u}}; // 0x42a54b-0x42a569
}

void SCENE_ClearLevelStates(LevelStateTable& levels) {
    for (auto& slot : levels.slots) slot[LevelStateTable::name_offset] = 0; // 0x41ae9e
    levels.ring_index = 0;                                                  // 0x41aead
}

void BOOT_ResetNewGame(GameState& state) {
    SCENE_ClearLevelStates(state.levels);
    ENT_ResetInventory(state);
    state.hotkeys_a.fill(-1);   // 0x43675c-0x43678f
    state.hotkeys_b.fill(-1);
    state.hud_icon = {{-1, -1}}; // 0x436791-0x4367a0
    state.transition = 15.0f;   // 0x4367b5; never counted down on this path
    state.movie_no_decode = 0;  // 0x4367c9
}

} // namespace od::port

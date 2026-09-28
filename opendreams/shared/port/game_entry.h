#pragma once

#include <string>
#include <string_view>

namespace od::port {

enum class GameEntryPhase {
    inactive, menu_hold, boot_return, project_movie, elder_movie, load_level,
    running
};

enum class GameEntryEvent {
    none, stop_menu, start_project_movie, start_elder_movie, load_level
};

struct GameEntryState {
    GameEntryPhase phase = GameEntryPhase::inactive;
    double hold_seconds = 0.0;
    // 0x5e5480: BOOT_Run arms 15.0, but DDAT_Load has already set the loading
    // state (0x661e08 = 1), so GAME_Tick never reaches the countdown and
    // SCENE_InitLevel clears it. Kept for fidelity; nothing consumes it here.
    float transition_frames = 0.0f;
    std::string project_movie;
    bool project_movie_available = false;
    bool elder_latch = true;
};

// The reached New Game branch of BOOT_Run. The caller obtains the video name
// from the active DREAMS.DAT record and checks the mounted source.
void BOOT_BeginNewGame(GameEntryState& state, std::string_view project_movie,
                       bool project_movie_available);

// Entry-only slice of BOOT_Run's return and GAME_Tick's loading branch.
// After the menu hold the movie closes and both pages clear to black; the
// optional project movie (record +0x3c) plays next, then the first loading
// tick starts the hardcoded elder movie while latch 0x49da28 is set. The level
// loads on the tick after that movie ends, with no fade (hard cut).
GameEntryEvent GAME_TickEntry(GameEntryState& state, double elapsed_seconds,
                              bool movie_ended);
void GAME_EntryLoaded(GameEntryState& state);

} // namespace od::port

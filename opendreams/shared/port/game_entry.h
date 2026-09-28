#pragma once

#include <string>
#include <string_view>

namespace od::port {

enum class GameEntryPhase {
    inactive, menu_hold, project_movie, transition, elder_movie, load_level,
    running
};

enum class GameEntryEvent {
    none, stop_menu, start_project_movie, start_elder_movie, load_level
};

struct GameEntryState {
    GameEntryPhase phase = GameEntryPhase::inactive;
    double hold_seconds = 0.0;
    float transition_frames = 0.0f;
    std::string project_movie;
    bool project_movie_available = false;
    bool elder_latch = true;
};

// The reached New Game branch of BOOT_Run. The caller obtains the video name
// from the active DREAMS.DAT record and checks the mounted source.
void BOOT_BeginNewGame(GameEntryState& state, std::string_view project_movie,
                       bool project_movie_available);

// Entry-only slice of GAME_Tick. elapsed_seconds is the runtime frame interval;
// the original game advances its transition by 30 engine frames per second.
GameEntryEvent GAME_TickEntry(GameEntryState& state, double elapsed_seconds,
                              bool movie_ended);
void GAME_EntryLoaded(GameEntryState& state);

} // namespace od::port

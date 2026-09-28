#include "port/game_entry.h"

#include <algorithm>

namespace od::port {
namespace {

// GAME_Tick loading branch (0x4240e2): the latch sends 0x1f, opens
// TETE_E~1.HNM and returns; without it the level loads on this tick.
GameEntryEvent start_elder_or_load(GameEntryState& state) {
    if (state.elder_latch) {
        state.elder_latch = false;
        state.phase = GameEntryPhase::elder_movie;
        return GameEntryEvent::start_elder_movie;
    }
    state.phase = GameEntryPhase::load_level;
    return GameEntryEvent::load_level;
}

} // namespace

void BOOT_BeginNewGame(GameEntryState& state, std::string_view project_movie,
                       bool project_movie_available) {
    state = {};
    state.phase = GameEntryPhase::menu_hold;
    state.project_movie = project_movie;
    state.project_movie_available = project_movie_available &&
                                    !project_movie.empty();
    state.transition_frames = 15.0f; // BOOT_Run writes 0x41700000.
}

GameEntryEvent GAME_TickEntry(GameEntryState& state, double elapsed_seconds,
                              bool movie_ended) {
    const double elapsed = std::max(0.0, elapsed_seconds);
    switch (state.phase) {
    case GameEntryPhase::menu_hold:
        // BOOT_TickFrame waits for three steps of 0x28 200 Hz timer ticks.
        state.hold_seconds += elapsed;
        if (state.hold_seconds < 3.0 * 41.0 / 200.0)
            return GameEntryEvent::none;
        // MGM 0x19 closes the movie; both pages are cleared to black.
        state.phase = GameEntryPhase::boot_return;
        return GameEntryEvent::stop_menu;
    case GameEntryPhase::boot_return:
        if (state.project_movie_available) {
            state.phase = GameEntryPhase::project_movie;
            return GameEntryEvent::start_project_movie;
        }
        return start_elder_or_load(state);
    case GameEntryPhase::project_movie:
        if (!movie_ended) return GameEntryEvent::none;
        return start_elder_or_load(state);
    case GameEntryPhase::elder_movie:
        if (!movie_ended) return GameEntryEvent::none;
        state.phase = GameEntryPhase::load_level;
        return GameEntryEvent::load_level;
    default:
        return GameEntryEvent::none;
    }
}

void GAME_EntryLoaded(GameEntryState& state) {
    if (state.phase == GameEntryPhase::load_level)
        state.phase = GameEntryPhase::running;
}

} // namespace od::port

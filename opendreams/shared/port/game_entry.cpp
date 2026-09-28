#include "port/game_entry.h"

#include <algorithm>

namespace od::port {

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
        state.phase = state.project_movie_available ?
            GameEntryPhase::project_movie : GameEntryPhase::transition;
        return state.project_movie_available ? GameEntryEvent::start_project_movie
                                             : GameEntryEvent::stop_menu;
    case GameEntryPhase::project_movie:
        if (movie_ended) state.phase = GameEntryPhase::transition;
        return GameEntryEvent::none;
    case GameEntryPhase::transition:
        // GAME_TickFrame computes 30 / estimated FPS and clamps to 0.2..5.
        state.transition_frames -= std::clamp(
            static_cast<float>(elapsed * 30.0), 0.2f, 5.0f);
        if (state.transition_frames > 0.0001f) return GameEntryEvent::none;
        state.transition_frames = 0.0f;
        if (state.elder_latch) {
            state.elder_latch = false;
            state.phase = GameEntryPhase::elder_movie;
            return GameEntryEvent::start_elder_movie;
        }
        state.phase = GameEntryPhase::load_level;
        return GameEntryEvent::load_level;
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

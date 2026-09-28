#include "render/movie_clock.h"

#include <iostream>

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "line " << __LINE__ << ": " #expr "\n"; return 1; } } while (0)

int main() {
    using od::MovieClock;
    constexpr uint64_t spf = MovieClock::samples_per_frame;
    constexpr uint64_t ns = MovieClock::frame_ns;
    CHECK(spf == 1470);

    // Sound movie: frames follow played samples, not host time.
    MovieClock clock;
    clock.start(1000, 0, 0);
    CHECK(clock.due(1000, true, 0) == 0);
    CHECK(clock.due(1000 + 50 * ns, true, spf - 1) == 0); // host ran ahead
    CHECK(clock.due(1000, true, spf) == 1);
    CHECK(clock.due(2000, true, 10 * spf + 5) == 10);

    // Audio drained (queue empty): continue at 15 Hz from the last audio frame.
    CHECK(clock.due(2000 + 3 * ns, false, 10 * spf + 5) == 13);
    // Returning audio never moves presentation backwards.
    CHECK(clock.due(2000 + 3 * ns, true, 11 * spf) == 13);

    // Resume after a pause: progress is relative to the device count then.
    clock.start(5000, 40, 77 * spf);
    CHECK(clock.due(5000, true, 77 * spf) == 40);
    CHECK(clock.due(5000, true, 79 * spf) == 42);

    // Silent movie: accumulated host time, independent of display cadence.
    MovieClock silent;
    silent.start(0, 0, 0);
    CHECK(silent.due(ns - 1, false, 0) == 0);
    CHECK(silent.due(ns, false, 0) == 1);
    CHECK(silent.due(15 * ns, false, 0) == 15);
    std::cout << "movie clock checks passed\n";
    return 0;
}

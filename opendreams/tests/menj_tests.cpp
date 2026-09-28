#include "port/menj.h"

#include <iostream>

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "line " << __LINE__ << ": " #expr "\n"; return 1; } } while (0)

int main() {
    using od::port::MENJ_VoiceCaptionsAt;
    // Three lines of 10, 6 and 4 ticks; the entry lasts 20 ticks.
    const std::vector<uint32_t> durations{10, 6, 4};
    auto frame = MENJ_VoiceCaptionsAt(durations, 20, 0);
    CHECK(frame.lines.size() == 1 && frame.lines[0].line == 0);
    CHECK(frame.lines[0].fade == 1 && frame.lines[0].coverage == 64);
    CHECK(frame.lines[0].x == 150 && frame.lines[0].y == 90);
    // Line 1 becomes current after tick 10; its fade 20 is still hidden when
    // printed on the next change, then halves to 10, 5, 2 and 1.
    frame = MENJ_VoiceCaptionsAt(durations, 20, 11);
    CHECK(frame.lines.size() == 1);
    frame = MENJ_VoiceCaptionsAt(durations, 20, 12);
    CHECK(frame.lines.size() == 2 && frame.lines[1].fade == 10);
    CHECK(frame.lines[1].coverage == 6 && frame.lines[1].y == 110);
    CHECK(MENJ_VoiceCaptionsAt(durations, 20, 13).lines[1].fade == 5);
    CHECK(MENJ_VoiceCaptionsAt(durations, 20, 14).lines[1].fade == 2);
    CHECK(MENJ_VoiceCaptionsAt(durations, 20, 14).lines[1].coverage == 32);
    CHECK(MENJ_VoiceCaptionsAt(durations, 20, 15).lines[1].fade == 1);
    // The counter reaching the entry duration finishes the captions.
    frame = MENJ_VoiceCaptionsAt(durations, 20, 25);
    CHECK(frame.finished && frame.lines.size() == 3);
    // A fifth line starts a new page of four.
    const std::vector<uint32_t> five{2, 2, 2, 2, 2};
    frame = MENJ_VoiceCaptionsAt(five, 10, 10);
    CHECK(frame.lines.size() == 1 && frame.lines[0].line == 4 && frame.lines[0].y == 90);
    // Low-resolution divisors.
    frame = MENJ_VoiceCaptionsAt(durations, 20, 0, 2, 2);
    CHECK(frame.lines[0].x == 75 && frame.lines[0].y == 45);
    std::cout << "voice caption checks passed\n";
    return 0;
}

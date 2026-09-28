#include "port/menj.h"

#include <algorithm>
#include <array>

namespace od::port {

VoiceCaptionFrame MENJ_VoiceCaptionsAt(const std::vector<uint32_t>& line_durations,
                                       uint32_t entry_duration, uint32_t tick,
                                       int scale_x, int scale_y) {
    constexpr size_t slots = 64;
    if (scale_x < 1) scale_x = 1;
    if (scale_y < 1) scale_y = 1;
    const size_t count = std::min(line_durations.size(), slots);
    std::array<int, slots> fade{};
    std::array<uint32_t, slots> end{};
    // Every slot first takes the entry duration and fade 20; line 0 starts
    // opaque and later lines end at the running sum of line durations.
    fade.fill(20);
    end.fill(entry_duration);
    if (count) {
        end[0] = line_durations[0];
        fade[0] = 1;
        for (size_t i = 1; i < count; ++i) end[i] = end[i - 1] + line_durations[i];
    }
    VoiceCaptionFrame frame;
    size_t current = 0, page = 0;
    const int row_height = 20 / scale_y;
    // Each counter change prints the page with the fades it holds, then
    // updates the current line; the entry finishes once the counter reaches
    // its duration.
    for (uint32_t step = 0; ; ++step) {
        frame.lines.clear();
        for (size_t line = page; line < page + 4 && line < slots; ++line) {
            if (fade[line] == 20 || line >= count) continue;
            VoiceCaptionPrint print;
            print.line = line;
            print.x = 150 / scale_x;
            print.y = 90 / scale_y + static_cast<int>(line % 4) * row_height;
            print.fade = fade[line];
            print.coverage = 0x40 / fade[line];
            frame.lines.push_back(print);
        }
        if (step == tick) break;
        if (current < slots && step < end[current]) {
            fade[current] = std::max(1, fade[current] / 2);
        } else if (current < slots) {
            fade[current] = 1;
            ++current;
            page = current / 4 * 4;
        }
        if (step >= entry_duration) {
            frame.finished = true;
            break;
        }
    }
    return frame;
}

} // namespace od::port

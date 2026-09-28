#pragma once

#include <cstdint>
#include <vector>

namespace od::port {

// Timed voice captions of MENJ_PlayVoiceCaptions (0x436ab6). Retail keeps 64
// 0x18-byte slots at 0x626810 (x, y, fade, -, end tick, text index); a line's
// fade starts at 20 (hidden), and the current line halves it once per change
// of the 15 Hz caption counter until it reaches 1 (opaque). Lines show in
// pages of four at x = 150/sx, y = 90/sy + (i % 4) * (20/sy).
struct VoiceCaptionPrint {
    size_t line = 0;
    int x = 0;
    int y = 0;
    int fade = 20;
    // TEXT_BlitGlyphFaded coverage 0x40 / fade; 63 or more is an opaque copy,
    // otherwise SPR_BlendPixel mixes (coverage >> 1) / 32 of the glyph.
    int coverage = 0;
};

struct VoiceCaptionFrame {
    std::vector<VoiceCaptionPrint> lines;
    bool finished = false; // The counter reached DRD_GetEntryDuration.
};

// The caption lines TEXT_PrintFaded draws at counter value `tick`, replaying
// the retail per-change steps from the start. line_durations are
// DRD_GetLineDuration values (15 Hz ticks); entry_duration is
// DRD_GetEntryDuration. At most 64 lines are used, as the retail table.
VoiceCaptionFrame MENJ_VoiceCaptionsAt(const std::vector<uint32_t>& line_durations,
                                       uint32_t entry_duration, uint32_t tick,
                                       int scale_x = 1, int scale_y = 1);

} // namespace od::port

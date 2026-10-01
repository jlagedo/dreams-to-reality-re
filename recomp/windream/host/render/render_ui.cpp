#include "render_ui.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace wd {
namespace {
uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
int32_t i32(const uint8_t *p) {
    uint32_t v = u32(p);
    int32_t s;
    std::memcpy(&s, &v, 4);
    return s;
}
struct Inputs {
    SceneReader reader;
    UiBatch &batch;
    std::string &error;
    bool read(uint32_t a, void *p, size_t n) {
        if (uint64_t(a) + n > 0x100000000ull || !reader.read ||
            !reader.read(reader.context, a, p, n)) {
            char message[100];
            std::snprintf(message, sizeof message, "unmapped UI source %08x + %zu", a, n);
            error = message;
            return false;
        }
        return true;
    }
    bool word(uint32_t a, uint32_t &v) {
        uint8_t b[4];
        if (!read(a, b, 4))
            return false;
        v = u32(b);
        return true;
    }
    void put(uint32_t a, uint32_t v, uint32_t bytes = 4) {
        batch.metadata.push_back({a, v, bytes});
    }
};
struct Clip {
    int x = 0, y = 0, w = 0, h = 0, source_w = 0, source_h = 0, screen_w = 0, screen_h = 0;
    int lead = 0, top = 0, skip = 0, right = 0, bottom = 0, sx = 1, sy = 1;
};
bool controls(Inputs &in, Clip &c) {
    uint32_t width, height, format;
    if (!in.word(0x49d9fc, width) || !in.word(0x49da00, height) || !in.word(0x49da1c, format) ||
        !in.word(0x5e549c, in.batch.target_address))
        return false;
    if (!width || !height || width > 16384 || height > 16384 || format > 1) {
        in.error = "invalid UI target layout";
        return false;
    }
    c.screen_w = int(width);
    c.screen_h = int(height);
    in.batch.format = od_pixel_format(format);
    return true;
}
bool clip(Inputs &in, Clip &c) {
    if (c.source_w < 0 || c.source_h < 0 || c.source_w > 16384 || c.source_h > 16384 ||
        c.x < -16777216 || c.x > 16777216 || c.y < -16777216 || c.y > 16777216) {
        in.error = "unsupported UI source dimensions/coordinates";
        return false;
    }
    c.right = c.x + c.source_w / c.sx;
    c.bottom = c.y + c.source_h / c.sy;
    if (c.x >= c.screen_w || c.y >= c.screen_h || c.right < 0 || c.bottom < 0) {
        c.w = c.h = 0;
        return true;
    }
    c.lead = std::max(0, -c.x);
    c.top = std::max(0, -c.y);
    c.w = c.source_w / c.sx;
    c.h = c.source_h / c.sy;
    c.skip = c.sy == 2 ? c.source_w : 0;
    if (c.x < 0)
        c.w += c.x;
    int destination_skip = (c.screen_w - c.right) * 2;
    if (c.right >= c.screen_w) {
        destination_skip = 0;
        c.skip = c.right - c.screen_w;
        c.w -= c.skip;
    }
    if (c.y < 0)
        c.h += c.y;
    if (c.bottom >= c.screen_h)
        c.h -= c.bottom - c.screen_h;
    in.put(0x49d0e8, uint32_t(c.top));
    in.put(0x49d0ec, uint32_t(std::max(0, c.y)));
    in.put(0x49d0f0, uint32_t(c.lead));
    in.put(0x49d0f4, uint32_t(std::max(0, c.x) * 2));
    in.put(0x49d0f8, uint32_t(c.skip));
    in.put(0x49d0fc, uint32_t(destination_skip));
    in.put(0x49d100, uint32_t(c.w));
    in.put(0x49d104, uint32_t(c.h));
    return true;
}
bool source_bytes(Inputs &in, uint32_t base, uint64_t start, uint64_t count,
                  std::vector<uint8_t> &data) {
    if (count > 64 * 1024 * 1024 || uint64_t(base) + start + count > 0x100000000ull) {
        in.error = "UI source range overflow";
        return false;
    }
    data.resize(size_t(count));
    return in.read(uint32_t(base + start), data.data(), data.size());
}
bool palette(Inputs &in, uint32_t address, std::array<uint16_t, 256> &colours) {
    uint8_t data[512];
    if (!in.read(address, data, sizeof data))
        return false;
    for (size_t n = 0; n < 256; ++n)
        colours[n] = uint16_t(data[n * 2] | uint16_t(data[n * 2 + 1]) << 8);
    return true;
}
} // namespace

static bool flag4(Inputs &in, Clip c, const uint8_t *request, const uint8_t *desc) {
    const int pitch = c.screen_w, height = c.screen_h;
    c.screen_w = 500;
    c.screen_h = 450;
    c.x = int32_t(uint32_t(i32(request)) - uint32_t(i32(desc + 12)));
    c.y = int32_t(uint32_t(i32(request + 4)) - uint32_t(i32(desc + 16)));
    c.source_w = i32(desc + 4);
    c.source_h = i32(desc + 8);
    if (!clip(in, c))
        return false;
    if (c.w <= 0 || c.h <= 0)
        return true;
    std::vector<uint8_t> pixels;
    std::array<uint16_t, 256> colours;
    const uint64_t stride = uint64_t(c.lead) + c.w + c.skip;
    const uint64_t count = uint64_t(c.h - 1) * stride + c.lead + c.w;
    if (!source_bytes(in, u32(desc + 24), uint64_t(c.top) * c.source_w, count, pixels) ||
        !palette(in, u32(desc), colours))
        return false;
    // Retail clips to 500x450 but adds (videoWidth+500)*2 after its
    // ordinary 500-pixel row advance: net stride videoWidth+1000 pixels.
    std::vector<std::pair<size_t, uint32_t>> stores;
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x) {
            const uint8_t index = pixels[size_t(y) * stride + c.lead + x];
            if (!index)
                continue;
            const uint64_t at = uint64_t(std::max(0, c.y)) * pitch + std::max(0, c.x) +
                                uint64_t(y) * (pitch + 1000) + x;
            if (at >= uint64_t(pitch) * height) {
                in.error = "flag 4 sprite writes outside target allocation";
                return false;
            }
            stores.push_back({size_t(at), uint32_t(colours[index]) | (64u << 16)});
        }
    if (stores.empty())
        return true;
    const size_t top = stores.front().first / pitch, bottom = stores.back().first / pitch;
    in.batch.kind = OD_DRAW_RAW;
    in.batch.rect = {0, int(top), pitch, int(bottom - top + 1)};
    in.batch.pixels.assign(size_t(pitch) * (bottom - top + 1), 0);
    for (auto store : stores)
        in.batch.pixels[store.first - top * pitch] = store.second;
    return true;
}

bool normalize_sprite(SceneReader reader, const UiRegisters &regs, bool faded, UiBatch &output,
                      std::string &error) {
    error.clear();
    UiBatch batch;
    Inputs in{reader, batch, error};
    Clip c;
    uint8_t request[16], desc[28];
    if (!controls(in, c) || !in.read(regs.esi, request, sizeof request) ||
        !in.read(u32(request + 12), desc, sizeof desc))
        return false;
    const uint32_t flags = u32(request + 8), factor = regs.eax & 255;
    if (!faded && (flags & 4)) {
        if (!flag4(in, c, request, desc))
            return false;
        output = std::move(batch);
        return true;
    }
    const bool paired = !faded && (flags & (16 | 8));
    const bool half = !faded && !paired && (flags & 2);
    const bool opaque = !faded && !paired && !half && (flags & 1);
    const bool neighbour = !faded && !paired && !half && !opaque;
    c.sx = (regs.ebx & 255) ? 2 : 1;
    c.sy = ((regs.ebx >> 8) & 255) ? 2 : 1;
    c.x = int32_t(uint32_t(i32(request)) - uint32_t(i32(desc + 12)));
    c.y = int32_t(uint32_t(i32(request + 4)) - uint32_t(i32(desc + 16)));
    c.source_w = i32(desc + 4);
    c.source_h = i32(desc + 8);
    in.put(0x49d11a, 0xf7df);
    in.put(0x49d10e, factor, 1);
    in.put(0x49d10f, regs.ebx & 255, 1);
    in.put(0x49d110, (regs.ebx >> 8) & 255, 1);
    in.put(0x49d116, uint32_t(c.sx * (faded ? 1 : 2)));
    if (!faded) {
        in.put(0x49d111, regs.ecx & 255, 1);
        in.put(0x49d112, uint32_t(c.sx));
        in.put(0x49d11e, (flags & 2) != 0);
        in.put(0x49d122, (flags & 1) != 0);
        in.put(0x49d126, (flags & 16) != 0);
        in.put(0x49d12a, (flags & 8) != 0);
    }
    if (!clip(in, c))
        return false;
    if (c.w <= 0 || c.h <= 0) {
        output = std::move(batch);
        return true;
    }
    const uint64_t start = uint64_t(c.top) * c.source_w * (!faded && (flags & 16) ? 2 : 1);
    const int bpp = paired ? 2 : 1, step = c.sx * bpp;
    if (paired || faded) {
        c.lead *= 2;
        c.skip *= 2;
        in.put(0x49d0f0, uint32_t(c.lead));
        in.put(0x49d0f8, uint32_t(c.skip));
    }
    const uint64_t row_step = uint64_t(c.lead) + uint64_t(c.w) * step + c.skip;
    const uint64_t needed = uint64_t(c.h - 1) * row_step + c.lead + uint64_t(c.w - 1) * step + bpp;
    std::vector<uint8_t> pixels;
    std::array<uint16_t, 256> colours;
    if (!source_bytes(in, u32(desc + 24), start, needed, pixels) ||
        !palette(in, u32(desc), colours))
        return false;
    const int span = c.w + (neighbour ? 1 : 0);
    batch.rect = {std::max(0, c.x), std::max(0, c.y), span, c.h};
    batch.pixels.assign(size_t(span) * c.h, 0);
    batch.kind = half ? OD_DRAW_HALF : OD_DRAW_BLEND;
    batch.parameter = faded ? 0 : (regs.ecx & 255);
    bool divided = paired && !(flags & 16);
    uint32_t last_alpha = 0;
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x) {
            const size_t offset = size_t(y * row_step + c.lead + uint64_t(x) * step);
            const uint8_t index = pixels[offset];
            if (!index && !opaque)
                continue;
            uint32_t alpha = 64;
            if (faded || divided) {
                if (!factor) {
                    error = "zero sprite divisor";
                    return false;
                }
                alpha = (faded ? 64 : pixels[offset + 1]) / factor;
            } else if (paired)
                alpha = pixels[offset + 1];
            if (alpha >= 128) {
                if (batch.lookup.empty()) {
                    std::vector<uint8_t> table;
                    if (!source_bytes(in, 0x5e64bc, 0, 7168 * 4, table))
                        return false;
                    batch.lookup.resize(7168);
                    for (size_t i = 0; i < batch.lookup.size(); ++i)
                        batch.lookup[i] = u32(table.data() + i * 4);
                    batch.kind = OD_DRAW_LOOKUP;
                }
            }
            // The retail divided-alpha loop jumps into the direct-alpha loop
            // after its first opaque quotient and stays there across row changes.
            if (divided && alpha >= 63 && alpha < 128)
                divided = false;
            if (alpha && (alpha < 63 || alpha >= 128))
                last_alpha = alpha;
            batch.pixels[size_t(y) * span + x] = colours[index] | (alpha << 16);
            if (neighbour)
                batch.pixels[size_t(y) * span + x + 1] =
                    ((batch.parameter || batch.format == OD_RGB555) ? 0u : 0x19e7u) | (64u << 16);
        }
    if (last_alpha)
        in.put(0x5ecdfc, last_alpha, 1);
    if (batch.rect.x + span > c.screen_w) {
        std::vector<uint32_t> expanded(size_t(c.screen_w) * (c.h + 1));
        for (int y = 0; y < c.h; ++y)
            for (int x = 0; x < span; ++x)
                if (batch.pixels[size_t(y) * span + x])
                    expanded[size_t(y) * c.screen_w + batch.rect.x + x] =
                        batch.pixels[size_t(y) * span + x];
        batch.rect.x = 0;
        batch.rect.width = c.screen_w;
        batch.rect.height = c.h + 1;
        batch.pixels = std::move(expanded);
        if (batch.rect.y + batch.rect.height > c.screen_h) {
            error = "sprite neighbour writes past the target allocation";
            return false;
        }
    }
    output = std::move(batch);
    return true;
}

bool normalize_gauge(SceneReader reader, const UiRegisters &regs, UiBatch &output,
                     std::string &error) {
    error.clear();
    UiBatch batch;
    Inputs in{reader, batch, error};
    Clip c;
    uint8_t request[24], desc[28];
    if (!controls(in, c) || !in.read(regs.esi, request, sizeof request) ||
        !in.read(u32(request + 8), desc, sizeof desc))
        return false;
    c.x = i32(request);
    c.y = i32(request + 4);
    c.source_w = i32(desc + 4);
    c.source_h = i32(desc + 8);
    c.sx = (regs.edi & 255) ? 2 : 1;
    c.sy = ((regs.edi >> 8) & 255) ? 2 : 1;
    const uint32_t mode = (regs.edi >> 16) & 255;
    const uint32_t thresholds[] = {regs.eax >> (c.sy == 2), regs.ebx >> (c.sy == 2),
                                   regs.ecx >> (c.sy == 2)};
    const int inset = c.sy == 2 ? 10 : 20, third_inset = c.sy == 2 ? 2 : 5;
    in.put(0x49d10f, regs.edi & 255, 1);
    in.put(0x49d110, (regs.edi >> 8) & 255, 1);
    in.put(0x49d162, mode, 1);
    in.put(0x49d15a, uint32_t(inset));
    in.put(0x49d15e, uint32_t(third_inset));
    for (int i = 0; i < 3; ++i)
        in.put(0x49d13a + 4 * i, thresholds[i]);
    in.put(0x49d14a, regs.esi);
    in.put(0x49d14e, regs.edx);
    in.put(0x49d116, uint32_t(2 * c.sx));
    if (!clip(in, c))
        return false;
    if (c.w <= 0 || c.h <= 0) {
        output = std::move(batch);
        return true;
    }
    uint32_t inherited;
    if (!in.word(0x49d126, inherited))
        return false;
    uint64_t start = uint64_t(c.top) * c.source_w * (inherited == 1 ? 2 : 1);
    const int step = 2 * c.sx;
    c.lead *= 2;
    c.skip *= 2;
    in.put(0x49d0f0, uint32_t(c.lead));
    in.put(0x49d0f8, uint32_t(c.skip));
    const uint64_t row_step = uint64_t(c.lead) + uint64_t(c.w) * step + c.skip;
    const uint64_t needed = uint64_t(c.h - 1) * row_step + c.lead + uint64_t(c.w - 1) * step + 2;
    std::vector<uint8_t> source;
    std::array<uint16_t, 256> colours;
    if (!source_bytes(in, u32(desc + 24), start, needed, source) ||
        !palette(in, u32(desc), colours))
        return false;
    std::vector<uint8_t> layers[3];
    auto layer = [&](int which) -> bool {
        if (!layers[which].empty())
            return true;
        uint32_t pixels;
        if (!in.word(u32(request + 12 + which * 4) + 24, pixels))
            return false;
        return source_bytes(in, pixels, 0,
                            uint64_t(c.h - 1) * c.sy * 128 + uint64_t(c.w - 1) * step + 2,
                            layers[which]);
    };
    batch.kind = OD_DRAW_BLEND;
    batch.rect = {std::max(0, c.x), std::max(0, c.y), c.w, c.h};
    batch.pixels.assign(size_t(c.w) * c.h, 0);
    uint32_t last_alpha = 0;
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x) {
            const size_t offset = size_t(y * row_step + c.lead + uint64_t(x) * step);
            uint32_t index = source[offset], alpha = source[offset + 1], colour = colours[index];
            if (!index)
                continue;
            if (alpha >= 0xfd) {
                const int which = int(0xff - alpha), remaining = c.h - y;
                const int threshold = remaining - (which == 2 ? third_inset : inset);
                if (which == 2 && regs.edx) {
                    if (mode == 1)
                        continue;
                    colour = int32_t(thresholds[which]) < threshold ? 0xe862 : 0x0bfd;
                    alpha = 64;
                } else {
                    if (int32_t(thresholds[which]) < threshold)
                        continue;
                    if (!layer(which))
                        return false;
                    const size_t at = size_t(y * c.sy) * 128 + size_t(x) * step;
                    index = layers[which][at];
                    if (!index)
                        continue;
                    colour = colours[index];
                    alpha = layers[which][at + 1];
                }
            }
            if (alpha && alpha < 63)
                last_alpha = alpha;
            if (alpha >= 63)
                alpha = 64;
            batch.pixels[size_t(y) * c.w + x] = colour | (alpha << 16);
        }
    in.put(0x49d152, uint32_t(c.h));
    in.put(0x49d156, uint32_t(c.w * step));
    if (last_alpha)
        in.put(0x5ecdfc, last_alpha, 1);
    output = std::move(batch);
    return true;
}
bool normalize_masked64(SceneReader reader, const UiRegisters &regs, UiBatch &output,
                        std::string &error) {
    error.clear();
    UiBatch batch;
    Inputs in{reader, batch, error};
    Clip c;
    uint8_t source[8192], mask[512];
    if (!controls(in, c) || !in.read(regs.eax, source, sizeof source) ||
        !in.read(0x49fd1a, mask, sizeof mask))
        return false;
    const int sx = c.screen_w < 401 ? 2 : 1, sy = c.screen_h < 400 ? 2 : 1, w = 64 / sx,
              h = 64 / sy;
    const int x = int32_t(regs.edx), y = int32_t(regs.ebx);
    const int64_t start = int64_t(y) * c.screen_w + x;
    const int64_t end = start + int64_t(h - 1) * c.screen_w + w;
    if (start < 0 || end > int64_t(c.screen_w) * c.screen_h) {
        error = "masked image writes outside target allocation";
        return false;
    }
    const bool wrap = x < 0 || x + w > c.screen_w;
    const int top = wrap ? int(start / c.screen_w) : y;
    const int left = wrap ? 0 : x;
    const int width = wrap ? c.screen_w : w;
    const int height = wrap ? int((end + c.screen_w - 1) / c.screen_w) - top : h;
    batch.kind = OD_DRAW_RAW;
    batch.rect = {left, top, width, height};
    batch.pixels.assign(size_t(width) * height, 0);
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col) {
            const int index = row * sy * 64 + col * sx;
            if (!(mask[index / 8] & (1u << (index % 8))))
                continue;
            const uint32_t colour = uint32_t(source[index * 2]) | uint32_t(source[index * 2 + 1])
                                                                      << 8;
            const size_t dest =
                wrap ? size_t(start + int64_t(row) * c.screen_w + col - int64_t(top) * c.screen_w)
                     : size_t(row) * w + col;
            batch.pixels[dest] = colour | (64u << 16);
        }
    output = std::move(batch);
    return true;
}
} // namespace wd

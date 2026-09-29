// C entry used by the independent x86 oracle harness, not the game runtime.
#include "render_ui.h"
#include <cstdio>

extern "C" int wd_normalize_ui_file(bool (*read)(void *, uint32_t, void *, size_t), void *context,
                                    const wd::UiRegisters *registers, uint32_t entry,
                                    const char *path, char *message, size_t capacity) {
    wd::UiBatch batch;
    std::string error;
    bool ok = registers &&
              (entry == 0x427b8c ? wd::normalize_masked64({context, read}, *registers, batch, error)
               : entry == 0x40368b ? wd::normalize_gauge({context, read}, *registers, batch, error)
                                   : wd::normalize_sprite({context, read}, *registers,
                                                          entry == 0x403bcd, batch, error));
    if (ok) {
        FILE *file = std::fopen(path, "wb");
        ok = file != nullptr;
        if (file) {
            auto word = [&](uint32_t value) {
                uint8_t b[4];
                for (int i = 0; i < 4; ++i)
                    b[i] = uint8_t(value >> (8 * i));
                ok = std::fwrite(b, 1, 4, file) == 4 && ok;
            };
            word(0x32495557);
            word(batch.target_address);
            word(batch.kind);
            word(batch.format);
            word(batch.parameter);
            word(uint32_t(batch.rect.x));
            word(uint32_t(batch.rect.y));
            word(uint32_t(batch.rect.width));
            word(uint32_t(batch.rect.height));
            word(uint32_t(batch.pixels.size()));
            word(uint32_t(batch.metadata.size()));
            for (auto value : batch.pixels)
                word(value);
            for (const auto &write : batch.metadata) {
                word(write.address);
                word(write.value);
                word(write.bytes);
            }
            word(uint32_t(batch.lookup.size()));
            for (auto value : batch.lookup)
                word(value);
            ok = std::fclose(file) == 0 && ok;
        }
        if (!ok)
            error = "cannot write native UI oracle fixture";
    }
    if (message && capacity)
        std::snprintf(message, capacity, "%s", error.c_str());
    return ok ? 1 : 0;
}

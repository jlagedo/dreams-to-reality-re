#pragma once

#include "port/vfs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

enum class SpriteErrorCode {
    none,
    missing_file,
    invalid_slot,
    invalid_table,
    source_error,
};

struct SpriteError {
    SpriteErrorCode code = SpriteErrorCode::none;
    std::string message;
    VfsError source;
    explicit operator bool() const { return code != SpriteErrorCode::none; }
};

enum class SpriteSlotStatus { empty, loaded, invalid };

struct SpriteDescriptor {
    std::array<uint8_t, 28> raw{};
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t field_0c = 0;
    int32_t field_10 = 0;
    int32_t field_14 = 0;
    uint32_t pixel_offset = 0;
    uint8_t bytes_per_pixel = 0; // Physical payload interpretation.
    size_t source_pixel_bytes = 0;
    size_t retail_read_bytes = 0;
    std::vector<uint8_t> retail_buffer;
    SpriteSlotStatus status = SpriteSlotStatus::empty;
    std::string note;
};

struct SpriteSet {
    std::string path;
    std::array<uint8_t, 512> raw_palette{};
    std::array<uint16_t, 256> palette{};
    std::vector<SpriteDescriptor> descriptors;
    uint64_t file_size = 0;
    uint64_t table_offset = 0;
    uint32_t footer_count = 0;
    uint8_t bytes_per_pixel = 0;
    size_t invalid_slots = 0;
};

struct FontState {
    bool active = false;
    uint32_t style = 0;
    size_t sprite_slot = 0;
    std::array<int32_t, 256> advances{};
};

struct PortraitSprite {
    uint32_t width = 0, height = 0;
    std::array<uint16_t, 256> palette_rgb555{};
    std::vector<uint8_t> indices;
    std::vector<uint8_t> coverage;
};

struct IconLookup {
    int table_index = -1;
    int bank = -1;
    int slot = -1;
};

struct IconNameMapping {
    std::string_view name;
    int bank = -1;
    int slot = -1;
};

// Retail's general sprite slots, five fixed icon banks and ten font slots,
// scoped to one selected VFS source. The VfsContext must outlive this state.
class SpriteState {
public:
    explicit SpriteState(VfsContext& vfs, bool display_16bit = false)
        : vfs_(&vfs), display_16bit_(display_16bit) {}
    const SpriteSet* set(size_t slot) const;
    const SpriteSet* icon_bank(size_t bank) const;
    const FontState* font(size_t slot) const;
    uint32_t mul32(size_t row, size_t column) const;
    uint32_t mul64(size_t row, size_t column) const;

private:
    VfsContext* vfs_ = nullptr;
    bool display_16bit_ = false;
    std::array<std::unique_ptr<SpriteSet>, 10> sets_;
    std::array<std::unique_ptr<SpriteSet>, 5> icon_banks_;
    std::array<FontState, 10> fonts_{};
    std::array<uint32_t, 32 * 32> mul32_{};
    std::array<uint32_t, 64 * 64> mul64_{};

    friend void SPR_InitMulTables(SpriteState&);
    friend bool SPR_LoadSet(SpriteState&, size_t, std::string_view, SpriteError&);
    friend const SpriteDescriptor* SPR_GetDescriptor(const SpriteState&, size_t, size_t);
    friend void SPR_FreeSet(SpriteState&, size_t);
    friend bool SPR_LoadIconBanks(SpriteState&, SpriteError&);
    friend void SPR_FreeIconBank(SpriteState&, size_t);
    friend void SPR_FreeIconBanks(SpriteState&);
    friend bool TEXT_LoadFont(SpriteState&, size_t, std::string_view, uint32_t,
                              SpriteError&);
    friend void TEXT_FreeFont(SpriteState&, size_t);
};

void SPR_InitMulTables(SpriteState& state);
bool SPR_LoadSet(SpriteState& state, size_t slot, std::string_view path,
                 SpriteError& error);
const SpriteDescriptor* SPR_GetDescriptor(const SpriteState& state,
                                           size_t slot, size_t index);
void SPR_FreeSet(SpriteState& state, size_t slot);
bool SPR_LoadIconBanks(SpriteState& state, SpriteError& error);
void SPR_FreeIconBank(SpriteState& state, size_t bank);
void SPR_FreeIconBanks(SpriteState& state);
IconLookup ICON_FindByName(std::string_view name);
// New read-only catalog view of the executable's 72-entry lookup table.
const std::array<IconNameMapping, 72>& ICON_NameTable();
bool TEXT_LoadFont(SpriteState& state, size_t slot, std::string_view path,
                   uint32_t style, SpriteError& error);
void TEXT_FreeFont(SpriteState& state, size_t slot);
bool SPR_LoadPortrait(const uint8_t* blob, size_t size,
                      PortraitSprite& portrait, SpriteError& error);

} // namespace od::port

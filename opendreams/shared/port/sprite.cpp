#include "port/sprite.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace od::port {
namespace {

bool fail(SpriteError& error, SpriteErrorCode code, std::string message) {
    error = {code, std::move(message), {}};
    return false;
}

bool source_fail(SpriteError& error, const VfsError& source) {
    error = {SpriteErrorCode::source_error, source.message, source};
    return false;
}

uint16_t little16(const uint8_t* value) {
    return static_cast<uint16_t>(value[0]) |
        static_cast<uint16_t>(value[1] << 8);
}

uint32_t little32(const uint8_t* value) {
    return static_cast<uint32_t>(value[0]) |
        (static_cast<uint32_t>(value[1]) << 8) |
        (static_cast<uint32_t>(value[2]) << 16) |
        (static_cast<uint32_t>(value[3]) << 24);
}

char upper_ascii(char value) {
    return value >= 'a' && value <= 'z' ? static_cast<char>(value - 'a' + 'A') : value;
}

bool ends_with_alp(std::string_view path) {
    if (path.size() < 4) return false;
    const auto end = path.substr(path.size() - 4);
    return upper_ascii(end[0]) == '.' && upper_ascii(end[1]) == 'A' &&
        upper_ascii(end[2]) == 'L' && upper_ascii(end[3]) == 'P';
}

bool read_exact_at(VfsContext& vfs, int32_t handle, uint64_t offset,
                   void* destination, size_t length, SpriteError& error) {
    VfsError source;
    uint64_t position = 0;
    if (!VFS_Seek(vfs, handle, static_cast<int64_t>(offset), 0, position, source))
        return source_fail(error, source);
    size_t count = 0;
    if (!VFS_Read(vfs, handle, destination, length, count, source))
        return source_fail(error, source);
    return count == length || fail(error, SpriteErrorCode::invalid_table,
                                   "sprite file has a truncated palette or table");
}

bool load_source(VfsContext& vfs, std::string_view path, bool icon_bank,
                 bool display_16bit, std::unique_ptr<SpriteSet>& result,
                 SpriteError& error) {
    error = {};
    result.reset();
    VfsError source;
    int32_t handle = 0;
    if (!VFS_Open(vfs, path, 0x200, handle, source)) {
        if (source.code == VfsErrorCode::missing_file)
            return fail(error, SpriteErrorCode::missing_file, source.message);
        return source_fail(error, source);
    }
    const auto close = [&]() {
        VfsError ignored;
        VFS_Close(vfs, handle, ignored);
    };
    uint64_t size = 0;
    if (!VFS_GetSize(vfs, handle, size, source)) {
        close();
        return source_fail(error, source);
    }
    if (size < 0x200 + 0x1c04 || size > 64 * 1024 * 1024) {
        close();
        return fail(error, SpriteErrorCode::invalid_table,
                    "sprite palette and descriptor table do not fit the file");
    }
    auto set = std::unique_ptr<SpriteSet>(new SpriteSet);
    set->path = std::string(path);
    set->file_size = size;
    set->table_offset = size - 0x1c04;
    set->bytes_per_pixel = ends_with_alp(path) ? 2 : 1;
    if (!read_exact_at(vfs, handle, 0, set->raw_palette.data(),
                       set->raw_palette.size(), error)) {
        close();
        return false;
    }
    for (size_t i = 0; i < set->palette.size(); ++i) {
        uint16_t color = little16(set->raw_palette.data() + i * 2);
        if (display_16bit)
            color = static_cast<uint16_t>(((color & 0x7fe0u) * 2u) | (color & 0x1fu));
        set->palette[i] = color;
    }
    std::array<uint8_t, 0x1c00> raw_table{};
    if (!read_exact_at(vfs, handle, set->table_offset,
                       raw_table.data(), raw_table.size(), error)) {
        close();
        return false;
    }
    std::array<uint8_t, 4> footer{};
    if (!read_exact_at(vfs, handle, size - 4, footer.data(), footer.size(), error)) {
        close();
        return false;
    }
    set->footer_count = little32(footer.data());
    if (set->footer_count > 256) {
        close();
        return fail(error, SpriteErrorCode::invalid_table,
                    "sprite footer count exceeds 256 descriptor slots");
    }
    set->descriptors.resize(256);
    for (size_t i = 0; i < set->descriptors.size(); ++i) {
        SpriteDescriptor& descriptor = set->descriptors[i];
        std::memcpy(descriptor.raw.data(), raw_table.data() + i * 28, 28);
        descriptor.width = little32(descriptor.raw.data() + 4);
        descriptor.height = little32(descriptor.raw.data() + 8);
        descriptor.field_0c = static_cast<int32_t>(little32(descriptor.raw.data() + 12));
        descriptor.field_10 = static_cast<int32_t>(little32(descriptor.raw.data() + 16));
        descriptor.field_14 = static_cast<int32_t>(little32(descriptor.raw.data() + 20));
        descriptor.pixel_offset = little32(descriptor.raw.data() + 24);
        descriptor.bytes_per_pixel = set->bytes_per_pixel;
        if (descriptor.pixel_offset == 0) continue;
        const uint64_t source_bytes = static_cast<uint64_t>(descriptor.width) *
            descriptor.height * set->bytes_per_pixel;
        if (descriptor.width == 0 || descriptor.height == 0 ||
            descriptor.width > 4096 || descriptor.height > 4096 ||
            descriptor.pixel_offset < 0x200 ||
            descriptor.pixel_offset >= set->table_offset ||
            source_bytes > set->table_offset - descriptor.pixel_offset) {
            descriptor.status = SpriteSlotStatus::invalid;
            descriptor.note = "descriptor dimensions or source pixel extent is invalid";
            ++set->invalid_slots;
            continue;
        }
        descriptor.source_pixel_bytes = static_cast<size_t>(source_bytes);
        const uint64_t retail_bytes = static_cast<uint64_t>(descriptor.width) *
            descriptor.height * (icon_bank ? 2u : set->bytes_per_pixel);
        if (retail_bytes > 32 * 1024 * 1024 ||
            retail_bytes > static_cast<uint64_t>(SIZE_MAX -
                ((icon_bank || set->bytes_per_pixel == 2) ? 0x400 : 0))) {
            descriptor.status = SpriteSlotStatus::invalid;
            descriptor.note = "retail sprite allocation exceeds supported size";
            ++set->invalid_slots;
            continue;
        }
        const size_t extra = icon_bank || set->bytes_per_pixel == 2 ? 0x400 : 0;
        descriptor.retail_buffer.resize(static_cast<size_t>(retail_bytes) + extra);
        uint64_t position = 0;
        if (!VFS_Seek(vfs, handle, descriptor.pixel_offset, 0, position, source)) {
            close();
            return source_fail(error, source);
        }
        size_t read = 0;
        if (!VFS_Read(vfs, handle, descriptor.retail_buffer.data(),
                      static_cast<size_t>(retail_bytes), read, source)) {
            close();
            return source_fail(error, source);
        }
        descriptor.retail_read_bytes = read;
        if (read < descriptor.source_pixel_bytes) {
            descriptor.status = SpriteSlotStatus::invalid;
            descriptor.note = "sprite pixel payload is truncated";
            ++set->invalid_slots;
        } else {
            descriptor.status = SpriteSlotStatus::loaded;
            if (read < retail_bytes)
                descriptor.note = "retail over-read stopped at the member boundary";
        }
    }
    close();
    result = std::move(set);
    return true;
}

constexpr std::array<IconNameMapping, 72> ICON_NAMES{{
    {"feu",0,0},{"arc",0,27},{"epee",0,5},{"guerison",0,23},{"bouclier",0,28},
    {"connaiss",0,6},{"temps",0,21},{"spirit",0,14},{"holo",0,7},
    {"resurec",0,22},{"invivib",0,8},{"mine",0,15},{"shaman",0,1},
    {"vitesse",0,24},{"cleeau",0,2},{"canard",0,3},{"disque",0,4},
    {"lettre0",0,9},{"lettre1",0,10},{"lettre2",0,11},{"tournd",0,13},
    {"soufnot1",0,16},{"soufnot2",0,17},{"omega",0,18},{"moulin",0,19},
    {"masque",0,20},{"sucette",0,25},{"piece",0,26},{"parfum",0,29},
    {"surfplan",0,30},{"mcombat",0,32},{"infosne",0,31},{"infosde",0,33},
    {"infosta",0,26},{"nothing",1,0},{"block",0,34},{"pyrcurs",2,1},
    {"pyrambo",2,0},{"pyramvi",2,2},{"pyramma",2,3},{"pyramox",2,4},
    {"exprbor",2,5},{"exprlev",2,6},{"replay",2,7},{"record",2,8},
    {"pyrafvi",2,9},{"pyrafma",2,10},{"joy_up",3,0},{"joy_dn",3,1},
    {"joy_lf",3,2},{"joy_rt",3,3},{"joy_k1",3,4},{"joy_k2",3,5},
    {"joy_k3",3,6},{"joy_sel",3,7},{"joy_swi",3,8},{"joy_bt0",3,9},
    {"joy_bt1",3,10},{"UpLf",4,2},{"UpRg",4,3},{"DnLf",4,1},
    {"DnRg",4,0},{"UpLfNA",4,6},{"UpRgNA",4,7},{"DnLfNA",4,5},
    {"DnRgNA",4,4},{"RubLf",4,8},{"RubRg",4,9},{"Desc1",4,10},
    {"Desc2",4,11},{"Desc3",4,12},{"Desc4",4,13}
}};

} // namespace

const SpriteSet* SpriteState::set(size_t slot) const {
    return slot < sets_.size() ? sets_[slot].get() : nullptr;
}

const SpriteSet* SpriteState::icon_bank(size_t bank) const {
    return bank < icon_banks_.size() ? icon_banks_[bank].get() : nullptr;
}

const FontState* SpriteState::font(size_t slot) const {
    return slot < fonts_.size() ? &fonts_[slot] : nullptr;
}

uint32_t SpriteState::mul32(size_t row, size_t column) const {
    return row < 32 && column < 32 ? mul32_[row * 32 + column] : 0;
}

uint32_t SpriteState::mul64(size_t row, size_t column) const {
    return row < 64 && column < 64 ? mul64_[row * 63 + column] : 0;
}

void SPR_InitMulTables(SpriteState& state) {
    for (size_t row = 0; row < 32; ++row)
        for (size_t column = 0; column < 32; ++column)
            state.mul32_[row * 32 + column] = static_cast<uint32_t>(row * column);
    for (size_t row = 0; row < 64; ++row)
        for (size_t column = 0; column < 64; ++column)
            state.mul64_[row * 63 + column] = static_cast<uint32_t>(row * column);
}

bool SPR_LoadSet(SpriteState& state, size_t slot, std::string_view path,
                 SpriteError& error) {
    error = {};
    if (slot >= state.sets_.size())
        return fail(error, SpriteErrorCode::invalid_slot, "sprite set slot is out of range");
    SPR_InitMulTables(state);
    SPR_FreeSet(state, slot);
    return load_source(*state.vfs_, path, false, state.display_16bit_,
                       state.sets_[slot], error);
}

const SpriteDescriptor* SPR_GetDescriptor(const SpriteState& state,
                                           size_t slot, size_t index) {
    const SpriteSet* set = state.set(slot);
    return set && index < set->descriptors.size() ? &set->descriptors[index] : nullptr;
}

void SPR_FreeSet(SpriteState& state, size_t slot) {
    if (slot < state.sets_.size()) state.sets_[slot].reset();
}

bool SPR_LoadIconBanks(SpriteState& state, SpriteError& error) {
    error = {};
    SPR_InitMulTables(state);
    constexpr std::array<std::string_view, 5> names{
        "MAGIE.ALP", "ANIM.ALP", "PYRAM.ALP", "TOUCHES.SPR", "INTERF.ALP"};
    for (size_t i = 0; i < names.size(); ++i) {
        SPR_FreeIconBank(state, i);
        if (!load_source(*state.vfs_, names[i], true, state.display_16bit_,
                         state.icon_banks_[i], error)) return false;
    }
    return true;
}

void SPR_FreeIconBank(SpriteState& state, size_t bank) {
    if (bank < state.icon_banks_.size()) state.icon_banks_[bank].reset();
}

void SPR_FreeIconBanks(SpriteState& state) {
    for (size_t i = 0; i < state.icon_banks_.size(); ++i)
        SPR_FreeIconBank(state, i);
}

const std::array<IconNameMapping, 72>& ICON_NameTable() { return ICON_NAMES; }

IconLookup ICON_FindByName(std::string_view name) {
    for (size_t i = 0; i < ICON_NAMES.size(); ++i) {
        const auto candidate = ICON_NAMES[i].name;
        if (candidate.size() != name.size()) continue;
        bool equal = true;
        for (size_t j = 0; j < name.size(); ++j)
            equal = equal && upper_ascii(candidate[j]) == upper_ascii(name[j]);
        if (equal) return {static_cast<int>(i), ICON_NAMES[i].bank, ICON_NAMES[i].slot};
    }
    return {};
}

bool TEXT_LoadFont(SpriteState& state, size_t slot, std::string_view path,
                   uint32_t style, SpriteError& error) {
    error = {};
    if (slot >= state.fonts_.size())
        return fail(error, SpriteErrorCode::invalid_slot, "font slot is out of range");
    if (state.fonts_[slot].active) TEXT_FreeFont(state, slot);
    if (!SPR_LoadSet(state, slot, path, error)) return false;
    FontState& font = state.fonts_[slot];
    font.active = true;
    font.style = style;
    font.sprite_slot = slot;
    for (size_t i = 0; i < font.advances.size(); ++i) {
        const auto* descriptor = SPR_GetDescriptor(state, slot, i == 0x20 ? 0x30 : i);
        font.advances[i] = static_cast<int32_t>(descriptor->width) -
                           descriptor->field_0c;
    }
    return true;
}

void TEXT_FreeFont(SpriteState& state, size_t slot) {
    if (slot >= state.fonts_.size() || !state.fonts_[slot].active) return;
    SPR_FreeSet(state, state.fonts_[slot].sprite_slot);
    state.fonts_[slot] = {};
}

} // namespace od::port

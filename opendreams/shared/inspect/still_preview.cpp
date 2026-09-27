#include "inspect/still_preview.h"

#include "port/dan.h"
#include "port/dsn.h"
#include "port/resource.h"
#include "port/sprite.h"
#include "port/stream.h"
#include "port/vfs.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <utility>

namespace od::inspect {
namespace {

bool fail(std::string& error, std::string message) {
    error = std::move(message);
    return false;
}

uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
std::string upper(std::string value) {
    for (char& c : value) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return value;
}
bool ends(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}
bool index_from_key(std::string_view key, std::string_view prefix, size_t& index) {
    if (key.substr(0, prefix.size()) != prefix) return false;
    const char* begin = key.data() + prefix.size();
    const char* end = key.data() + key.size();
    const auto parsed = std::from_chars(begin, end, index);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

std::array<uint8_t, 4> rgb555(uint16_t word) {
    const unsigned r = (word >> 10) & 31, g = (word >> 5) & 31, b = word & 31;
    return {static_cast<uint8_t>((r << 3) | (r >> 2)),
            static_cast<uint8_t>((g << 3) | (g >> 2)),
            static_cast<uint8_t>((b << 3) | (b >> 2)), 255};
}
std::array<uint8_t, 4> rgb565(uint16_t word) {
    return {static_cast<uint8_t>(((word >> 11) & 31) * 255 / 31),
            static_cast<uint8_t>(((word >> 5) & 63) * 255 / 63),
            static_cast<uint8_t>((word & 31) * 255 / 31), 255};
}

bool read_at(port::VfsContext& vfs, std::string_view path, uint64_t offset,
             size_t count, std::vector<uint8_t>& bytes, std::string& error) {
    port::VfsError source;
    int32_t handle = 0;
    if (!port::VFS_Open(vfs, path, 0x200, handle, source))
        return fail(error, source.message);
    uint64_t size = 0, position = 0;
    size_t read = 0;
    const bool okay = port::VFS_GetSize(vfs, handle, size, source) &&
        offset <= size && count <= size - offset &&
        port::VFS_Seek(vfs, handle, static_cast<int64_t>(offset), 0, position, source);
    if (okay) {
        bytes.resize(count);
        if (!port::VFS_Read(vfs, handle, bytes.data(), count, read, source) || read != count) {
            bytes.clear();
            port::VfsError ignored;
            port::VFS_Close(vfs, handle, ignored);
            return fail(error, source ? source.message : "selected asset is truncated");
        }
    }
    port::VfsError ignored;
    port::VFS_Close(vfs, handle, ignored);
    if (!okay) return fail(error, source ? source.message :
                           "selected pixel range exceeds its physical file");
    return true;
}

bool from_sprite(const Source& source, const Row& row,
                 StillImage& image, std::string& error) {
    if (row.parent == SIZE_MAX || row.parent >= source.rows.size())
        return fail(error, "sprite slot has no owning set");
    const Row& owner = source.rows[row.parent];
    size_t slot = 0;
    if (!index_from_key(row.key, "slot:", slot) || slot >= 256)
        return fail(error, "sprite slot key is invalid");
    port::VfsContext vfs(source.image);
    std::string path = owner.path;
    if (owner.parent != SIZE_MAX && owner.parent < source.rows.size() &&
        source.rows[owner.parent].kind == "BF archive") {
        std::vector<port::BfEntry> members;
        port::VfsError mount_error;
        if (!port::BF_Mount(vfs, owner.path, members, mount_error))
            return fail(error, mount_error.message);
        const std::string selected = upper(owner.name);
        const size_t matches = static_cast<size_t>(std::count_if(members.begin(),members.end(),
            [&](const port::BfEntry& member) { return upper(member.name) == selected; }));
        if (matches != 1)
            return fail(error, "BF member name is ambiguous in the selected physical archive");
        path = owner.name;
    }
    port::SpriteState state(vfs);
    port::SpriteError sprite_error;
    const bool font = owner.kind == "Font";
    const bool loaded = font ? port::TEXT_LoadFont(state, 0, path, 0, sprite_error)
                             : port::SPR_LoadSet(state, 0, path, sprite_error);
    if (!loaded) return fail(error, sprite_error.message);
    const auto* set = state.set(0);
    const auto* descriptor = port::SPR_GetDescriptor(state, 0, slot);
    if (!set || !descriptor || descriptor->status != port::SpriteSlotStatus::loaded)
        return fail(error, "selected sprite or glyph has no valid pixels");
    const size_t count = static_cast<size_t>(descriptor->width) * descriptor->height;
    if (!count || descriptor->source_pixel_bytes != count * set->bytes_per_pixel ||
        descriptor->retail_buffer.size() < descriptor->source_pixel_bytes)
        return fail(error, "sprite pixel extent differs from its descriptor");
    image.width = descriptor->width;
    image.height = descriptor->height;
    image.indices.resize(count);
    image.colors.resize(256);
    image.transparent_zero = true;
    const std::string source_name=upper(path);
    image.pyramid_commands = source_name == "PYRAM.ALP" ||
        ends(source_name,"/PYRAM.ALP");
    for (size_t i=0; i<256; ++i) image.colors[i] = rgb555(set->palette[i]);
    if (set->bytes_per_pixel == 2) image.coverage.resize(count);
    for (size_t i=0; i<count; ++i) {
        image.indices[i] = descriptor->retail_buffer[i * set->bytes_per_pixel];
        if (set->bytes_per_pixel == 2)
            image.coverage[i] = descriptor->retail_buffer[i * 2 + 1];
    }
    image.note = font ? "RGB555 font palette; index 0 is transparent" :
        set->bytes_per_pixel == 2 ?
        "RGB555 palette; the second source byte is coverage" :
        "RGB555 palette; index 0 is transparent";
    if (image.pyramid_commands)
        image.note += "; yellow/cyan/magenta mark dynamic layer commands";
    return true;
}

bool from_vga_sprite(const Source& source, const Row& row,
                     StillImage& image, std::string& error) {
    port::VfsContext vfs(source.image);
    std::vector<uint8_t> palette, record;
    if (!read_at(vfs,row.path,0,1024,palette,error) ||
        !read_at(vfs,row.path,row.offset,static_cast<size_t>(row.size),record,error))
        return false;
    if (record.size() < 16) return fail(error,"VGA sprite record is truncated");
    const uint32_t width = le32(record.data()), height = le32(record.data()+4);
    if (!width || !height || width>4096 || height>4096 ||
        static_cast<uint64_t>(width)*height != record.size()-16)
        return fail(error,"VGA sprite dimensions differ from its physical record");
    image.width = width; image.height = height;
    image.indices.assign(record.begin()+16,record.end());
    image.colors.resize(256);
    image.transparent_zero = true; // A viewer convention; retail opacity is unverified.
    for (size_t i=0; i<256; ++i) {
        const uint8_t* entry = palette.data()+i*4;
        if (entry[0]>63 || entry[1]>63 || entry[2]>63)
            return fail(error,"VGA sprite palette contains a non-DAC channel");
        image.colors[i] = {
            static_cast<uint8_t>((entry[0]<<2)|(entry[0]>>4)),
            static_cast<uint8_t>((entry[1]<<2)|(entry[1]>>4)),
            static_cast<uint8_t>((entry[2]<<2)|(entry[2]>>4)),255};
    }
    image.note = "6-bit VGA palette; index-0 transparency is a viewer assumption";
    return true;
}

bool from_bank(const std::vector<uint8_t>& bank, StillImage& image,
               std::string& error) {
    if (bank.size() != 0x18014u)
        return fail(error,"texture bank is not the retail 0x18014-byte layout");
    image.width = image.height = 256;
    image.indices.assign(bank.begin()+0x8014,bank.end());
    image.colors.resize(32u*256u);
    image.transparent_zero = false;
    size_t best_count = 0;
    for (size_t row=0; row<32; ++row) {
        std::array<uint16_t,256> packed{};
        for (size_t i=0; i<256; ++i) {
            const uint8_t* entry = bank.data()+0x14+row*1024+i*4;
            packed[i] = le16(entry+2);
            image.colors[row*256+i] = rgb565(packed[i]);
        }
        std::sort(packed.begin(),packed.end());
        const size_t count = static_cast<size_t>(std::unique(packed.begin(),packed.end())-
                                                  packed.begin());
        if (count > best_count) { best_count=count; image.default_row=static_cast<uint32_t>(row); }
    }
    image.note = "RGB565 palette ramp; default row retains the most distinct colors";
    return true;
}

bool from_standalone_bank(const Source& source, const Row& row,
                          StillImage& image, std::string& error) {
    port::VfsContext vfs(source.image);
    std::vector<uint8_t> header;
    if (!read_at(vfs,row.path,0,8,header,error)) return false;
    if (std::memcmp(header.data(),"F3DC",4)!=0 || le32(header.data()+4)!=450)
        return fail(error,"standalone texture has no retail F3DC/450 header");
    std::vector<uint8_t> bank;
    if (!port::RES_ReadFile(vfs,row.path,bank,error)) return false;
    return from_bank(bank,image,error);
}

bool from_dan_bank(const Source& source, const Row& row,
                   StillImage& image, std::string& error) {
    port::VfsContext vfs(source.image);
    port::DanArchive dan(vfs);
    port::DanError source_error;
    if (!port::DAN_OpenArchive(dan,row.path,source_error))
        return fail(error,source_error.message);
    const size_t slash = row.path.find_last_of("/\\");
    const std::string basename = row.path.substr(slash == std::string::npos ? 0 : slash+1);
    const size_t dot = basename.find_last_of('.');
    const std::string model_name = basename.substr(0,dot)+".3DC";
    std::vector<uint8_t> model,bank;
    if (!port::DAN_Read3DC(dan,model_name,model,source_error) ||
        !port::DAN_ReadTextureChunks(dan,source_error) ||
        !port::DAN_Load3DM(dan,row.name,bank,source_error))
        return fail(error,source_error.message);
    return from_bank(bank,image,error);
}

bool from_scene_texture(const Source& source, const Row& row,
                        StillImage& image, std::string& error) {
    size_t object_index = 0;
    if (row.kind == "Scene texture" &&
        !index_from_key(row.key,"texture:",object_index))
        return fail(error,"scene texture key is invalid");
    port::VfsContext vfs(source.image);
    port::StreamError stream_error;
    auto stream = port::STRM_Create(vfs,0x57800,0x57800,0x8000,stream_error);
    if (!stream) return fail(error,stream_error.message);
    port::DsnState dsn;
    port::DSN_InitState(dsn,*stream);
    port::DsnError source_error;
    std::vector<uint8_t> geometry,collision;
    port::DsnTexturePage page;
    const bool loaded = port::DSN_LoadHeader(dsn,row.path,source_error) &&
        port::DSN_LoadMaterialsAndFaces(dsn,geometry,source_error) &&
        port::DSN_LoadVertexPool(dsn,collision,source_error) &&
        port::DSN_LoadTextures(dsn,object_index,page,source_error);
    port::STRM_Free(stream);
    if (!loaded)
        return fail(error,source_error.message);
    image.width = image.height = 256;
    image.indices = std::move(page.indices);
    image.colors.resize(256);
    for (size_t i=0; i<256; ++i) image.colors[i] = rgb565(page.palette_rgb565[i]);
    image.transparent_zero = false;
    image.note = "Assembled from one palette tag and 64 progressive texture planes";
    if (row.kind != "Scene texture")
        image.note += "; showing the scene's first object";
    return true;
}

} // namespace

bool StillImage::render(uint32_t row, bool hide_zero,
                        std::vector<uint8_t>& rgba, std::string& error) const {
    error.clear();
    rgba.clear();
    if (!width || !height || indices.size()!=static_cast<size_t>(width)*height ||
        (!coverage.empty() && coverage.size()!=indices.size()) ||
        row>=palette_rows())
        return fail(error,"selected image or palette row is incomplete");
    rgba.resize(indices.size()*4);
    const auto* palette=colors.data()+static_cast<size_t>(row)*256;
    for (size_t i=0; i<indices.size(); ++i) {
        const uint8_t index=indices[i];
        auto color=palette[index];
        if (hide_zero && index==0) color[3]=0;
        if (!coverage.empty()) {
            const uint8_t blend=coverage[i];
            if (pyramid_commands && index!=0 && blend>=0xfd) {
                color = blend==0xfd ? std::array<uint8_t,4>{255,255,0,255} :
                        blend==0xfe ? std::array<uint8_t,4>{0,255,255,255} :
                                      std::array<uint8_t,4>{255,0,255,255};
            } else if (index!=0 || !hide_zero) {
                color[3]=blend>=63 ? 255 :
                    static_cast<uint8_t>(((blend>>1)*255+16)/32);
            }
        }
        std::memcpy(rgba.data()+i*4,color.data(),4);
    }
    return true;
}

bool StillImage::palette_rgba(uint32_t row,
                              std::array<uint8_t,1024>& rgba) const {
    if (row>=palette_rows()) return false;
    for (size_t i=0; i<256; ++i)
        std::memcpy(rgba.data()+i*4,colors[static_cast<size_t>(row)*256+i].data(),4);
    return true;
}

bool load_still_image(const Source& source, const Row& row,
                      StillImage& image, std::string& error) {
    image = {};
    error.clear();
    if (!source.image || row.path.empty()) return fail(error,"selected image has no physical source");
    if (row.kind=="Sprite slot" || row.kind=="Font glyph")
        return from_sprite(source,row,image,error);
    if (row.kind=="VGA sprite") return from_vga_sprite(source,row,image,error);
    if (row.kind=="Material texture") return from_dan_bank(source,row,image,error);
    if (row.kind=="Scene texture" || row.kind=="Palette tag" ||
        row.kind=="Texture tile tag") return from_scene_texture(source,row,image,error);
    if (row.physical && ends(upper(row.path),".3DM"))
        return from_standalone_bank(source,row,image,error);
    return fail(error,"selected row has no static image decoder");
}

bool portrait_still_image(const port::PortraitSprite& portrait,
                          StillImage& image, std::string& error) {
    image={}; error.clear();
    const size_t count=static_cast<size_t>(portrait.width)*portrait.height;
    if (!count || portrait.indices.size()!=count || portrait.coverage.size()!=count)
        return fail(error,"dialogue portrait has no complete indexed image");
    image.width=portrait.width;
    image.height=portrait.height;
    image.indices=portrait.indices;
    image.coverage=portrait.coverage;
    image.colors.resize(256);
    image.transparent_zero=true;
    image.note="Dialogue portrait: RGB555 palette and per-pixel coverage";
    for (size_t i=0; i<256; ++i)
        image.colors[i]=rgb555(portrait.palette_rgb555[i]);
    return true;
}

} // namespace od::inspect

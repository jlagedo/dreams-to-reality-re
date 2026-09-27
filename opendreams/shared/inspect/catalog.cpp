#include "inspect/catalog.h"

#include "port/dan.h"
#include "port/ddat.h"
#include "port/drd.h"
#include "port/dsn.h"
#include "port/fsb.h"
#include "port/sprite.h"
#include "port/stream.h"
#include "port/vfs.h"
#include "port/video.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace od::inspect {
namespace {

std::string upper(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}

std::string_view basename(std::string_view path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

bool ends(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::string cell(const uint8_t* data, size_t length) {
    size_t count = 0;
    while (count < length && data[count]) ++count;
    return std::string(reinterpret_cast<const char*>(data), count);
}

uint32_t little32(const uint8_t* value) {
    return static_cast<uint32_t>(value[0]) |
        (static_cast<uint32_t>(value[1]) << 8) |
        (static_cast<uint32_t>(value[2]) << 16) |
        (static_cast<uint32_t>(value[3]) << 24);
}

uint16_t little16(const uint8_t* value) {
    return static_cast<uint16_t>(value[0]) |
        static_cast<uint16_t>(value[1] << 8);
}

Group group_for(std::string_view path) {
    const std::string name = upper(basename(path));
    if (name == "DREAMS.DAT") return Group::projects;
    if (ends(name, ".DSN")) return Group::scenes;
    if (ends(name, ".DAN") || ends(name, ".3DC")) return Group::models;
    if (ends(name, ".3DM")) return Group::textures;
    if (ends(name, ".HNM") || ends(name, ".UBB")) return Group::video;
    if (ends(name, ".DRD")) return Group::dialogue;
    if (name == "FSB.DAT" || ends(name, ".FSB")) return Group::audio;
    if (ends(name, ".BF") || ends(name, ".SPR") || ends(name, ".ALP"))
        return Group::interface_assets;
    if (ends(name, ".INI") || ends(name, ".TXT") || ends(name, ".SAV") ||
        ends(name, ".ID") || ends(name, ".DEM")) return Group::game_data;
    return Group::extras;
}

bool has_indexer(std::string_view path) {
    const std::string name = upper(basename(path));
    return name == "DREAMS.DAT" || name == "FSB.DAT" ||
        ends(name, ".BF") || ends(name, ".DSN") || ends(name, ".DAN") ||
        ends(name, ".DRD") || ends(name, ".SPR") || ends(name, ".ALP") ||
        ends(name, ".3DM") ||
        ends(name, ".HNM") || ends(name, ".UBB") || ends(name, ".FSB");
}

size_t add(Source& source, Row row) {
    row.id = source.rows.size();
    const size_t parent = row.parent;
    source.rows.push_back(std::move(row));
    source.children.emplace_back();
    if (parent < source.children.size()) source.children[parent].push_back(source.rows.size() - 1);
    return source.rows.size() - 1;
}

void invalid(Row& row, std::string message) {
    row.status = Status::invalid;
    row.detail = std::move(message);
}

void add_project_children(Source& source, size_t project, const uint8_t* record) {
    const std::string path = source.rows[project].path;
    const auto child = [&](size_t offset, size_t length, Group group,
                           std::string kind, std::string target, size_t index) {
        Row row;
        row.parent = project;
        row.group = group;
        row.status = target.empty() ? Status::available : Status::indexing;
        row.name = cell(record + offset, std::min<size_t>(12, length));
        row.kind = std::move(kind);
        row.path = path;
        row.key = row.name + ":" + std::to_string(index);
        row.detail = "Target: " + target;
        row.provenance = "retail DDAT_LoadRecord; viewer-derived field offsets";
        return add(source, std::move(row));
    };
    for (size_t i = 0; i < 16; ++i) {
        const size_t at = 0x600 + i * 0xc0;
        if (std::memcmp(record + at, "OBJET", 5) != 0) continue;
        const std::string asset = cell(record + at + 12, 32);
        child(at, 0xc0, i == 0 ? Group::scenes : Group::models,
              "Project object", asset, i);
    }
    for (size_t i = 0; i < 8; ++i) {
        const size_t at = 0x200 + i * 0x80;
        if (std::memcmp(record + at, "LINK", 4) != 0) continue;
        child(at, 0x80, Group::projects, "Project link",
              cell(record + at + 12, 12), i);
    }
    for (size_t i = 0; i < 16; ++i) {
        const size_t at = 0x1e00 + i * 0x40;
        if (std::memcmp(record + at, "LINKADVENT", 10) != 0) continue;
        const size_t event = child(at, 0x40, Group::projects, "Project event", "", i);
        const std::string video = cell(record + at + 0x2c, 20);
        if (!video.empty()) {
            Row row;
            row.parent = event;
            row.group = Group::video;
            row.status = Status::indexing;
            row.name = video;
            row.kind = "Cutscene reference";
            row.path = path;
            row.key = "event-video:" + std::to_string(i);
            row.detail = "Target: " + video;
            row.provenance = "retail DDAT_LoadRecord; viewer-derived LINKADVENT +0x2c";
            add(source, std::move(row));
        }
        const uint32_t opcode = little32(record + at + 0x20);
        const uint32_t stage = little32(record + at + 0x1c);
        if (opcode == 0x40 && stage > 0) {
            Row row;
            row.parent = event;
            row.group = Group::dialogue;
            row.status = Status::indexing;
            row.name = "Dialogue " + std::to_string(stage);
            row.kind = "Dialogue reference";
            row.path = path;
            row.key = "event-dialogue:" + std::to_string(i);
            row.detail = "Target: " + row.name;
            row.provenance = "retail DDAT_LoadRecord; viewer-derived LINKADVENT opcode 0x40";
            add(source, std::move(row));
        }
    }
    const uint32_t track = little32(record + 0x1f8);
    if (track > 0 && track < 100) {
        Row row;
        row.parent = project;
        row.group = Group::audio;
        row.status = Status::indexing;
        row.name = "CD track " + std::to_string(track);
        row.kind = "Music reference";
        row.path = path;
        row.key = "music";
        row.detail = "Target: " + row.name;
        row.provenance = "retail DDAT_LoadRecord; viewer-derived header +0x1f8";
        add(source, std::move(row));
    }
}

} // namespace

struct Catalog::State : Source {
    std::unique_ptr<port::VfsContext> vfs;
    std::vector<size_t> jobs;
    size_t next_job = 0;

    void sprite_rows(size_t parent, const port::SpriteSet& set,
                     std::string_view provenance, int icon_bank = -1) {
        rows[parent].kind = icon_bank >= 0 ? "Icon bank" :
            upper(set.path).find("/FONT/") != std::string::npos ? "Font" : "Sprite set";
        rows[parent].status = Status::available;
        rows[parent].provenance = std::string(provenance);
        for (size_t i = 0; i < set.descriptors.size(); ++i) {
            const auto& descriptor = set.descriptors[i];
            if (descriptor.status == port::SpriteSlotStatus::empty) continue;
            Row row;
            row.parent = parent;
            row.group = Group::interface_assets;
            row.status = descriptor.status == port::SpriteSlotStatus::invalid
                ? Status::invalid : Status::available;
            row.name = rows[parent].kind == "Font" ? "Glyph " + std::to_string(i)
                : "Slot " + std::to_string(i);
            if (icon_bank >= 0) for (const auto& name : port::ICON_NameTable()) {
                if (name.bank == icon_bank && name.slot == static_cast<int>(i)) {
                    row.name += " (" + std::string(name.name) + ")";
                    break;
                }
            }
            row.kind = rows[parent].kind == "Font" ? "Font glyph" : "Sprite slot";
            row.path = rows[parent].path;
            row.key = "slot:" + std::to_string(i);
            row.offset = descriptor.pixel_offset;
            if (!rows[parent].physical && rows[parent].has_extent)
                row.offset += rows[parent].offset;
            row.size = descriptor.source_pixel_bytes;
            row.has_extent = row.status == Status::available;
            row.detail = std::to_string(descriptor.width) + " x " +
                std::to_string(descriptor.height) + "  " + descriptor.note;
            row.provenance = rows[parent].provenance;
            add(*this, std::move(row));
        }
    }

    // Viewer-derived index for DATA/OBJET's distinct VGA pointer-table sheets.
    // No retail reader for this family has been identified. All bytes still
    // enter through the shared retail VFS boundary, not a second ISO parser.
    void object_sheet(size_t parent, std::string_view path) {
        port::VfsError error;
        int32_t handle = 0;
        if (!port::VFS_Open(*vfs, path, 0x200, handle, error)) {
            invalid(rows[parent], error.message); return;
        }
        const auto close = [&]() { port::VfsError ignored; port::VFS_Close(*vfs, handle, ignored); };
        uint64_t source_size = 0;
        if (!port::VFS_GetSize(*vfs, handle, source_size, error) || source_size < 0x800) {
            invalid(rows[parent], error ? error.message : "VGA sprite sheet is too short");
            close(); return;
        }
        std::array<uint8_t, 0x800> header{};
        size_t read = 0;
        if (!port::VFS_Read(*vfs, handle, header.data(), header.size(), read, error) ||
            read != header.size()) {
            invalid(rows[parent], error ? error.message : "VGA sprite header is truncated");
            close(); return;
        }
        for (size_t i = 0; i < 256; ++i) {
            const size_t at = i * 4;
            if (header[at] > 0x3f || header[at + 1] > 0x3f || header[at + 2] > 0x3f) {
                invalid(rows[parent], "not a 6-bit VGA palette"); close(); return;
            }
        }
        rows[parent].kind = "VGA sprite sheet";
        rows[parent].status = Status::available;
        rows[parent].provenance = "viewer-derived pointer-table index after retail VFS_Open/VFS_Read";
        std::vector<uint64_t> seen;
        for (size_t i = 0; i < 256; ++i) {
            const uint32_t relative = little32(header.data() + 0x400 + i * 4);
            if (relative == 0 || relative == 0x100 || relative == 0x5000) continue;
            const uint64_t offset = 0x400ull + relative;
            if (offset > source_size || source_size - offset < 16 ||
                std::find(seen.begin(), seen.end(), offset) != seen.end()) continue;
            seen.push_back(offset);
            uint64_t position = 0;
            std::array<uint8_t, 16> record{};
            if (!port::VFS_Seek(*vfs, handle, static_cast<int64_t>(offset), 0, position, error) ||
                !port::VFS_Read(*vfs, handle, record.data(), record.size(), read, error) ||
                read != record.size()) continue;
            const uint32_t width = little32(record.data());
            const uint32_t height = little32(record.data() + 4);
            if (!width || !height || width > 4096 || height > 4096 ||
                static_cast<uint64_t>(width) * height > source_size - offset - 16) continue;
            Row row;
            row.parent = parent;
            row.group = Group::interface_assets;
            row.status = Status::available;
            row.name = "Slot " + std::to_string(i);
            row.kind = "VGA sprite";
            row.path = rows[parent].path;
            row.key = "slot:" + std::to_string(i);
            row.offset = offset;
            row.size = 16 + static_cast<uint64_t>(width) * height;
            row.has_extent = true;
            row.detail = std::to_string(width) + " x " + std::to_string(height);
            row.provenance = rows[parent].provenance;
            add(*this, std::move(row));
        }
        close();
    }

    void scene_tags(size_t parent, std::string_view path, uint64_t body_offset) {
        port::VfsError error;
        int32_t handle = 0;
        if (!port::VFS_Open(*vfs, path, 0x200, handle, error)) {
            invalid(rows[parent], error.message); return;
        }
        const auto close = [&]() { port::VfsError ignored; port::VFS_Close(*vfs, handle, ignored); };
        uint64_t size = 0;
        if (!port::VFS_GetSize(*vfs, handle, size, error) || body_offset > size) {
            invalid(rows[parent], error ? error.message : "DSN body offset exceeds file");
            close(); return;
        }
        uint64_t position = body_offset;
        size_t tag_index = 0;
        while (position < size && tag_index < 100000) {
            if (size - position < 5) { invalid(rows[parent], "truncated DSN tag header"); break; }
            uint64_t seek_position = 0;
            std::array<uint8_t, 5> header{};
            size_t read = 0;
            if (!port::VFS_Seek(*vfs, handle, static_cast<int64_t>(position), 0,
                                seek_position, error) ||
                !port::VFS_Read(*vfs, handle, header.data(), header.size(), read, error) ||
                read != header.size()) {
                invalid(rows[parent], error ? error.message : "short DSN tag read"); break;
            }
            const uint32_t span = little32(header.data() + 1);
            if (span < 5 || span > size - position) {
                invalid(rows[parent], "DSN tag size exceeds physical file"); break;
            }
            Row row;
            row.parent = parent;
            row.group = header[0] == 3 || header[0] == 4 ? Group::textures : Group::scenes;
            row.status = Status::available;
            row.name = "Tag " + std::to_string(tag_index) + " (" +
                std::to_string(header[0]) + ")";
            row.kind = header[0] == 1 ? "Geometry tag" : header[0] == 2
                ? "Packed geometry tag" : header[0] == 3 ? "Palette tag"
                : header[0] == 4 ? "Texture tile tag" : "Unknown DSN tag";
            row.path = rows[parent].path;
            row.key = "tag:" + std::to_string(tag_index);
            row.offset = position;
            row.size = span;
            row.has_extent = true;
            row.provenance = "viewer-derived tag header after retail DSN_LoadHeader and VFS_Read";
            add(*this, std::move(row));
            position += span;
            ++tag_index;
        }
        if (tag_index == 100000) invalid(rows[parent], "DSN tag count exceeds viewer limit");
        close();
    }

    void sprites(size_t parent, std::string_view path) {
        port::SpriteState state(*vfs);
        port::SpriteError error;
        const bool is_font = upper(path).find("/FONT/") != std::string::npos;
        const bool ok = is_font
            ? port::TEXT_LoadFont(state, 0, path, 0, error)
            : port::SPR_LoadSet(state, 0, path, error);
        if (!ok) { invalid(rows[parent], error.message); return; }
        const port::SpriteSet* set = state.set(0);
        sprite_rows(parent, *set, is_font ? "retail TEXT_LoadFont" : "retail SPR_LoadSet");
    }

    void index(size_t index) {
        const std::string path = rows[index].path;
        const std::string name = upper(basename(path));
        if (ends(name, ".BF")) {
            std::vector<port::BfEntry> members;
            port::VfsError error;
            if (!port::BF_Mount(*vfs, path, members, error)) {
                invalid(rows[index], error.message); return;
            }
            rows[index].kind = "BF archive";
            rows[index].status = Status::available;
            rows[index].provenance = "retail BF_Mount";
            std::array<size_t, 5> icon_rows{};
            icon_rows.fill(SIZE_MAX);
            constexpr std::array<std::string_view, 5> icon_names{{
                "MAGIE.ALP", "ANIM.ALP", "PYRAM.ALP", "TOUCHES.SPR", "INTERF.ALP"}};
            for (const auto& member : members) {
                Row row;
                row.parent = index;
                row.group = group_for(member.name);
                row.status = Status::available;
                row.name = member.name;
                row.kind = "BF member";
                row.path = path;
                row.key = "member:" + std::to_string(member.row);
                row.offset = member.offset;
                row.size = member.byte_size;
                row.has_extent = true;
                row.provenance = "retail BF_Mount";
                const size_t child = add(*this, std::move(row));
                bool fixed_icon = false;
                for (size_t bank = 0; bank < icon_names.size(); ++bank)
                    if (upper(member.name) == icon_names[bank]) {
                        icon_rows[bank] = child;
                        fixed_icon = true;
                    }
                if (!fixed_icon && (ends(upper(member.name), ".SPR") ||
                                    ends(upper(member.name), ".ALP")))
                    sprites(child, member.name);
            }
            port::SpriteState icons(*vfs);
            port::SpriteError sprite_error;
            if (port::SPR_LoadIconBanks(icons, sprite_error)) {
                for (size_t bank = 0; bank < icon_rows.size(); ++bank)
                    if (icon_rows[bank] != SIZE_MAX && icons.icon_bank(bank))
                        sprite_rows(icon_rows[bank], *icons.icon_bank(bank),
                                    "retail SPR_LoadIconBanks / ICON_NameTable", static_cast<int>(bank));
            } else for (size_t child : icon_rows)
                if (child != SIZE_MAX) invalid(rows[child], sprite_error.message);
            return;
        }
        if (name == "DREAMS.DAT") {
            port::DdatBank bank;
            port::DdatError error;
            if (!port::DDAT_Load(*vfs, bank, error)) {
                invalid(rows[index], error.message); return;
            }
            rows[index].kind = "Project bank";
            rows[index].status = Status::available;
            rows[index].provenance = "retail DDAT_Load / DDAT_LoadRecord / RLE_UnpackZeros";
            for (size_t i = 0; i < port::DdatBank::record_count; ++i) {
                const std::string record_name(bank.record_name(i));
                const uint8_t* record = nullptr;
                if (!bank.has_record(record_name) ||
                    !port::DDAT_LoadRecord(bank, record_name, record, error)) {
                    invalid(rows[index], error.message); break;
                }
                Row row;
                row.parent = index;
                row.group = Group::projects;
                row.status = Status::available;
                row.name = record_name;
                row.kind = "Project";
                row.path = path;
                row.key = "record:" + std::to_string(i);
                row.size = port::DdatBank::record_size;
                row.detail = "Record " + std::to_string(i) + ", decoded size 0x2200";
                row.provenance = "retail DDAT_LoadRecord / RLE_UnpackZeros";
                const size_t project = add(*this, std::move(row));
                add_project_children(*this, project, record);
            }
            return;
        }
        if (ends(name, ".DSN")) {
            port::StreamError stream_error;
            auto stream = port::STRM_Create(*vfs, 0x57800, 0x57800, 0x8000, stream_error);
            if (!stream) { invalid(rows[index], stream_error.message); return; }
            port::DsnState dsn;
            port::DSN_InitState(dsn, *stream);
            port::DsnError error;
            if (!port::DSN_LoadHeader(dsn, path, error)) {
                invalid(rows[index], error.message); return;
            }
            rows[index].kind = "Scene";
            rows[index].status = Status::available;
            rows[index].provenance = "retail DSN_LoadHeader";
            for (size_t i = 0; i < dsn.objects().size(); ++i) {
                const auto& object = dsn.objects()[i];
                Row row;
                row.parent = index;
                row.group = Group::scenes;
                row.status = Status::available;
                row.name = object.name.empty() ? "Object " + std::to_string(i) : object.name;
                row.kind = "Scene object";
                row.path = path;
                row.key = "object:" + std::to_string(i);
                row.detail = "Header record " + std::to_string(i);
                row.provenance = "retail DSN_LoadHeader";
                add(*this, std::move(row));
                Row texture;
                texture.parent = index;
                texture.group = Group::textures;
                texture.status = Status::available;
                texture.name = dsn.objects()[i].name + " texture";
                texture.kind = "Scene texture";
                texture.path = path;
                texture.key = "texture:" + std::to_string(i);
                texture.detail = "Object " + std::to_string(i) +
                    "; 256 x 256 indexed page assembled from DSN tags 3/4";
                texture.provenance = "viewer-selected object via retail DSN_LoadTextures";
                add(*this, std::move(texture));
            }
            const uint64_t body_offset = dsn.body_offset();
            port::StreamError ignored;
            port::STRM_Close(*stream, ignored);
            scene_tags(index, path, body_offset);
            return;
        }
        if (ends(name, ".DAN")) {
            port::DanArchive dan(*vfs);
            port::DanError error;
            if (!port::DAN_OpenArchive(dan, path, error)) {
                invalid(rows[index], error.message); return;
            }
            rows[index].kind = "Model archive";
            rows[index].status = Status::available;
            rows[index].provenance = "retail DAN_OpenArchive";
            for (size_t i = 0; i < dan.names().size(); ++i) {
                Row row;
                row.parent = index;
                row.group = Group::models;
                row.status = Status::available;
                row.name = dan.names()[i].text;
                row.kind = "Model name";
                row.path = path;
                row.key = "name:" + std::to_string(i);
                row.provenance = "retail DAN_OpenArchive";
                add(*this, std::move(row));
                Row texture;
                texture.parent = index;
                texture.group = Group::textures;
                texture.status = Status::available;
                texture.name = dan.names()[i].text + ".3DM";
                texture.kind = "Material texture";
                texture.path = path;
                texture.key = "material:" + std::to_string(i);
                texture.detail = "Indexed 256 x 256 page with 32 palette rows";
                texture.provenance = "retail DAN_ReadTextureChunks / DAN_Load3DM";
                add(*this, std::move(texture));
            }
            const bool chunks_ok = port::DAN_ReadAnimChunks(dan, error);
            for (size_t i = 0; i < port::DAN_GetAnimCount(dan); ++i) {
                Row row;
                row.parent = index;
                row.group = Group::animations;
                row.status = chunks_ok ? Status::available : Status::invalid;
                row.name = std::string(port::DAN_GetAnimName(dan, i));
                row.kind = "Animation clip";
                row.path = path;
                row.key = "clip:" + std::to_string(i);
                row.provenance = "retail DAN_GetAnimName / DAN_ReadAnimChunks";
                if (chunks_ok && i < dan.animation_chunks().size()) {
                    row.offset = dan.animation_chunks()[i].file_offset;
                    row.size = dan.animation_chunks()[i].payload_size + 5;
                    row.has_extent = true;
                } else row.detail = error.message;
                add(*this, std::move(row));
            }
            if (!chunks_ok) invalid(rows[index], error.message);
            return;
        }
        if (ends(name, ".3DM")) {
            rows[index].kind = "Texture bank";
            rows[index].status = Status::available;
            rows[index].provenance = "retail RES_ReadFile loose .3DM route";
            return;
        }
        if (ends(name, ".DRD")) {
            port::DrdBank drd(*vfs);
            port::DrdError error;
            if (!port::DRD_Open(drd, path, error)) {
                invalid(rows[index], error.message); return;
            }
            rows[index].kind = "Dialogue bank";
            rows[index].status = Status::available;
            rows[index].provenance = "retail DRD_Open / DRD_LoadEntry";
            for (size_t i = 0; i < drd.entry_count(); ++i) {
                Row row;
                row.parent = index;
                row.group = Group::dialogue;
                row.name = "Dialogue " + std::to_string(i + 1);
                row.kind = "Dialogue entry";
                row.path = path;
                row.key = "entry:" + std::to_string(i);
                row.offset = drd.entry_offset(i);
                row.size = drd.entry_size(i);
                row.has_extent = true;
                row.provenance = "retail DRD_Open / DRD_LoadEntry";
                if (port::DRD_LoadEntry(drd, i, error)) {
                    row.status = Status::available;
                    row.detail = "Voice bytes " + std::to_string(drd.wave().size) +
                        ", caption lines " + std::to_string(port::DRD_GetLineCount(drd)) +
                        ", portrait bytes " + std::to_string(port::DRD_GetPortrait(drd).size);
                } else { row.status = Status::invalid; row.detail = error.message; }
                add(*this, std::move(row));
            }
            return;
        }
        if (name == "FSB.DAT" || ends(name, ".FSB")) {
            port::FsbBank fsb(*vfs);
            port::FsbError error;
            if (!port::FSB_Load(fsb, path, error)) {
                invalid(rows[index], error.message); return;
            }
            rows[index].kind = "Sound bank";
            rows[index].status = Status::available;
            rows[index].provenance = "retail FSB_Load";
            for (const auto& clip : fsb.clips()) {
                Row row;
                row.parent = index;
                row.group = Group::audio;
                row.status = Status::available;
                row.name = "Sound " + std::to_string(clip.index);
                row.kind = "Sound effect";
                row.path = path;
                row.key = "clip:" + std::to_string(clip.index);
                row.offset = clip.file_offset;
                row.size = clip.byte_size;
                row.has_extent = true;
                row.provenance = "retail FSB_Load";
                add(*this, std::move(row));
            }
            return;
        }
        if (ends(name, ".SPR") || ends(name, ".ALP")) {
            if (upper(path).find("DATA/OBJET/") == 0 && ends(name, ".SPR"))
                object_sheet(index, path);
            else sprites(index, path);
            return;
        }
        if (ends(name, ".HNM") || ends(name, ".UBB")) {
            port::VideoState video(*vfs);
            port::VideoError error;
            if (!port::VID_Open(video, path, error)) {
                invalid(rows[index], error.message); return;
            }
            rows[index].kind = video.family() == port::VideoFamily::hnm4
                ? "HNM4 animated texture" : video.family() == port::VideoFamily::hnm5
                    ? "UBB2/UBS2 movie" : "HNM6/HNS6 movie";
            rows[index].group = video.family() == port::VideoFamily::hnm4
                ? Group::textures : Group::video;
            rows[index].status = Status::available;
            const auto& header = video.header();
            rows[index].detail = "Magic " + cell(header.data(), 4) + "; " +
                std::to_string(little16(header.data() + 8)) + " x " +
                std::to_string(little16(header.data() + 10)) + "; " +
                std::to_string(little32(header.data() + 16)) + " frames";
            if (little32(header.data() + 12) != video.source_size())
                rows[index].detail += "; declared size differs from file";
            rows[index].provenance = "retail VID_Open; viewer-derived header dimensions/frame count";
        }
    }
};

Catalog::Catalog() = default;
Catalog::~Catalog() = default;

const char* group_name(Group group) {
    static constexpr std::array<const char*, static_cast<size_t>(Group::count)> names{{
        "Projects", "Scenes", "Models & props", "Animations", "Textures", "Video",
        "Audio", "Dialogue", "Interface", "Game data", "Extras", "Source discs"}};
    return names[static_cast<size_t>(group)];
}

const char* status_name(Status status) {
    switch (status) {
    case Status::indexing: return "Indexing";
    case Status::available: return "Available";
    case Status::missing: return "Missing";
    case Status::ambiguous: return "Ambiguous";
    case Status::invalid: return "Invalid";
    case Status::unindexed: return "Unindexed";
    }
    return "Unknown";
}

const char* identity_name(disc::Identity identity) {
    switch (identity) {
    case disc::Identity::disc1: return "Disc 1";
    case disc::Identity::disc2: return "Disc 2";
    case disc::Identity::ambiguous: return "Ambiguous markers";
    case disc::Identity::unknown: return "Unidentified";
    }
    return "Unidentified";
}

bool Catalog::replace(size_t slot, const std::filesystem::path& cue, std::string& error) {
    error.clear();
    if (slot >= states_.size()) { error = "invalid source slot"; return false; }
    disc::Error disc_error;
    auto image = disc::Image::open(cue, disc_error);
    if (!image) { error = disc_error.message; return false; }
    auto state = std::unique_ptr<State>(new State);
    state->image = std::shared_ptr<const disc::Image>(std::move(image));
    state->vfs.reset(new port::VfsContext(state->image));
    const auto& entries = state->image->entries();
    for (const auto& entry : entries) {
        Row row;
        row.parent = entry.parent_index == UINT32_MAX ? SIZE_MAX : entry.parent_index;
        row.physical = true;
        row.path = entry.path;
        row.name = std::string(basename(entry.path));
        row.kind = entry.kind == disc::EntryKind::directory ? "Directory" : "File";
        row.group = entry.kind == disc::EntryKind::directory
            ? Group::source : group_for(entry.path);
        row.extra = row.group == Group::extras;
        row.status = entry.kind == disc::EntryKind::directory
            ? Status::available : has_indexer(entry.path) ? Status::indexing : Status::unindexed;
        row.size = entry.byte_size;
        row.offset = static_cast<uint64_t>(entry.extent_sector) * 2048;
        row.has_extent = true;
        row.provenance = "ISO 9660 source record (viewer portability layer)";
        add(*state, std::move(row));
        if (entry.kind == disc::EntryKind::file) {
            ++state->total_files;
            if (has_indexer(entry.path)) state->jobs.push_back(entry.id.index);
        }
    }
    // BF member registration must precede sprite loads that name BF members.
    std::stable_sort(state->jobs.begin(), state->jobs.end(), [&](size_t a, size_t b) {
        return ends(upper(state->rows[a].path), ".BF") &&
            !ends(upper(state->rows[b].path), ".BF");
    });
    state->indexable_files = state->jobs.size();
    for (const auto& track : state->image->tracks()) {
        if (track.mode != disc::TrackMode::audio) continue;
        Row row;
        row.group = Group::audio;
        row.status = track.status == disc::TrackStatus::available ? Status::available
            : track.status == disc::TrackStatus::missing ? Status::missing : Status::invalid;
        row.name = "CD track " + std::to_string(track.number);
        row.kind = "Audio track";
        row.key = "track:" + std::to_string(track.number);
        row.path = track.cue_filename;
        row.size = track.byte_size;
        row.detail = "Mode: AUDIO; backing: " + track.backing_path.u8string() +
            "; INDEX 00: " + (track.index00_frames
            ? std::to_string(*track.index00_frames) : std::string("none")) +
            " frames; INDEX 01: " + std::to_string(track.index01_frames) +
            " frames. " + track.note;
        row.provenance = "CUE track metadata (viewer portability layer)";
        add(*state, std::move(row));
    }
    state->complete = state->jobs.empty();
    states_[slot] = std::move(state);
    refresh_references();
    return true;
}

void Catalog::unmount(size_t slot) {
    if (slot < states_.size()) {
        states_[slot].reset();
        refresh_references();
    }
}

const Source* Catalog::source(size_t slot) const {
    return slot < states_.size() ? states_[slot].get() : nullptr;
}

const Row* Catalog::row(size_t slot, size_t index) const {
    const auto* source_ptr = source(slot);
    return source_ptr && index < source_ptr->rows.size() ? &source_ptr->rows[index] : nullptr;
}

void Catalog::tick(size_t files_per_source) {
    bool newly_complete = false;
    for (auto& state : states_) {
        if (!state) continue;
        const bool was_complete = state->complete;
        for (size_t i = 0; i < files_per_source && state->next_job < state->jobs.size(); ++i) {
            state->index(state->jobs[state->next_job++]);
            ++state->indexed_files;
        }
        state->complete = state->next_job == state->jobs.size();
        newly_complete |= state->complete && !was_complete;
    }
    if (newly_complete) refresh_references();
}

void Catalog::refresh_references() {
    std::unordered_map<std::string, size_t> candidates;
    bool complete = true;
    for (const auto& state : states_) {
        if (!state) continue;
        complete &= state->complete;
        for (const auto& row : state->rows) {
            if ((row.physical && row.kind != "Directory") || row.kind == "Project" ||
                row.kind == "Dialogue entry" || row.kind == "Audio track")
                ++candidates[upper(row.physical ? basename(row.path) : row.name)];
        }
    }
    for (auto& state : states_) {
        if (!state) continue;
        for (auto& row : state->rows) {
            if (row.detail.compare(0, 8, "Target: ") != 0 || row.detail.size() == 8)
                continue;
            const auto target = upper(basename(std::string_view(row.detail).substr(8)));
            const size_t count = candidates[target];
            row.status = count > 1 ? Status::ambiguous : count == 1 ? Status::available
                : complete ? Status::missing : Status::indexing;
        }
    }
}

std::vector<Candidate> Catalog::resolve(std::string_view target) const {
    std::vector<Candidate> found;
    const std::string key = upper(basename(target));
    if (key.empty()) return found;
    for (size_t slot = 0; slot < states_.size(); ++slot) {
        const auto* source_ptr = source(slot);
        if (!source_ptr) continue;
        for (const auto& row : source_ptr->rows) {
            if ((row.physical && row.kind != "Directory" &&
                 upper(basename(row.path)) == key) ||
                ((row.kind == "Project" || row.kind == "Dialogue entry" ||
                  row.kind == "Audio track") && upper(row.name) == key))
                found.push_back({slot, row.id});
        }
    }
    return found;
}

std::vector<Candidate> Catalog::search(std::string_view text, Group group,
                                       int disc_filter, int status_filter,
                                       bool include_extras, bool source_view) const {
    std::vector<Candidate> found;
    const std::string needle = upper(text);
    for (size_t slot = 0; slot < states_.size(); ++slot) {
        const auto* source_ptr = source(slot);
        if (!source_ptr) continue;
        if (disc_filter && static_cast<int>(source_ptr->image->identity()) != disc_filter)
            continue;
        for (const auto& row : source_ptr->rows) {
            if (!source_view && group != Group::count && row.group != group) continue;
            if (source_view && !row.physical && row.kind != "Audio track" &&
                row.parent == SIZE_MAX) continue;
            if (!include_extras && row.extra) continue;
            if (status_filter && static_cast<int>(row.status) + 1 != status_filter) continue;
            if (!needle.empty() && upper(row.name).find(needle) == std::string::npos &&
                upper(row.path).find(needle) == std::string::npos &&
                upper(row.key).find(needle) == std::string::npos &&
                upper(row.detail).find(needle) == std::string::npos) continue;
            found.push_back({slot, row.id});
        }
    }
    return found;
}

} // namespace od::inspect

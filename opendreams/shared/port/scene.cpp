#include "port/scene.h"

#include "port/dan.h"
#include "port/dsn.h"
#include "port/math.h"
#include "port/player.h"
#include "port/resource.h"
#include "port/stream.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <utility>

namespace od::port {
namespace {

bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}

uint32_t u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
}

std::string cell(const uint8_t* bytes, size_t length) {
    size_t used = 0;
    while (used < length && bytes[used]) ++used;
    return std::string(reinterpret_cast<const char*>(bytes), used);
}

std::string stem(std::string_view name) {
    const size_t dot = name.find_last_of('.');
    return std::string(name.substr(0, dot));
}

std::string basename(std::string_view path) {
    const size_t slash = path.find_last_of("/\\");
    return std::string(path.substr(slash == std::string_view::npos ? 0 : slash + 1));
}

std::string upper_ascii(std::string_view input) {
    std::string result(input);
    for (char& ch : result)
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - ('a' - 'A'));
    return result;
}

void write_cell(uint8_t* destination, size_t capacity, std::string_view text) {
    const size_t length = std::min(capacity - 1, text.size());
    std::memcpy(destination, text.data(), length);
    destination[length] = 0;
}

void append_actor_graph(ModelGraph& scene, const PreviewActor& actor) {
    const size_t node_base=scene.nodes.size();
    for (auto node : actor.model.nodes) {
        if (node.parent>=0) node.parent+=static_cast<int>(node_base);
        scene.nodes.push_back(std::move(node));
    }
    const auto mapping=merge_graph_materials(scene.materials,actor.model.materials);
    const auto append=[&](const std::vector<ModelFace>& from, std::vector<ModelFace>& to) {
        for (auto face : from) {
            face.owner_node+=node_base;
            if (face.material_index<mapping.size())
                face.material_index=mapping[face.material_index];
            for (auto& corner : face.corners) corner.node+=node_base;
            to.push_back(std::move(face));
        }
    };
    append(actor.model.faces,scene.faces);
    append(actor.model.flat_faces,scene.flat_faces);
}

bool image_has(const disc::Image* image, const std::string& path) {
    disc::FileId file;
    disc::Error source;
    return image && image->find(path,file,source);
}

} // namespace

PreviewLevelContext::PreviewLevelContext(std::shared_ptr<const disc::Image> image,
                                        std::shared_ptr<const disc::Image> secondary)
    : scene_image_(image), vfs_(std::move(image)), secondary_vfs_(secondary ?
        std::make_unique<VfsContext>(std::move(secondary)) : nullptr) {}

bool PreviewLevelContext::select_project(std::string_view name,
                                         size_t object_slot, std::string& error) {
    error.clear();
    loaded_ = false;
    pending_load_ = false;
    level_ = {};
    render_ = {};
    actor_ = {};
    actors_.clear(); issues_.clear(); missing_materials_.clear();
    synthetic_scene_ = false;
    project_scene_ = false;
    scene_source_path_.clear();
    model_source_path_.clear();
    selected_model_name_.clear();
    if (object_slot == 0 || object_slot >= 16)
        return fail(error, "select a project object from OBJET1 through OBJET15");
    DdatError ddat_error;
    if (!DDAT_Load(vfs_, bank_, ddat_error)) {
        error = ddat_error.message;
        return false;
    }
    const uint8_t* record = nullptr;
    if (!DDAT_LoadRecord(bank_, name, record, ddat_error) ||
        bank_.used_previous_fallback() || !record) {
        error = ddat_error.message.empty() ? "project record is unavailable"
                                           : ddat_error.message;
        return false;
    }
    std::memcpy(project_record_.data(), record, project_record_.size());
    project_name_ = std::string(name);
    selected_slot_ = object_slot;
    pending_load_ = true;
    return true;
}

bool PreviewLevelContext::select_project_scene(std::string_view name,
                                               std::string& error) {
    if (!select_project(name,1,error)) return false;
    selected_slot_=SIZE_MAX;
    project_scene_=true;
    return true;
}

bool PreviewLevelContext::select_scene(std::string_view physical_path,
                                       std::string& error) {
    error.clear();
    loaded_=false;
    pending_load_=false;
    level_={}; render_={}; actor_={};
    actors_.clear(); issues_.clear(); missing_materials_.clear();
    project_record_.fill(0);
    model_source_path_.clear();
    selected_model_name_.clear();
    const std::string path=upper_ascii(physical_path);
    if (path.compare(0,9,"DATA/3DC/")!=0 ||
        path.size()<14 || path.substr(path.size()-4)!=".DSN")
        return fail(error,"select a physical DATA/3DC/*.DSN scene");
    scene_source_path_=std::string(physical_path);
    project_name_=stem(basename(path));
    selected_slot_=SIZE_MAX;
    synthetic_scene_=false;
    project_scene_=false;
    pending_load_=true;
    return true;
}

bool PreviewLevelContext::select_model(std::string_view physical_path,
                                       std::string_view model_name,
                                       std::string& error) {
    error.clear();
    loaded_ = false;
    pending_load_ = false;
    level_ = {};
    render_ = {};
    actor_ = {};
    actors_.clear(); issues_.clear(); missing_materials_.clear();
    project_record_.fill(0);
    const std::string path = upper_ascii(physical_path);
    const std::string asset = basename(path);
    if (path.compare(0, 9, "DATA/3DC/") != 0 ||
        asset.size() < 5 || asset.size() >= 16 ||
        asset.substr(asset.size() - 4) != ".DAN")
        return fail(error, "select a model name inside a DATA/3DC/*.DAN archive");
    write_cell(project_record_.data(), 16, "Preview");
    uint8_t* scene = project_record_.data() + 0x600;
    write_cell(scene, 12, "OBJET0");
    write_cell(scene + 0xc, 16, "PREVIEW.DSN");
    uint8_t* object = scene + 0xc0;
    write_cell(object, 12, "OBJET1");
    write_cell(object + 0xc, 16, asset);
    object[0x34] = 1; // Active at level entry; character bit clear.
    project_name_ = "Preview";
    model_source_path_ = std::string(physical_path);
    selected_model_name_ = std::string(model_name);
    scene_source_path_.clear();
    selected_slot_ = 1;
    synthetic_scene_ = true;
    project_scene_ = false;
    pending_load_ = true;
    return true;
}

bool load_level_resource(PreviewLevelContext& context, std::string_view path,
                         ModelGraph& graph, std::string& error) {
    const std::string physical(path);
    VfsContext* vfs=&context.vfs_;
    if (!image_has(context.vfs_.image(),physical) && context.secondary_vfs_ &&
        image_has(context.secondary_vfs_->image(),physical))
        vfs=context.secondary_vfs_.get();
    return RES_Load(*vfs,physical,context.cache_,graph,error);
}

bool ENT_InitShadows(PreviewLevelContext& context, std::string& error) {
    error.clear();
    // Both loads are fatal in retail (SYS_FatalError on -1).
    return load_level_resource(context,"DATA/3DC/OMBRE.3DC",context.shadow_models_[0],error) &&
           load_level_resource(context,"DATA/3DC/OMBRE2.3DC",context.shadow_models_[1],error);
}

bool ENT_LoadShotModels(PreviewLevelContext& context, std::string& error) {
    error.clear();
    // RES_RaiseFatalError("pas de tir possible") on -1.
    return load_level_resource(context,"DATA/3DC/GR00.3DC",context.shot_models_[0],error) &&
           load_level_resource(context,"DATA/3DC/BOULE.3DC",context.shot_models_[1],error);
}

bool load_scene_object(PreviewLevelContext& context, const std::string& path,
                       std::string& error) {
    StreamError stream_error;
    auto stream = STRM_Create(context.vfs_, 0x57800, 0x57800, 0x8000, stream_error);
    if (!stream) {
        error = stream_error.message;
        return false;
    }
    DsnState dsn;
    DSN_InitState(dsn, *stream);
    DsnError dsn_error;
    std::vector<uint8_t> geometry, collision;
    std::vector<DsnTexturePage> pages;
    if (!DSN_LoadHeader(dsn, path, dsn_error) ||
        !DSN_LoadMaterialsAndFaces(dsn, geometry, dsn_error) ||
        !DSN_LoadVertexPool(dsn, collision, dsn_error) ||
        !DSN_LoadTextures(dsn,pages,dsn_error)) {
        error = dsn_error.message;
        STRM_Free(stream);
        return false;
    }
    STRM_Free(stream);
    // DSN mode: RES_ReadFile sends every .3DM to DSN_Create3DM. The port
    // decodes all pages up front; only instantiated maps become banks, as
    // DSN_LoadTextures fills only maps with a 0x5df880 page.
    std::vector<uint8_t> created(pages.size());
    MaterialRoute route;
    route.dsn_pages=&pages;
    route.dsn_created=&created;
    route.source=upper_ascii(basename(path));
    if (!RES_Relocate(geometry, context.level_, error) ||
        !MDL_LoadMaterials(context.cache_,route,context.level_,error)) return false;
    for (size_t index=0; index<pages.size(); ++index)
        if (!created[index]) context.unused_maps_.push_back(pages[index].object_name);
    return true;
}

bool SCENE_InitLevel(PreviewLevelContext& context, std::string& error) {
    error.clear();
    // RES_InitArena (0x4568a4): the material cache is level-scoped.
    RES_InitArena(context.cache_);
    context.unused_maps_.clear();
    context.load_gaps_.clear();
    context.shadow_models_ = {};
    context.shot_models_ = {};
    context.player_seed_ = {};
    context.level_ = {};
    if (context.synthetic_scene_) {
        ModelNode root;
        root.local_rot = {32768, 0, 0, 0, 32768, 0, 0, 0, 32768};
        context.level_.nodes.push_back(std::move(root));
        return true;
    }
    // A physical DSN preview is not a level: its cache holds only its own
    // directory (viewer adaptation).
    if (!context.scene_source_path_.empty()) return true;
    if (!ENT_InitShadows(context,error)) return false;
    const std::string selected = cell(context.project_record_.data() + 0x8c, 32);
    // DAT_0049da14 selects mhe.3dc over xh_.3dc; the port has no hero switch.
    const std::string asset = selected.empty() ? "XH_.3DC" : upper_ascii(selected);
    if (context.runtime_player_) {
        PlayerState& player = *context.runtime_player_;
        ANIM_InitStateTable(player);
        if (!ENT_LoadObject(player, context.project_record_.data(), asset,
                            context.cache_, error) ||
            !ANIM_LoadEntitySet(player, error) ||
            !ANIM_RequestState(player, 0, error) ||
            !ANIM_ApplyPendingState(player, error)) return false;
    } else {
        // Preview: the player is not placed, but its materials enter the
        // cache here, before any OBJET slot.
        context.player_seed_.asset_name = asset;
        std::string seed_error;
        if (!ENT_LoadModel(context,context.player_seed_,seed_error))
            context.load_gaps_.push_back("player " + asset + ": " + seed_error);
    }
    return ENT_LoadShotModels(context,error);
}

bool ENT_LoadModel(PreviewLevelContext& context, PreviewActor& actor,
                   std::string& error) {
    error.clear();
    const std::string name = actor.asset_name;
    if (name.size() < 5)
        return fail(error,"project object has no supported model resource");
    const bool dan=name.substr(name.size()-4)==".DAN";
    const bool raw=name.substr(name.size()-4)==".3DC";
    if (!dan && !raw)
        return fail(error,"project object does not reference DAN or 3DC geometry");
    const auto source_for=[&](const std::string& path) -> VfsContext* {
        disc::FileId file;
        disc::Error source;
        if (context.vfs_.image()->find(path,file,source)) return &context.vfs_;
        if (context.secondary_vfs_ &&
            context.secondary_vfs_->image()->find(path,file,source)) {
            actor.from_secondary_source=true;
            return context.secondary_vfs_.get();
        }
        return nullptr;
    };
    if (raw) {
        const std::string path="DATA/3DC/"+name;
        if (VfsContext* physical=source_for(path)) {
            if (!RES_Load(*physical,path,context.cache_,actor.model,error)) return false;
            return MDL_AttachNode(actor,error);
        }
    }
    const std::string path = context.model_source_path_.empty()
        ? "DATA/3DC/" + stem(name) + ".DAN" : context.model_source_path_;
    VfsContext* asset_source=source_for(path);
    if (!asset_source)
        return fail(error,context.secondary_vfs_ ?
            "project model is absent from both mounted sources" :
            "project model is absent from the selected source");
    DanArchive archive(*asset_source);
    DanError dan_error;
    if (!DAN_OpenArchive(archive, path, dan_error)) {
        error = dan_error.message;
        return false;
    }
    if (!context.selected_model_name_.empty()) {
        const std::string selected = upper_ascii(context.selected_model_name_);
        const auto found = std::find_if(archive.names().begin(), archive.names().end(),
            [&](const DanName& entry) { return upper_ascii(entry.text) == selected; });
        if (found == archive.names().end())
            return fail(error, "selected model name is absent from its DAN archive");
    }
    if (!RES_Load(archive,stem(name)+".3DC",context.cache_,actor.model,error)) return false;
    if (!MDL_AttachNode(actor, error)) return false;
    if (!DAN_ReadAnimChunks(archive, dan_error)) {
        error = dan_error.message;
        return false;
    }
    return true;
}

bool ENT_InstantiateFromObjet(PreviewLevelContext& context, size_t slot,
                              std::string& error) {
    error.clear();
    if (!slot || slot >= 16)
        return fail(error, "project object slot is outside OBJET1..15");
    const uint8_t* record = context.project_record_.data() + 0x600 + slot * 0xc0;
    if (std::memcmp(record, "OBJET", 5) != 0 || !(record[0x34] & 1u) ||
        (record[0x35] & 1u))
        return fail(error, "selected project object is not active at level entry");
    PreviewActor actor;
    actor.object_slot = slot;
    actor.asset_name = cell(record + 0xc, 16);
    actor.flags = static_cast<uint16_t>(record[0x34] | (record[0x35] << 8));
    if (!ENT_LoadModel(context, actor, error)) return false;
    context.actor_ = std::move(actor);
    return true;
}

bool MDL_AttachNode(PreviewActor& actor, std::string& error) {
    error.clear();
    if (actor.model.nodes.empty())
        return fail(error, "actor model has no root node to attach");
    actor.model.nodes[0].external_parent_handle = 0;
    actor.attached_to_camera_root = true;
    return true;
}

bool MATH_EulerToMat3(int x, int y, int z,
                      std::array<int32_t, 9>& matrix, std::string& error) {
    error.clear();
    // Retail indexes the tables with the raw 12-bit angles; its callers mask
    // them with 0xfff first. The mask here keeps out-of-range host input
    // inside the tables instead of reading neighbouring retail memory.
    const auto& tables = math_trig_tables();
    const int64_t sx = tables.sin[static_cast<size_t>(x & 4095)];
    const int64_t cx = tables.cos[static_cast<size_t>(x & 4095)];
    const int64_t sy = tables.sin[static_cast<size_t>(y & 4095)];
    const int64_t cy = tables.cos[static_cast<size_t>(y & 4095)];
    const int64_t sz = tables.sin[static_cast<size_t>(z & 4095)];
    const int64_t cz = tables.cos[static_cast<size_t>(z & 4095)];
    // 32-bit IMUL/ADD followed by SAR 15, in 0x45b194's operation order.
    const auto sar15 = [](int64_t value) {
        const uint32_t bits = static_cast<uint32_t>(value);
        int32_t wrapped = 0;
        std::memcpy(&wrapped, &bits, sizeof(bits));
        return wrapped >> 15;
    };
    matrix = {
        sar15(cz * cy + static_cast<int64_t>(sar15(sx * sz)) * sy),
        sar15(-sz * cy + sy * static_cast<int64_t>(sar15(cz * sx))),
        sar15(sy * cx),
        sar15(sz * cx),
        sar15(cz * cx),
        static_cast<int32_t>(-sx),
        sar15(-cz * sy + sx * static_cast<int64_t>(sar15(sz * cy))),
        sar15(sz * sy + static_cast<int64_t>(sar15(cz * cy)) * sx),
        sar15(cy * cx)};
    return true;
}

bool MDL_SetNodeRotation(ModelGraph& graph, size_t node,
                         const std::array<int32_t, 9>& rotation,
                         std::string& error) {
    error.clear();
    if (node >= graph.nodes.size())
        return fail(error, "model rotation target is missing");
    graph.nodes[node].local_rot = rotation;
    return true;
}

bool MDL_SetNodePosition(ModelGraph& graph, size_t node,
                         const std::array<int32_t, 3>& position,
                         std::string& error) {
    error.clear();
    if (node >= graph.nodes.size())
        return fail(error, "model position target is missing");
    graph.nodes[node].local_xyz = position;
    return true;
}

bool ENT_ResetToSpawn(PreviewLevelContext& context, PreviewActor& actor,
                      std::string& error) {
    error.clear();
    if (actor.object_slot >= 16)
        return fail(error, "actor has no source OBJET slot");
    const uint8_t* record = context.project_record_.data() + 0x600 +
                            actor.object_slot * 0xc0;
    for (size_t axis = 0; axis < 3; ++axis)
        actor.spawn_xyz[axis] = static_cast<int32_t>(u32(record + 0x40 + axis * 4));
    actor.spawn_heading = u32(record + 0x5c);
    std::array<int32_t, 9> rotation{};
    if (!MATH_EulerToMat3(0, static_cast<int>(actor.spawn_heading), 0,
                          rotation, error) ||
        !MDL_SetNodeRotation(actor.model, 0, rotation, error) ||
        !MDL_SetNodePosition(actor.model, 0, actor.spawn_xyz, error)) return false;
    return true;
}

bool SCENE_LoadLevel(PreviewLevelContext& context, std::string& error) {
    error.clear();
    if (!context.pending_load_)
        return fail(error, "no project is pending level load");
    context.loaded_ = false;
    context.actors_.clear();
    context.issues_.clear();
    context.missing_materials_.clear();
    context.render_ = {};
    if (!SCENE_InitLevel(context, error)) return false;
    const auto report=[&](const ModelGraph& graph, const std::string& label) {
        const auto lines=unbound_material_report(context.cache_,graph,label);
        context.missing_materials_.insert(context.missing_materials_.end(),
                                          lines.begin(),lines.end());
    };
    if (context.synthetic_scene_) {
        if (!ENT_InstantiateFromObjet(context, context.selected_slot_, error) ||
            !ENT_ResetToSpawn(context, context.actor_, error)) return false;
        report(context.actor_.model,context.actor_.asset_name);
        context.pending_load_ = false;
        context.loaded_ = true;
        return true;
    }
    if (!context.scene_source_path_.empty()) {
        if (!load_scene_object(context,context.scene_source_path_,error)) return false;
        report(context.level_,upper_ascii(basename(context.scene_source_path_)));
        context.render_=context.level_;
        context.pending_load_=false;
        context.loaded_=true;
        return true;
    }
    // Active OBJET1..15 in slot order (DSN mode off), then OBJET0.
    PreviewActor selected;
    bool selected_loaded=false;
    std::string selected_error;
    for (size_t slot=1; slot<16; ++slot) {
        const uint8_t* record=context.project_record_.data()+0x600+slot*0xc0;
        if (std::memcmp(record,"OBJET",5)!=0 || !(record[0x34]&1u) ||
            (record[0x35]&1u)) continue;
        const std::string asset=cell(record+0xc,16);
        std::string object_error;
        if (!ENT_InstantiateFromObjet(context,slot,object_error) ||
            !ENT_ResetToSpawn(context,context.actor_,object_error)) {
            context.issues_.push_back({slot,asset,object_error});
            if (slot==context.selected_slot_) selected_error=object_error;
            continue;
        }
        if (slot==context.selected_slot_) {
            selected=context.actor_;
            selected_loaded=true;
        }
        context.actors_.push_back(std::move(context.actor_));
    }
    const uint8_t* scene_slot = context.project_record_.data() + 0x600;
    if (std::memcmp(scene_slot, "OBJET0", 6) != 0)
        return fail(error, "project OBJET0 is not a scene slot");
    const std::string scene_name = cell(scene_slot + 0xc, 16);
    if (scene_name.size() < 5 || scene_name.substr(scene_name.size() - 4) != ".DSN")
        return fail(error, "project OBJET0 does not name a DSN scene");
    if (!load_scene_object(context,"DATA/3DC/"+scene_name,error)) return false;
    report(context.level_,upper_ascii(scene_name));
    for (const auto& actor : context.actors_) report(actor.model,actor.asset_name);
    if (context.project_scene_) {
        context.render_=context.level_;
        for (const auto& actor : context.actors_) append_actor_graph(context.render_,actor);
        context.actor_={};
    } else {
        if (!selected_loaded) {
            if (selected_error.empty())
                return fail(error, "selected project object is not active at level entry");
            error=selected_error;
            return false;
        }
        context.actor_=std::move(selected);
    }
    context.pending_load_=false;
    context.loaded_=true;
    return true;
}

} // namespace od::port

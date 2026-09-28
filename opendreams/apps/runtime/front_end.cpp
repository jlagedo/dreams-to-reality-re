#include "front_end.h"

#include "port/sound.h"
#include "render/level_materials.h"

#include <imgui.h>
#include <util/sokol_imgui.h>

#include <algorithm>
#include <vector>

namespace od::runtime {
namespace {

constexpr uint64_t repeat_ns = 200000000ull; // 0x28 ticks of the 200 Hz timer.
constexpr int stick_dead_zone = 12000;
// Retail projection distance 407.44 at the 640-pixel width (NDC focal term).
constexpr float retail_focal_x = 2.0f * 407.44f / 640.0f;
int32_t signed_le32(const uint8_t* bytes) {
    const uint32_t value = static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
    return static_cast<int32_t>(value);
}

std::string record_text(const uint8_t* bytes, size_t capacity) {
    size_t length = 0;
    while (length < capacity && bytes[length]) ++length;
    return std::string(reinterpret_cast<const char*>(bytes), length);
}

bool pressed(const bool* keys, SDL_Scancode code) {
    return keys && keys[code];
}

} // namespace

bool FrontEnd::init(std::shared_ptr<const disc::Image> disc1,
                    std::shared_ptr<const disc::Image> disc2, SDL_Window* window,
                    bool skip_intro, bool start_new_game,
                    const std::filesystem::path& save_root, std::string& error) {
    shutdown();
    error.clear();
    if (!disc1 || !window) {
        error = "Disc 1 and a runtime window are needed for the front end";
        return false;
    }
    disc1_ = std::move(disc1);
    disc2_ = std::move(disc2);
    window_ = window;
    vfs_ = std::make_unique<port::VfsContext>(disc1_);
    std::vector<port::BfEntry> members;
    port::VfsError vfs_error;
    if (!port::BF_Mount(*vfs_, "DATA/ICONE/ICONES.BF", members, vfs_error)) {
        error = vfs_error.message;
        shutdown();
        return false;
    }
    sprites_ = std::make_unique<port::SpriteState>(*vfs_);
    port::SpriteError sprite_error;
    // GAME_Init loads HI640, HI480 and HI320 into font slots 0, 1 and 2; the
    // 640x480 menu pages use slots 0 (labels, options) and 1 (save slots).
    if (!port::SPR_LoadIconBanks(*sprites_, sprite_error) ||
        !port::TEXT_LoadFont(*sprites_, 0, "DATA/FONT/HI640.SPR", 0,
                             sprite_error) ||
        !port::TEXT_LoadFont(*sprites_, 1, "DATA/FONT/HI480.SPR", 0,
                             sprite_error)) {
        error = sprite_error.message;
        shutdown();
        return false;
    }
    if (!menu_canvas_.load(*sprites_, error) || !video_.init(error) ||
        !world_renderer_.init(error, 640, 480)) {
        shutdown();
        return false;
    }
    sounds_ = std::make_unique<port::FsbBank>(*vfs_);
    port::FsbError sound_error;
    if (!port::FSB_Load(*sounds_, "DATA/SOUND/FSB.DAT", sound_error))
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "menu effects unavailable: %s",
                    sound_error.message.c_str());
    phase_ = skip_intro || start_new_game ? Phase::menu : Phase::intro;
    const char* movie = phase_ == Phase::menu ? "DATA/HNM/GENERIC.HNM" :
                                                "DATA/HNM/INTRO.HNM";
    if (!video_.open(disc1_, movie, error)) {
        shutdown();
        return false;
    }
    menu_ = {};
    options_ = {};
    saves_ = {};
    saves_.root = save_root;
    std::string index_error;
    // GAME_LoadIndex runs once from UI_InitIcons during GAME_Init.
    if (!port::GAME_LoadIndex(saves_.root, saves_.index, index_error) ||
        !index_error.empty())
        SDL_Log("save index %s: %s", saves_.root.u8string().c_str(),
                index_error.c_str());
    master_volume_ = 127;
    // GAME_Init/GAME_InitSubsystems game-state values; the 3D engine init
    // (0x421345) also runs ENT_ResetInventory once at startup.
    game_ = {};
    port::ENT_ResetInventory(game_);
    wants_quit_ = false;
    previous_frame_ns_ = SDL_GetTicksNS();
    if (start_new_game && !begin_new_game()) {
        error = error_;
        shutdown();
        return false;
    }
    return true;
}

void FrontEnd::open_menu_page(const std::string& page) {
    if (phase_ != Phase::menu || page.empty()) return;
    menu_.selected = page == "load" ? 1 : 2;
    int sound_id = -1, master_volume = -1;
    port::MENU_Tick(menu_, options_, saves_, port::MenuAction::confirm, sound_id,
                    master_volume, &save_lines_);
}

void FrontEnd::update_devices() {
    if (gamepad_ && !SDL_GamepadConnected(gamepad_)) {
        SDL_CloseGamepad(gamepad_);
        gamepad_ = nullptr;
    }
    if (joystick_ && !SDL_JoystickConnected(joystick_)) {
        SDL_CloseJoystick(joystick_);
        joystick_ = nullptr;
    }
    if (!gamepad_) {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (ids && count > 0) gamepad_ = SDL_OpenGamepad(ids[0]);
        SDL_free(ids);
    }
    if (gamepad_ && joystick_) {
        SDL_CloseJoystick(joystick_);
        joystick_ = nullptr;
    }
    if (!gamepad_ && !joystick_) {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetJoysticks(&count);
        for (int i = 0; ids && i < count && !joystick_; ++i)
            if (!SDL_IsGamepad(ids[i])) joystick_ = SDL_OpenJoystick(ids[i]);
        SDL_free(ids);
    }
}

port::MenuAction FrontEnd::poll_action() {
    if (SDL_GetKeyboardFocus() != window_) {
        previous_confirm_ = false;
        previous_cancel_ = false;
        held_direction_ = port::MenuAction::none;
        return port::MenuAction::none;
    }
    update_devices();
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const bool up_key = pressed(keys, SDL_SCANCODE_UP);
    const bool down_key = pressed(keys, SDL_SCANCODE_DOWN);
    const bool left_key = pressed(keys, SDL_SCANCODE_LEFT);
    const bool right_key = pressed(keys, SDL_SCANCODE_RIGHT);
    bool confirm = pressed(keys, SDL_SCANCODE_SPACE) ||
                   pressed(keys, SDL_SCANCODE_RETURN);
    bool cancel = pressed(keys, SDL_SCANCODE_ESCAPE);
    bool up = up_key, down = down_key, left = left_key, right = right_key;
    if (gamepad_) {
        up |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_UP);
        down |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        left |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        right |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        const int x = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX);
        const int y = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY);
        left |= x < -stick_dead_zone; right |= x > stick_dead_zone;
        up |= y < -stick_dead_zone; down |= y > stick_dead_zone;
        confirm |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_SOUTH) ||
                   SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_START);
        cancel |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_EAST) ||
                  SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_BACK);
    } else if (joystick_) {
        const int x = SDL_GetJoystickAxis(joystick_, 0);
        const int y = SDL_GetJoystickAxis(joystick_, 1);
        left |= x < -stick_dead_zone; right |= x > stick_dead_zone;
        up |= y < -stick_dead_zone; down |= y > stick_dead_zone;
        confirm |= SDL_GetJoystickButton(joystick_, 0);
        cancel |= SDL_GetJoystickButton(joystick_, 1);
    }
    const bool confirm_edge = confirm && !previous_confirm_;
    const bool cancel_edge = cancel && !previous_cancel_;
    previous_confirm_ = confirm;
    previous_cancel_ = cancel;
    if (cancel_edge) return port::MenuAction::cancel;
    if (confirm_edge) return port::MenuAction::confirm;
    const port::MenuAction direction = left && !right ? port::MenuAction::left :
        right && !left ? port::MenuAction::right :
        up && !down ? port::MenuAction::up :
        down && !up ? port::MenuAction::down : port::MenuAction::none;
    const uint64_t now = SDL_GetTicksNS();
    if (direction != held_direction_) {
        held_direction_ = direction;
        next_repeat_ns_ = now + repeat_ns;
        return direction;
    }
    if (direction != port::MenuAction::none && now >= next_repeat_ns_) {
        next_repeat_ns_ = now + repeat_ns;
        return direction;
    }
    return port::MenuAction::none;
}

bool FrontEnd::enter_menu() {
    error_.clear();
    video_.close();
    if (!video_.open(disc1_, "DATA/HNM/GENERIC.HNM", error_)) return false;
    phase_ = Phase::menu;
    held_direction_ = port::MenuAction::none;
    menu_.dirty = true;
    return true;
}

bool FrontEnd::begin_new_game() {
    error_.clear();
    first_project_ = std::make_unique<port::PreviewLevelContext>(disc1_, disc2_);
    if (!first_project_->select_project_scene("Project0", error_)) return false;
    const auto& record = first_project_->project_record();
    const std::string project_movie = record_text(record.data() + 0x3c, 32);
    disc::FileId file;
    disc::Error source_error;
    const bool movie_available = !project_movie.empty() && disc1_->find(
        "DATA/HNM/" + project_movie, file, source_error);
    if (!project_movie.empty() && !movie_available)
        SDL_Log("Project0 optional movie %s is absent; retail open also fails",
                project_movie.c_str());
    port::BOOT_ResetNewGame(game_);
    port::BOOT_BeginNewGame(entry_, project_movie, movie_available);
    // New Game keeps the elder latch (0x49da28); only a death restart re-arms it.
    entry_.elder_latch = game_.elder_latch != 0;
    phase_ = Phase::new_game;
    notice_.clear();
    previous_frame_ns_ = SDL_GetTicksNS();
    return true;
}

bool FrontEnd::load_first_project() {
    video_.close();
    // SCENE_InitLevel sends 0x1f; SCENE_LoadLevel clears the music latch.
    port::CD_StopMusic(playlist_, music_);
    level_music_started_ = false;
    player_ = std::make_unique<port::PlayerState>(disc1_);
    if (first_project_) first_project_->set_runtime_player(player_.get());
    if (!first_project_ || !port::SCENE_LoadLevel(*first_project_, error_))
        return false;
    const auto& record = first_project_->project_record();
    if (!port::PLAYER_ComposeRenderGraph(*player_,
            first_project_->render_graph(), world_graph_, player_node_base_,
            error_) ||
        !world_renderer_.load(world_graph_, error_)) return false;
    // SCENE_LoadLevel palette setup and DREAMSFX SCENE_SetFog. The player
    // (loaded first by SCENE_InitLevel) is the one palette-bound actor.
    {
        auto setup = port::level_material_setup(*first_project_,
            first_project_->render_graph().materials.size(),
            player_->model().materials.size());
        port::PaletteActor player_palette;
        const std::string asset(player_->asset_name());
        player_palette.name = asset.substr(0, asset.find('.'));
        player_palette.adapts = true;
        setup.actors.push_back(player_palette);
        if (!world_materials_.start(world_graph_, setup, error_)) return false;
        sync_level_materials(world_materials_, world_renderer_);
    }
    const std::array<int32_t, 3> spawn{{
        signed_le32(record.data() + 0xb4),
        signed_le32(record.data() + 0xb8),
        signed_le32(record.data() + 0xbc)
    }};
    port::FollowCamera camera;
    port::CAM_StartFollow(camera, spawn,
        signed_le32(record.data() + 0x10c), record.data());
    const auto eye = world_renderer_.world_to_view(camera.eye);
    const auto target = world_renderer_.world_to_view(camera.target);
    world_view_ = {};
    world_view_.explicit_eye = true;
    for (size_t axis = 0; axis < 3; ++axis) {
        world_view_.eye[axis] = eye[axis];
        world_view_.target[axis] = target[axis];
    }
    world_view_.near_plane = std::max(0.00001f,
                                      world_renderer_.scaled_distance(140.0f));
    world_view_.far_plane = std::max(world_view_.near_plane + 1.0f,
                                     world_renderer_.scaled_distance(1048575.0f));
    port::GAME_EntryLoaded(entry_);
    phase_ = Phase::world;
    SDL_Log("Project0 loaded: %zu scene faces, %zu placed actors, %zu unavailable actors; player %s, %zu faces, %zu clips, state 0 %s",
            first_project_->level_graph().faces.size(),
            first_project_->placed_actors().size(),
            first_project_->object_issues().size(),
            player_->asset_name().data(), player_->model().faces.size(),
            player_->clip_count(), player_->active_clip_name().data());
    return true;
}

bool FrontEnd::handle_entry_event(port::GameEntryEvent event) {
    switch (event) {
    case port::GameEntryEvent::stop_menu:
        // MGM 0x19 closes the movie, then MENU_Draw and VID_Swap present one
        // more frame (the last movie picture under the menu) before BOOT_Run
        // clears both pages to black.
        video_.close();
        menu_farewell_ = true;
        return true;
    case port::GameEntryEvent::start_project_movie:
        video_.close();
        return video_.open(disc1_, "DATA/HNM/" + entry_.project_movie, error_);
    case port::GameEntryEvent::start_elder_movie:
        game_.elder_latch = 0;
        video_.close();
        return video_.open(disc1_, "DATA/HNM/TETE_E~1.HNM", error_);
    case port::GameEntryEvent::load_level:
        return load_first_project();
    default:
        return true;
    }
}

void FrontEnd::draw_world(ImDrawList* list) {
    if (!world_renderer_.has_model()) return;
    // Runtime destination: the whole drawable in physical pixels. The retail
    // 640-wide focal length is kept and the vertical term follows the aspect.
    const ImGuiIO& io = ImGui::GetIO();
    const int width = std::max(1, static_cast<int>(
        io.DisplaySize.x * io.DisplayFramebufferScale.x + 0.5f));
    const int height = std::max(1, static_cast<int>(
        io.DisplaySize.y * io.DisplayFramebufferScale.y + 0.5f));
    // Cinemascope (0x49d9f8, on by default): GAME_DrawFrame blanks the top
    // and bottom h/8 rows and the level viewport is the middle 3/4. The
    // horizontal focal length is kept across both modes (assumption: the
    // retail projection centre follows the viewport).
    const bool bands = options_.cinemascope == 1;
    const int band = bands ? height / 8 : 0;
    const int view_height = std::max(1, height - 2 * band);
    if (!world_renderer_.resize_target(width, view_height, error_)) {
        wants_quit_ = true;
        return;
    }
    world_renderer_.draw(with_horizontal_focal(world_view_, retail_focal_x,
                                               width, view_height));
    const float top = io.DisplaySize.y * static_cast<float>(band) /
                      static_cast<float>(height);
    list->AddImage(simgui_imtextureid(world_renderer_.texture_view()), {0, top},
                   {io.DisplaySize.x, io.DisplaySize.y - top});
}

void FrontEnd::set_master_volume(int volume) {
    // DirectSound voices and the 22 kHz movie stream; MCI CD music is not a
    // DirectSound channel and keeps its own volume.
    AudioOutput* channels[] = {&effect_, &video_.audio()};
    port::DSOUND_SetMasterVolume(master_volume_, volume, channels,
                                 sizeof(channels) / sizeof(channels[0]));
}

// The GAME_Tick gameplay-branch helper at 0x42f166 (no checked name): once
// per level load, a nonzero project +0x11c byte becomes a one-track playlist
// (MGM 0x1e); MGM_DispatchMessages then pumps CD_TickPlaylist. Project +0x1fc
// selects the level disc as (level + 1) >> 1.
bool FrontEnd::tick_level_music() {
    if (!first_project_) return true;
    const auto& record = first_project_->project_record();
    if (!level_music_started_) {
        level_music_started_ = true;
        const uint32_t track = record[0x11c];
        if (track) port::CD_SetPlaylist(playlist_, track, music_);
    }
    const int32_t level = signed_le32(record.data() + 0x1fc);
    const auto& image = (level + 1) >> 1 == 2 ? disc2_ : disc1_;
    if (!image) return true;
    std::string music_error;
    if (!port::CD_TickPlaylist(playlist_, image, music_, music_error)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "level music: %s", music_error.c_str());
        port::CD_StopMusic(playlist_, music_);
    }
    return true;
}

void FrontEnd::draw_menu_pages(ImDrawList* list, ImVec2 origin, float scale) {
    menu_canvas_.draw(list, menu_, origin, scale);
    if (menu_.submenu == 1 && menu_.selected == 2)
        for (const auto& line : port::MENU_DrawOptionsPage(options_))
            menu_canvas_.draw_text(list, line, origin, scale);
    if (menu_.submenu == 1 && menu_.selected == 1)
        for (const auto& line : save_lines_)
            menu_canvas_.draw_text(list, line, origin, scale);
}

void FrontEnd::play_sound(int sound_id) {
    if (!sounds_ || !sounds_->loaded() || sound_id < 0) return;
    bool repaired = false;
    std::string error;
    if (!port::DSOUND_PlaySound(*sounds_, static_cast<size_t>(sound_id),
                                effect_, repaired, error))
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "menu effect %d: %s", sound_id,
                    error.c_str());
}

void FrontEnd::draw() {
    if (!disc1_ || wants_quit_) return;
    const uint64_t now = SDL_GetTicksNS();
    const double elapsed_seconds = previous_frame_ns_ && now >= previous_frame_ns_
        ? static_cast<double>(now - previous_frame_ns_) / 1000000000.0 : 0.0;
    previous_frame_ns_ = now;
    port::MenuAction action = poll_action();
    if (phase_ == Phase::intro &&
        (action == port::MenuAction::confirm || action == port::MenuAction::cancel)) {
        if (!enter_menu()) wants_quit_ = true;
        action = port::MenuAction::none;
    }
    const bool movie_phase = phase_ == Phase::intro || phase_ == Phase::menu ||
        (phase_ == Phase::new_game &&
         (entry_.phase == port::GameEntryPhase::menu_hold ||
          entry_.phase == port::GameEntryPhase::project_movie ||
          entry_.phase == port::GameEntryPhase::elder_movie));
    if (!wants_quit_ && movie_phase && !video_.tick(error_)) wants_quit_ = true;
    if (!wants_quit_ && video_.ended() &&
        (phase_ == Phase::intro || phase_ == Phase::menu ||
         (phase_ == Phase::new_game &&
          entry_.phase == port::GameEntryPhase::menu_hold))) {
        if (phase_ == Phase::intro) {
            if (!enter_menu()) wants_quit_ = true;
        } else if (!video_.restart(error_)) wants_quit_ = true;
    }
    // MENU_Tick runs every pump iteration so page timers (the save-slot blink)
    // advance without input.
    if (!wants_quit_ && phase_ == Phase::menu) {
        int sound_id = -1;
        int master_volume = -1;
        const port::MenuChoice choice = port::MENU_Tick(
            menu_, options_, saves_, action, sound_id, master_volume, &save_lines_);
        play_sound(sound_id);
        if (master_volume >= 0) set_master_volume(master_volume);
        // Host-only: GAME_LoadGame is not ported, so retail's failure branch
        // leaves the page open; say why instead of staying silent.
        notice_ = saves_.page.restore_unavailable && menu_.submenu == 1 ?
            "Restoring a saved game is not available yet" : std::string();
        switch (choice) {
        case port::MenuChoice::quit: wants_quit_ = true; break;
        case port::MenuChoice::new_game:
            if (!begin_new_game()) wants_quit_ = true;
            break;
        default: break;
        }
    }
    if (!wants_quit_ && phase_ == Phase::new_game) {
        const port::GameEntryEvent event = port::GAME_TickEntry(
            entry_, elapsed_seconds, video_.ended());
        if (!handle_entry_event(event)) wants_quit_ = true;
    }
    if (!wants_quit_ && phase_ == Phase::world) tick_level_music();
    if (!wants_quit_ && phase_ == Phase::world && player_ &&
        (!port::ANIM_TickPlayerIdle(*player_, elapsed_seconds, error_) ||
         !port::PLAYER_UpdateRenderGraph(*player_, world_graph_,
                                          player_node_base_, error_) ||
         !world_renderer_.update_pose(world_graph_, error_)))
        wants_quit_ = true;
    // 0x42f024 and the HNM4 material on the fixed 30 Hz game tick.
    if (!wants_quit_ && phase_ == Phase::world) {
        if (!world_materials_.advance(elapsed_seconds, error_)) wants_quit_ = true;
        sync_level_materials(world_materials_, world_renderer_);
    }
    if (wants_quit_) return;
    if (movie_phase) video_.upload();
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    list->AddRectFilled({0, 0}, display, IM_COL32(0, 0, 0, 255));
    const float scale = std::min(display.x / 640.0f, display.y / 480.0f);
    const ImVec2 origin{(display.x - 640.0f * scale) * 0.5f,
                        (display.y - 480.0f * scale) * 0.5f};
    // BOOT_TickFrame keeps drawing the menu over the movie during the New
    // Game hold.
    const bool menu_hold = phase_ == Phase::new_game &&
        entry_.phase == port::GameEntryPhase::menu_hold;
    if (phase_ == Phase::world) draw_world(list);
    else if ((movie_phase && video_.has_image()) || menu_farewell_)
        list->AddImage(simgui_imtextureid(video_.texture_view()), origin,
                       {origin.x + 640.0f * scale, origin.y + 480.0f * scale});
    if (phase_ == Phase::menu || menu_hold || menu_farewell_)
        draw_menu_pages(list, origin, scale);
    menu_farewell_ = false;
    if (!notice_.empty())
        list->AddText({origin.x + 20.0f * scale, origin.y + 440.0f * scale},
                      IM_COL32(255, 255, 255, 255), notice_.c_str());
}

void FrontEnd::shutdown() {
    effect_.stop();
    port::CD_StopMusic(playlist_, music_);
    playlist_ = {};
    level_music_started_ = false;
    video_.shutdown();
    world_renderer_.shutdown();
    menu_canvas_.shutdown();
    sounds_.reset();
    sprites_.reset();
    vfs_.reset();
    disc1_.reset();
    disc2_.reset();
    first_project_.reset();
    player_.reset();
    world_materials_.clear();
    world_graph_ = {};
    player_node_base_ = 0;
    entry_ = {};
    game_ = {};
    window_ = nullptr;
    if (gamepad_) SDL_CloseGamepad(gamepad_);
    if (joystick_) SDL_CloseJoystick(joystick_);
    gamepad_ = nullptr;
    joystick_ = nullptr;
    options_ = {};
    saves_ = {};
    save_lines_.clear();
    error_.clear();
    notice_.clear();
}

} // namespace od::runtime

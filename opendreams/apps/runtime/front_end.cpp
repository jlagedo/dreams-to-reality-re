#include "front_end.h"

#include "port/sound.h"

#include <imgui.h>
#include <util/sokol_imgui.h>

#include <algorithm>
#include <vector>

namespace od::runtime {
namespace {

constexpr uint64_t repeat_ns = 200000000ull; // 0x28 ticks of the 200 Hz timer.
constexpr int stick_dead_zone = 12000;
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
                    std::string& error) {
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
    if (!port::SPR_LoadIconBanks(*sprites_, sprite_error) ||
        !port::TEXT_LoadFont(*sprites_, 0, "DATA/FONT/HI640.SPR", 0,
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
    wants_quit_ = false;
    previous_frame_ns_ = SDL_GetTicksNS();
    if (start_new_game && !begin_new_game()) {
        error = error_;
        shutdown();
        return false;
    }
    return true;
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
    port::BOOT_BeginNewGame(entry_, project_movie, movie_available);
    phase_ = Phase::new_game;
    notice_.clear();
    previous_frame_ns_ = SDL_GetTicksNS();
    return true;
}

bool FrontEnd::load_first_project() {
    video_.close();
    player_ = std::make_unique<port::PlayerState>(disc1_);
    if (first_project_) first_project_->set_runtime_player(player_.get());
    if (!first_project_ || !port::SCENE_LoadLevel(*first_project_, error_))
        return false;
    const auto& record = first_project_->project_record();
    if (!port::PLAYER_ComposeRenderGraph(*player_,
            first_project_->render_graph(), world_graph_, player_node_base_,
            error_) ||
        !world_renderer_.load(world_graph_, error_)) return false;
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
    world_view_.focal_x = 2.0f * 407.44f / 640.0f;
    world_view_.focal_y = 2.0f * 407.44f / 480.0f;
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
        video_.close();
        return true;
    case port::GameEntryEvent::start_project_movie:
        video_.close();
        return video_.open(disc1_, "DATA/HNM/" + entry_.project_movie, error_);
    case port::GameEntryEvent::start_elder_movie:
        video_.close();
        return video_.open(disc1_, "DATA/HNM/TETE_E~1.HNM", error_);
    case port::GameEntryEvent::load_level:
        return load_first_project();
    default:
        return true;
    }
}

void FrontEnd::draw_world(ImDrawList* list, ImVec2 origin, float scale) {
    if (!world_renderer_.has_model()) return;
    world_renderer_.draw(world_view_);
    list->AddImage(simgui_imtextureid(world_renderer_.texture_view()), origin,
                   {origin.x + 640.0f * scale, origin.y + 480.0f * scale});
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
    if (!wants_quit_ && phase_ == Phase::menu && action != port::MenuAction::none) {
        int sound_id = -1;
        const port::MenuChoice choice = port::MENU_Tick(menu_, action, sound_id);
        play_sound(sound_id);
        switch (choice) {
        case port::MenuChoice::quit: wants_quit_ = true; break;
        case port::MenuChoice::new_game:
            if (!begin_new_game()) wants_quit_ = true;
            break;
        case port::MenuChoice::load_game:
            notice_ = "LOAD A GAME: save browser is not ported yet"; break;
        case port::MenuChoice::options:
            notice_ = "OPTIONS: options page is not ported yet"; break;
        default: if (sound_id == 9) notice_.clear(); break;
        }
    }
    if (!wants_quit_ && phase_ == Phase::new_game) {
        const port::GameEntryEvent event = port::GAME_TickEntry(
            entry_, elapsed_seconds, video_.ended());
        if (!handle_entry_event(event)) wants_quit_ = true;
    }
    if (!wants_quit_ && phase_ == Phase::world && player_ &&
        (!port::ANIM_TickPlayerIdle(*player_, elapsed_seconds, error_) ||
         !port::PLAYER_UpdateRenderGraph(*player_, world_graph_,
                                          player_node_base_, error_) ||
         !world_renderer_.update_pose(world_graph_, error_)))
        wants_quit_ = true;
    if (wants_quit_) return;
    if (movie_phase) video_.upload();
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    list->AddRectFilled({0, 0}, display, IM_COL32(0, 0, 0, 255));
    const float scale = std::min(display.x / 640.0f, display.y / 480.0f);
    const ImVec2 origin{(display.x - 640.0f * scale) * 0.5f,
                        (display.y - 480.0f * scale) * 0.5f};
    if (phase_ == Phase::world) draw_world(list, origin, scale);
    else if (movie_phase && video_.has_image())
        list->AddImage(simgui_imtextureid(video_.texture_view()), origin,
                       {origin.x + 640.0f * scale, origin.y + 480.0f * scale});
    if (phase_ == Phase::menu) menu_canvas_.draw(list, menu_, origin, scale);
    if (!notice_.empty())
        list->AddText({origin.x + 20.0f * scale, origin.y + 440.0f * scale},
                      IM_COL32(255, 255, 255, 255), notice_.c_str());
}

void FrontEnd::shutdown() {
    effect_.stop();
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
    world_graph_ = {};
    player_node_base_ = 0;
    entry_ = {};
    window_ = nullptr;
    if (gamepad_) SDL_CloseGamepad(gamepad_);
    if (joystick_) SDL_CloseJoystick(joystick_);
    gamepad_ = nullptr;
    joystick_ = nullptr;
    error_.clear();
    notice_.clear();
}

} // namespace od::runtime

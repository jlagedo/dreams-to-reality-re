#pragma once

#include "audio/audio_output.h"
#include "disc/image.h"
#include "port/boot_menu.h"
#include "port/camera.h"
#include "port/fsb.h"
#include "port/game_entry.h"
#include "port/player.h"
#include "port/scene.h"
#include "port/sprite.h"
#include "port/vfs.h"
#include "render/boot_menu.h"
#include "render/model_preview.h"
#include "render/video_preview.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace od::runtime {

class FrontEnd {
public:
    bool init(std::shared_ptr<const disc::Image> disc1,
              std::shared_ptr<const disc::Image> disc2, SDL_Window* window,
              bool skip_intro, bool start_new_game,
              std::string& error);
    void draw();
    void shutdown();
    bool wants_quit() const { return wants_quit_; }
    const std::string& error() const { return error_; }

private:
    enum class Phase { intro, menu, new_game, world };
    void update_devices();
    port::MenuAction poll_action();
    bool enter_menu();
    bool begin_new_game();
    bool handle_entry_event(port::GameEntryEvent event);
    bool load_first_project();
    void draw_world(ImDrawList* list, ImVec2 origin, float scale);
    void play_sound(int sound_id);

    std::shared_ptr<const disc::Image> disc1_;
    std::shared_ptr<const disc::Image> disc2_;
    std::unique_ptr<port::VfsContext> vfs_;
    std::unique_ptr<port::SpriteState> sprites_;
    std::unique_ptr<port::FsbBank> sounds_;
    BootMenuCanvas menu_canvas_;
    VideoPreview video_;
    ModelPreview world_renderer_;
    ModelView world_view_;
    std::unique_ptr<port::PreviewLevelContext> first_project_;
    std::unique_ptr<port::PlayerState> player_;
    port::ModelGraph world_graph_;
    size_t player_node_base_ = 0;
    port::GameEntryState entry_;
    uint64_t previous_frame_ns_ = 0;
    AudioOutput effect_;
    SDL_Gamepad* gamepad_ = nullptr;
    SDL_Joystick* joystick_ = nullptr;
    SDL_Window* window_ = nullptr;
    port::BootMenuState menu_;
    Phase phase_ = Phase::intro;
    port::MenuAction held_direction_ = port::MenuAction::none;
    uint64_t next_repeat_ns_ = 0;
    bool previous_confirm_ = false;
    bool previous_cancel_ = false;
    bool wants_quit_ = false;
    std::string notice_;
    std::string error_;
};

} // namespace od::runtime

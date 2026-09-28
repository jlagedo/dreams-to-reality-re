#pragma once

#include "audio/audio_output.h"
#include "disc/image.h"
#include "port/boot_menu.h"
#include "port/camera.h"
#include "port/fsb.h"
#include "port/game_entry.h"
#include "port/game_state.h"
#include "port/level_materials.h"
#include "port/player.h"
#include "port/scene.h"
#include "port/sound.h"
#include "port/sprite.h"
#include "port/vfs.h"
#include "render/boot_menu.h"
#include "render/model_preview.h"
#include "render/video_preview.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace od::runtime {

class FrontEnd {
public:
    bool init(std::shared_ptr<const disc::Image> disc1,
              std::shared_ptr<const disc::Image> disc2, SDL_Window* window,
              bool skip_intro, bool start_new_game,
              const std::filesystem::path& save_root, std::string& error);
    // Development check: open the Load or Options page as a confirm would.
    void open_menu_page(const std::string& page);
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
    void draw_world(ImDrawList* list);
    void draw_menu_pages(ImDrawList* list, ImVec2 origin, float scale);
    void play_sound(int sound_id);
    void set_master_volume(int volume);
    bool tick_level_music();

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
    // Palette lighting, fog and the HNM4 material of the world graph.
    port::LevelMaterialSession world_materials_;
    size_t player_node_base_ = 0;
    port::GameEntryState entry_;
    port::GameState game_;
    uint64_t previous_frame_ns_ = 0;
    AudioOutput effect_;
    AudioOutput music_;
    port::CdPlaylist playlist_;
    bool level_music_started_ = false; // 0x4a0fd0, cleared by SCENE_LoadLevel
    int master_volume_ = 127;          // 0x633bd0 after DSOUND_Init
    port::FrontEndOptions options_;
    port::FrontEndSaves saves_;
    std::vector<port::MenuTextLine> save_lines_;
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
    bool menu_farewell_ = false;
    std::string notice_;
    std::string error_;
};

} // namespace od::runtime

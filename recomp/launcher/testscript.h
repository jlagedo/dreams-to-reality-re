// TEST ONLY: a scripted driver for the launcher window.
//
// DREAMS_LAUNCHER_SCRIPT=<file> makes the window run a text script, one step per
// frame (with settle frames between steps), through the same paths a person
// uses: synthetic SDL key, text, mouse and drop events pushed with
// SDL_PushEvent (so ImGui's SDL3 backend and the launcher's own event code see
// them), a virtual SDL gamepad, and the file dialog's own callback. It reads
// launcher state through "facts" the window publishes each frame, and fails the
// run with "script:<line>: <message>". With the variable unset none of this
// exists: the window never creates a TestScript. The grammar is documented in
// launcher.h, next to the other test-only variables.
//
// This file knows SDL and the standard library only (no ImGui, no model): the
// window feeds it rectangles and facts, and it feeds events back.
#ifndef LAUNCHER_TESTSCRIPT_H
#define LAUNCHER_TESTSCRIPT_H

#include <SDL3/SDL.h>

#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

class TestScript {
public:
    // Reads and checks the script's syntax. nullptr with *error ("script:<line>: ...") otherwise.
    static std::unique_ptr<TestScript> load(const std::string& path, std::string* error);
    ~TestScript();

    // Called once when the window exists. pick delivers what a file dialog would
    // report to the window (the same callback SDL_ShowOpenFileDialog calls), from
    // whatever thread the script chooses: list is SDL's NULL-terminated array, or NULL.
    using PickFn = std::function<void(int row, const char* const* list)>;
    // padfn tells the window which SDL gamepad ImGui should read (the script's virtual one, or none).
    using PadFn = std::function<void(SDL_Gamepad*)>;
    void attach(SDL_Window* window, bool gamepad_subsystem, PickFn pick, PadFn padfn);

    // True for an event the script pushed. A scripted run ignores every other
    // keyboard, text and mouse event, so a person at the machine cannot disturb it.
    static bool is_synthetic(const SDL_Event& e);
    static bool is_real_input(const SDL_Event& e);  // keyboard, text, mouse: what a person produces

    // The scripted pointer, once the script has moved it (the window re-applies it
    // every frame: ImGui's SDL backend otherwise follows the real pointer).
    bool pointer(float* x, float* y) const;

    // The window, each frame: begin_frame before drawing, widget() and fact() while
    // drawing, end_frame after the frame is rendered (this runs the script).
    void begin_frame();
    void widget(const std::string& id, float x0, float y0, float x1, float y1, bool focused, bool visible);
    void fact(const std::string& name, const std::string& value);
    void end_frame();

    bool failed() const { return !error_.empty(); }
    const std::string& error() const { return error_; }
    bool finished() const { return pc_ >= steps_.size() && micro_.empty(); }
    // Called when the window has closed: an error if the script still had steps to run.
    void window_closed();
    // A screenshot requested by `shot`, to be taken from the frame just rendered.
    bool take_shot(std::string* path);
    bool pad_attached() const { return joystick_ != nullptr; }

private:
    struct Step {
        int line;
        std::vector<std::string> args;  // args[0] is the step name
    };
    struct Widget {
        float x0, y0, x1, y1;
        bool focused, visible;
    };
    struct Micro {
        std::function<void()> run;
        int frames;  // frames to wait after it
    };

    std::vector<Step> steps_;
    size_t pc_ = 0;
    int wait_ = 0;
    int settle_ = 3;
    int idle_ = 0;
    int line_ = 0;  // line of the step being run
    std::deque<Micro> micro_;
    std::string error_;
    std::string shot_;

    SDL_Window* window_ = nullptr;
    bool gamepad_subsystem_ = false;
    PickFn pick_;
    PadFn padfn_;
    SDL_Gamepad* gamepad_ = nullptr;
    std::map<std::string, Widget> widgets_;
    std::map<std::string, std::string> facts_;
    std::list<std::string> keep_;  // strings an event points into, until the script is destroyed
    float mx_ = 0, my_ = 0;
    bool have_pointer_ = false;
    SDL_Keymod mods_ = SDL_KMOD_NONE;
    SDL_Joystick* joystick_ = nullptr;
    SDL_JoystickID joystick_id_ = 0;
    std::vector<SDL_Thread*> threads_;

    bool fail(const std::string& msg);
    void run(const Step& s);
    int number(const std::string& s, const char* what, int lo, int hi, bool* ok);
    void push(SDL_Event& e);
    void key_event(int scancode, bool down);
    void mouse_move(float x, float y);
    void mouse_button(bool down);
    const char* store(const std::string& s);
    bool find_widget(const std::string& id, Widget* out);
    bool click(const Step& s);
    bool key_step(const Step& s, bool down, bool up);
    bool drop(const Step& s);
    bool dialog(const Step& s);
    bool expect(const Step& s);
    bool pad(const Step& s);
    static int pick_thread(void* data);
};

#endif

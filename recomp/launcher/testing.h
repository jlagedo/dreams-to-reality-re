// The launcher's test-only machinery, behind one door.
//
// A build made with the CMake option DREAMS_LAUNCHER_TESTING (default ON; the
// game's release build forces it off, recomp/windream/CMakeLists.txt WD_RELEASE)
// defines DREAMS_LAUNCHER_TESTING and compiles testscript.cpp: the text-script
// driver and the test-only environment variables, all named in launcher.h.
// Any other build gets the empty inline stand-ins below, so a call site is one
// line with no #ifdef and none of the test-only variables is ever read: their
// names exist in testscript.cpp only.
//
// The compiled-in code carries the text "launcher-testing" (testscript.cpp,
// kMarker); recomp/windream/release.py refuses an executable that holds it.
//
// To remove the feature: delete testscript.cpp/.h and this header's ON half, the
// DREAMS_LAUNCHER_TESTING block in CMakeLists.txt, the `testing::kEnabled`
// branches in launcher.cpp and ui.cpp, and tests/recomp/test_launcher_ui.py.
#ifndef LAUNCHER_TESTING_H
#define LAUNCHER_TESTING_H

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <string>

namespace testing {

// The test-only environment variables. Their names are in testscript.cpp.
enum class Var {
    Home,     // DREAMS_LAUNCHER_HOME: the folder of dreams.ini and userdata
    Verbose,  // DREAMS_LAUNCHER_VERBOSE: print each disc's status line in --play
    Shot,     // DREAMS_LAUNCHER_SHOT: save one frame to this BMP and quit
    ShotTab,  // DREAMS_LAUNCHER_SHOT_TAB: the tab that frame shows
    Script,   // DREAMS_LAUNCHER_SCRIPT: a text script that drives the window
};

}  // namespace testing

#ifdef DREAMS_LAUNCHER_TESTING

#include "testscript.h"

namespace testing {
constexpr bool kEnabled = true;
// The variable's value, or nullptr when it is unset or empty.
const char* env(Var v);
}  // namespace testing

#else

namespace testing {
constexpr bool kEnabled = false;
inline const char* env(Var) { return nullptr; }
}  // namespace testing

// Stand-in for the script driver: never created (load returns nullptr), so every
// member is the do-nothing a call site can safely reach.
class TestScript {
public:
    using PickFn = std::function<void(int row, const char* const* list)>;
    using PadFn = std::function<void(SDL_Gamepad*)>;
    static std::unique_ptr<TestScript> load(const std::string&, std::string*) { return nullptr; }
    void attach(SDL_Window*, bool, PickFn, PadFn) {}
    static bool is_real_input(const SDL_Event&) { return false; }
    bool pointer(float*, float*) const { return false; }
    void begin_frame() {}
    void widget(const std::string&, float, float, float, float, bool, bool) {}
    void fact(const std::string&, const std::string&) {}
    void end_frame() {}
    bool failed() const { return false; }
    const std::string& error() const {
        static const std::string none;
        return none;
    }
    void window_closed() {}
    bool take_shot(std::string*) { return false; }
};

#endif

#endif

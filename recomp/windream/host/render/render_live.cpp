extern "C" {
#include "crash_report.h"
extern int g_wd_quiet;
}
#include "render_fatal.h"
#include "render_live.h"
#include "render_boundary.h"
#include "render_interp.h"
#include "render_scene_draw.h"
#include "pacing.h"
#include "render_ui.h"
#include "render_metrics.h"
#include "render_movie.h"
#include "render_surface_scope.h"
#include "render/direct_sokol.h"
#include "render/graphics_backend.h"
#include "render/fog.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten/html5_webgl.h>
#endif

namespace {
using wd::SurfaceScope;
// No guest execution occurs inside these snapshots. Cache VirtualQuery's
// committed ranges only for this read scope; never across guest allocations.
struct ReadScope {
    ReadScope() { wd_render_read_scope_begin(); }
    ~ReadScope() { wd_render_read_scope_end(); }
};
struct Surface {
    uint32_t base = 0, bytes = 0;
    int width = 0, height = 0, pitch = 0, physical_width = 0, physical_height = 0;
    od_pixel_format format = OD_RGB565;
    od_render_id target = 0;
    wd_surface_id registry = 0;
    bool main = false;
    bool shadow = false;
    uint64_t version = 0;
};
struct MetricScope {
    uint64_t token;
    explicit MetricScope(wd_render_metric_category category) : token(wd_render_metrics_begin(category)) {}
    void finish() { wd_render_metrics_end(token); token = 0; }
    ~MetricScope() { finish(); }
};
struct CallbackFrame {
    uint32_t root, destination, caller;
    int main_frame;
    wd::SceneSnapshot scene;
    wd::SceneLighting lighting;
};
struct Live {
    SDL_Window *window = nullptr;
    od::GraphicsBackend backend;
    od_renderer *renderer = nullptr;
    wd::SceneDraw scene;
    od::port::GlideFogState fog;
    od::port::FogWaterPhase fog_water;
    uint64_t fog_updates = 0;
    std::map<uint32_t, Surface> surfaces;
    std::vector<std::vector<uint32_t>> caption_surfaces;
    std::vector<CallbackFrame> callback_frames;
    // The DOS 3dfx build's palette rows, by page, for the pages a slot of the
    // lighting-material table updates. Level lifetime.
    std::map<uint32_t, std::array<uint16_t, 32 * 256>> dos_banks;
    // Per page, the last REND_UpdatePaletteRows call: what the rows of faces
    // lit by an attack light are generated from (od_lit_palette_rows).
    struct LitSource {
        od_dos_palette_update update{};
        std::array<uint8_t, 1024> source{};
        int32_t scale = 0;
    };
    std::map<uint32_t, LitSource> lit_sources;
    uint64_t dos_palette_updates = 0;
    std::set<int32_t> observed_face_types;
    bool observed_lighting = false, observed_environment = false;
    unsigned surface_depth = 0; // renderer thread only, not a guest-worker lock
    uint64_t allocation = 1, frames = 0, scenes = 0, ui_draws = 0, uploads = 0, copies = 0;
    uint64_t shadows = 0;
    uint64_t lines = 0;
    uint64_t hnm5_frames = 0;
    uint64_t export_reads = 0, export_bytes = 0;
    uint64_t capture_reads = 0, capture_bytes = 0;
    int drawable_w = 640, drawable_h = 480;
    int present_w = 0, present_h = 0;
    bool pending_present = false, captured_scene = false;
} state;
// Display interpolation (WD_INTERPOLATE, with WD_FIXED_STEP's 30 Hz grid,
// host/sdl/pacing.c). The display gets a frame at every refresh, not only
// when the game presents one: the scene of a recent game frame with its
// node, vertex and camera transforms blended from the frame before
// (render_interp.cpp), then the 2D draws the game made into the same surface
// after that scene (HUD, text) replayed over it.
//
// Timing: game frame k is due at grid time D_k and is shown in full at
// D_k + latency; a refresh at time v shows the game at step
// k + (v - D_k - latency) / period, from the pair of frames around it. The
// latency covers the time the game takes to make a frame, so a refresh while
// it computes the next still has both frames it needs. Frames are drawn when
// the swapchain can take one without blocking (the D3D11 frame-latency
// waitable object): in the present while the game waits for the grid, and at
// host calls while it computes (wd_render_display_point). The game frame's
// own present is then left out (its output stays in the back buffer for
// captures). Host-only: nothing blended is written to the guest, and a frame
// with a draw that cannot be replayed ends the interpolation until the next
// two clean ones.
struct Replay {
    bool line = false;
    od_draw_2d draw{};
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    uint32_t colour = 0;
    od_pixel_format format = OD_RGB565;
};
struct GameFrame {
    bool scene = false, clean = false;
    const char *unclean = nullptr; // why it cannot be replayed
    uint32_t base = 0;         // the surface the main scene went to
    od_render_id target = 0;   // its target then
    int width = 0, height = 0; // its physical size
    uint64_t deadline = 0;     // the grid time its present was due
    uint64_t index = 0;        // game frames presented before it
    wd::SceneSnapshot snapshot;
    wd::SceneLighting lighting;
    wd::SceneDraw::Retained before; // Glide's retained palette its submit began with
    std::vector<Replay> replays;
    std::vector<od_render_id> held; // uploads its replays draw, released with it
};
struct Interpolation {
    int enabled = -1;
    SDL_ThreadID thread = 0;
    GameFrame recording;            // the game frame being drawn
    std::deque<GameFrame> history;  // the last ones presented, oldest first, at most 3
    wd::SceneMotion motion[2];      // history[n-3] to [n-2], and [n-2] to [n-1]
    od_render_id scratch = 0;
    int scratch_w = 0, scratch_h = 0, scratch_lw = 0, scratch_lh = 0;
    bool busy = false;
    bool showing = false;           // this game frame's own present is left out
    uint64_t interval = 0;          // the display's refresh period
    uint64_t vblank = 0;            // a recent refresh: when a blocking wait returned
    uint64_t latency = 0;           // how far the display trails the grid
    uint64_t render_ns = 2000000;   // a display frame's cost, smoothed
    uint64_t returned = 0;          // when the last game-frame present went back to the game
    uint64_t shown = 0;             // when the last display frame was presented
    uint64_t next_poll = 0;         // display points do nothing before this
    uint64_t game_frames = 0, frames = 0, in_present = 0, stalls = 0, refused = 0;
    std::map<std::string, uint64_t> reasons;
    FILE *trace = nullptr;          // WD_INTERP_TRACE: one line per display frame
    // WD_SMOOTH_CAMERA=<ms>: the displayed camera follows the game's through
    // a first-order lag of that time constant (display only; 0: off). It
    // evens out the follow camera's per-frame easing and dead zone.
    double camera_tau_ns = 0;
    bool camera_valid = false;
    wd::CameraPose camera;          // the displayed camera
    uint64_t camera_when = 0;       // the refresh it was for
} interp;
[[noreturn]] void fatal(const std::string &message) { wd_render_fatal(message.c_str()); }
// wd_render_fatal's hooks, installed before main so that the earliest failure
// (an unknown WD_RENDERER) is reported like any other.
void fatal_report(const char *message) {
    // abort() does not reach the crash handler, so the report is made here.
    const std::string why = std::string("direct renderer FATAL: ") + message;
    recomp_report_state(why.c_str());
}
#ifdef __EMSCRIPTEN__
// The page shows the message (wd_render_fatal's wd_web_status). SDL's box
// calls alert(), which a worker does not have: it would abort the runtime
// before the page hears why.
void fatal_notify(const char *) {}
#else
bool set(const char *name) {
    const char *value = std::getenv(name);
    return value && *value;
}
void fatal_notify(const char *message) {
    // Never block an unattended run: headless, quiet and control-channel
    // (test) runs end with the stderr line alone.
    if (g_wd_quiet || set("WD_HEADLESS") || set("WD_QUIET") || set("WD_CTL"))
        return;
    const std::string text =
        std::string(message) +
        "\n\nThe GPU renderer stopped on a case it does not support. "
        "To avoid it, choose Renderer: Original (software) in the launcher "
        "(run.py: --renderer software).\n\n"
        "Details are in log.txt and in direct-fatal.txt.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dreams to Reality", text.c_str(), nullptr);
}
#endif
const int fatal_hooks_installed = (wd_render_fatal_hooks(fatal_report, fatal_notify), 0);
void require(bool ok, const char *message) {
    if (!ok)
        fatal(message);
}
// Bind the string itself when the other argument may replace its storage.
// error.c_str() evaluated before that operation could leave a dangling pointer.
void require(bool ok, const std::string &message) {
    if (!ok)
        fatal(message);
}
uint32_t word(uint32_t address) {
    uint32_t value;
    require(wd_render_read_arena(nullptr, address, &value, 4), "unmapped renderer control");
    return value;
}
void put(uint32_t address, uint32_t value) { wd_render_write_arena(address, &value, 4); }
bool interpolating() {
    if (interp.enabled < 0) {
        const char *value = std::getenv("WD_INTERPOLATE");
        const bool wanted = value && *value && std::atoi(value) != 0;
#ifdef __EMSCRIPTEN__
        // A worker's OffscreenCanvas shows only the last frame of its task.
        interp.enabled = 0;
#else
        interp.enabled = wanted && wd_fixed_step();
#endif
        if (interp.enabled) {
            std::fprintf(stderr, "[direct] display interpolation on\n");
            const char *trace = std::getenv("WD_INTERP_TRACE");
            if (trace && *trace)
                interp.trace = std::fopen(trace, "w");
            const char *tau = std::getenv("WD_SMOOTH_CAMERA");
            interp.camera_tau_ns = tau && *tau ? std::max(0.0, std::atof(tau)) * 1e6 : 0;
        } else if (wanted) {
            std::fprintf(stderr, "[direct] display interpolation needs WD_FIXED_STEP and a "
                                 "native build; off\n");
        }
    }
    return interp.enabled > 0;
}
// A draw into the target this game frame's scene was drawn to, after it.
bool recording(od_render_id target) {
    return interp.enabled > 0 && interp.recording.scene && target == interp.recording.target;
}
void forget_target(od_render_id id);
void refuse(const char *why);
void retire_invalid_surfaces() {
    // VM decommit may originate on a guest worker thread. It only invalidates
    // registry IDs; release GPU objects here on the renderer's owning thread.
    for (auto it = state.surfaces.begin(); it != state.surfaces.end();) {
        wd_surface_desc descriptor{};
        if (wd_surface_describe(it->second.registry, &descriptor)) {
            ++it;
            continue;
        }
        forget_target(it->second.target);
        od_renderer_release(state.renderer, it->second.target);
        std::fprintf(stderr, "[direct] retired decommitted surface=%08x generation=%llu\n",
                     it->first, (unsigned long long)it->second.registry);
        it = state.surfaces.erase(it);
    }
}
Surface *find(uint32_t address, uint64_t bytes = 1) {
    require(state.surface_depth != 0, "GPU surface lookup outside renderer operation");
    auto it = state.surfaces.upper_bound(address);
    if (it == state.surfaces.begin())
        return nullptr;
    --it;
    auto &surface = it->second;
    return uint64_t(address) + bytes <= uint64_t(surface.base) + surface.bytes ? &surface : nullptr;
}
Surface &exact(uint32_t base) {
    auto *surface = find(base);
    if (!surface || surface->base != base)
        fatal("unregistered GPU surface " + std::to_string(base));
    return *surface;
}
od_render_id gpu_image(void *, uint32_t address, uint64_t *version) {
    auto *surface = find(address, 65536);
    if (!surface || !surface->shadow || surface->base != address)
        return 0;
    *version = surface->version;
    return surface->target;
}
// What the DOS 3dfx build (DREAMSFX.EXE) would have instead of the Windows
// state the lifted game holds; see docs/research/glide-direct-gaps.md,
// "Palette rows and the +0xc8 levels". The guest is only read.
bool guest(uint32_t address, void *destination, size_t bytes) {
    return wd_render_read_arena(nullptr, address, destination, bytes);
}
struct DosLights {
    // Lights held by an active effect-light slot (four at 0x6155f4, 0x18
    // bytes: +0 light, +4 flags, +0x14 owner). ENT_AddEffectLight has two
    // kinds of caller: SCENE_AddActorEffectLights with an actor of the list
    // at 0x4fb728, which the DOS build lacks, and the attack code with a
    // record of the 16-entry attack pool (0x630db8, 0x2d0 bytes each).
    // Owner decision 2026-10-01: the attack lights light the scene as in
    // Windows, in every project. The DOS binder (0x56164) leaves the scene
    // out when its palette scale is 0 (111 projects) and the DOS rows of the
    // other 39 run the other way; both follow from the cost of palette
    // downloads on the 3dfx card, so neither is reproduced.
    std::array<bool, 100> actor{}, attack{};
    bool any_attack = false;
};
bool effect_lights(DosLights &result) {
    bool any = false;
    for (uint32_t slot = 0; slot < 4; ++slot) {
        uint32_t record[6];
        require(guest(0x6155f4 + slot * 0x18, record, sizeof record), "unmapped effect lights");
        if (!(record[1] & 1) || record[0] >= 100)
            continue;
        const bool attack = record[5] >= 0x630db8 && record[5] < 0x630db8 + 16 * 0x2d0;
        (attack ? result.attack : result.actor)[record[0]] = true;
        result.any_attack |= attack;
        any = true;
    }
    return any;
}
DosLights capture_lights; // of the capture in progress
uint32_t filter_lights(void *, uint32_t, uint8_t *indices, uint32_t count) {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (!capture_lights.actor[indices[i]])
            indices[kept++] = indices[i];
    return kept;
}
bool lit_bank(void *, uint32_t page, uint16_t *bank) {
    const auto found = state.lit_sources.find(page);
    if (found == state.lit_sources.end())
        return false; // a page no slot updates: the block binds its file rows as before
    od_lit_palette_rows(&found->second.update, found->second.source.data(), found->second.scale,
                        bank);
    return true;
}
bool dos_bank(void *, uint32_t page, uint16_t *bank) {
    const auto found = state.dos_banks.find(page);
    if (found == state.dos_banks.end())
        return false;
    std::copy(found->second.begin(), found->second.end(), bank);
    return true;
}
wd::SceneReader scene_reader() {
    capture_lights = {};
    effect_lights(capture_lights);
    wd::SceneReader reader{nullptr, wd_render_read_arena, gpu_image};
    reader.dos_palette = dos_bank;
    reader.dos_lights = filter_lights;
    reader.lit_palette = lit_bank;
    return reader;
}
Surface &shadow_surface(uint32_t base) {
    if (auto *found = find(base, 65536)) {
        require(found->shadow && found->base == base, "shadow aliases a different surface");
        return *found;
    }
    Surface surface;
    surface.base = base;
    surface.bytes = 65536;
    surface.width = 128;
    surface.height = 256;
    surface.pitch = 256;
    surface.physical_width = 128;
    surface.physical_height = 256;
    surface.shadow = true;
    surface.target = od_renderer_target(state.renderer, 128, 256, 128, 256);
    require(surface.target != 0, od_renderer_error(state.renderer));
    wd_surface_desc d{base, 65536, 256, 256, 256, WD_SURFACE_P8, state.allocation++};
    surface.registry = wd_surface_register(&d);
    require(surface.registry != 0, "shadow range registration failed");
    wd_surface_set_authority(surface.registry, WD_SURFACE_GPU);
    return state.surfaces.emplace(base, surface).first->second;
}
void draw_shadow(const wd::SceneSnapshot &scene, Surface &surface) {
    MetricScope metric(WD_METRIC_SCENE);
    std::string error;
    require(wd::SceneDraw::submit_shadow(state.renderer, scene, surface.target, error), error);
    ++surface.version;
    ++state.shadows;
}
void draw(od_draw_2d command) {
    MetricScope metric(command.kind == OD_DRAW_COPY || command.kind == OD_DRAW_DIM ? WD_METRIC_COPY : WD_METRIC_UI);
    require(od_renderer_draw_2d(state.renderer, &command) != 0, od_renderer_error(state.renderer));
    if (recording(command.target)) {
        // A copy of the surface onto itself changes nothing (a dialogue frame
        // makes one); any other draw reading its own target is not replayed.
        if (command.kind == OD_DRAW_COPY && command.source == command.target)
            return;
        if (command.source == command.target || command.lookup == command.target) {
            interp.recording.clean = false;
            interp.recording.unclean = "a 2D draw that reads its own target";
        } else {
            interp.recording.replays.push_back({false, command});
        }
    }
}
void resize(Surface &surface) {
    if (!surface.main)
        return;
    int width, height;
    if (SDL_GetWindowSizeInPixels(state.window, &width, &height) && width > 0 && height > 0) {
        state.drawable_w = width;
        state.drawable_h = height;
    }
    if (surface.physical_width == state.drawable_w && surface.physical_height == state.drawable_h)
        return;
    auto next = od_renderer_target(state.renderer, state.drawable_w, state.drawable_h,
                                   surface.width, surface.height);
    require(next != 0, od_renderer_error(state.renderer));
    draw({OD_DRAW_COPY,
          next,
          surface.target,
          {0, 0, surface.width, surface.height},
          surface.format,
          0,
          1});
    forget_target(surface.target);
    od_renderer_release(state.renderer, surface.target);
    surface.target = next;
    surface.physical_width = state.drawable_w;
    surface.physical_height = state.drawable_h;
}
void upload(Surface &surface, od_rect rect, const std::vector<uint32_t> &pixels,
            od_draw_kind kind = OD_DRAW_RAW, uint32_t parameter = 0,
            const std::vector<uint32_t> &lookup = {}) {
    if (rect.width <= 0 || rect.height <= 0)
        return;
    MetricScope metric(WD_METRIC_UPLOAD);
    auto texture = od_renderer_upload_packed(state.renderer, rect.width, rect.height, pixels.data(),
                                             size_t(rect.width) * 4);
    require(texture != 0, od_renderer_error(state.renderer));
    od_render_id table = 0;
    if (!lookup.empty()) {
        table = od_renderer_upload_packed(state.renderer, 256, 28, lookup.data(), 256 * 4);
        require(table != 0, od_renderer_error(state.renderer));
    }
    draw({kind, surface.target, texture, rect, surface.format, parameter, 0, table});
    // A recorded upload is drawn again by the replays; release it after them.
    const auto release = [&](od_render_id id) {
        if (recording(surface.target))
            interp.recording.held.push_back(id);
        else
            od_renderer_release(state.renderer, id);
    };
    if (table)
        release(table);
    release(texture);
    state.uploads += table ? 2 : 1;
}
std::vector<od_rect> ranges(const Surface &surface, uint32_t address, uint32_t bytes) {
    if (((address - surface.base) | bytes) & 1u)
        fatal("byte-granular GPU surface transfer is not implemented");
    if (surface.pitch != surface.width * 2)
        fatal("GPU surface transfer has unsupported padding");
    uint32_t offset = (address - surface.base) / 2, count = bytes / 2;
    std::vector<od_rect> result;
    while (count) {
        const uint32_t x = offset % uint32_t(surface.width), y = offset / uint32_t(surface.width);
        uint32_t width = std::min(count, uint32_t(surface.width) - x), height = 1;
        if (!x && count >= uint32_t(surface.width)) {
            width = surface.width;
            height = count / width;
        }
        result.push_back({int(x), int(y), int(width), int(height)});
        offset += width * height;
        count -= width * height;
    }
    return result;
}
const char *sokol_item_name(uint32_t item) {
#define _SG_LOGITEM_XMACRO(item, msg) #item,
    static const char *const names[] = {_SG_LOG_ITEMS};
#undef _SG_LOGITEM_XMACRO
    return item < sizeof names / sizeof *names ? names[item] : "unknown item";
}
// sokol's message text is compiled out of non-debug builds: name the log item
// and the line, so a report says which check failed.
void log_sokol(const char *, uint32_t level, uint32_t item, const char *message, uint32_t line,
               const char *, void *) {
    if (level > 1)
        return;
    std::string text = std::string("sokol ") + sokol_item_name(item) + " (sokol_gfx.h:" +
                       std::to_string(line) + ")";
    if (message)
        text += std::string(": ") + message;
#ifdef __EMSCRIPTEN__
    // A lost context fails sokol's next framebuffer check before the backend's
    // acquire sees it: WebGL's checkFramebufferStatus answers UNSUPPORTED once
    // the context is gone, while isContextLost() can still say false (seen with
    // CDP Browser.crashGpuProcess).
    if (item == SG_LOGITEM_GL_FRAMEBUFFER_STATUS_UNSUPPORTED ||
        emscripten_is_webgl_context_lost(emscripten_webgl_get_current_context()))
        text += "; the WebGL context was lost (the browser reset the GPU)";
#endif
    fatal(text);
}
// A target is about to be released: replays that read it cannot be drawn
// again (the dialogue's caption surfaces go at the end of their scope).
void forget_target(od_render_id id) {
    if (interp.enabled <= 0 || !id)
        return;
    const auto reads = [id](const GameFrame &frame) {
        return std::any_of(frame.replays.begin(), frame.replays.end(), [id](const Replay &r) {
            return !r.line && (r.draw.source == id || r.draw.lookup == id);
        });
    };
    if (reads(interp.recording)) {
        interp.recording.clean = false;
        interp.recording.unclean = "a replayed draw's source surface was released";
    }
    if (std::any_of(interp.history.begin(), interp.history.end(), reads))
        refuse("a replayed draw's source surface was released");
}
void refuse(const char *why) {
    ++interp.refused;
    if (++interp.reasons[why] == 1)
        std::fprintf(stderr, "[direct] interpolation stops: %s\n", why);
    for (auto &frame : interp.history)
        for (auto id : frame.held)
            od_renderer_release(state.renderer, id);
    interp.history.clear();
    interp.motion[0] = interp.motion[1] = wd::SceneMotion{};
}
// The refresh a frame drawn now is shown at. interp.vblank is the refresh the
// last display frame was shown at (predicted when it was drawn, measured when
// a wait for the swapchain blocks until it): with one frame queued, the next
// one goes out at the first refresh after it that its drawing can make.
uint64_t display_time(uint64_t now) {
    const uint64_t i = interp.interval, ready = now + interp.render_ns;
    if (!interp.vblank || ready < interp.vblank)
        return interp.vblank ? interp.vblank + i : ready + i;
    return interp.vblank + std::max<uint64_t>(1, (ready - interp.vblank + i - 1) / i) * i;
}
// Development control channel ("display_shot"): the next display frame as a PNG.
std::string display_capture;
// One display frame: the game at the step the refresh at `when` stands for.
void show_frame(uint64_t when, bool in_present) {
    const size_t n = interp.history.size();
    const uint64_t period = wd_pacing_period_ns(), begin = SDL_GetTicksNS();
    // Relative to the newest frame: 0 is that frame, -1 the one before.
    const auto step = [&](size_t i) {
        return (double(int64_t(when - interp.history[i].deadline)) - double(interp.latency)) /
               double(period);
    };
    size_t newer = n - 1;
    double alpha = 1 + step(newer);
    if (alpha < 0 && n >= 3) {
        newer = n - 2;
        alpha = 1 + step(newer);
    }
    if (alpha > 1)
        ++interp.stalls; // the next game frame is late: hold this one
    alpha = std::clamp(alpha, 0.0, 1.0);
    auto &older_frame = interp.history[newer - 1], &frame = interp.history[newer];
    const auto &motion = interp.motion[newer == n - 1 ? 1 : 0];
    std::string error;
    interp.busy = true;
    wd::blend_scene(older_frame.snapshot, frame.snapshot, motion, float(alpha));
    auto camera = wd::blended_camera(older_frame.snapshot, frame.snapshot, motion, float(alpha));
    if (interp.camera_tau_ns > 0) {
        const double dt = interp.camera_valid ? double(int64_t(when - interp.camera_when)) : 0;
        const float follow = dt > 0 ? float(1 - std::exp(-dt / interp.camera_tau_ns)) : 0.0f;
        if (!interp.camera_valid || !wd::follow_camera(interp.camera, camera, follow))
            interp.camera = camera; // the first frame, or a cut
        interp.camera_valid = true;
        interp.camera_when = when;
        camera = interp.camera;
        wd::apply_camera(frame.snapshot, camera);
    }
    const auto retained = state.scene.retained();
    state.scene.restore(frame.before);
    require(state.scene.submit(state.renderer, frame.snapshot, interp.scratch, frame.width,
                               frame.height, true, error, nullptr, &frame.lighting),
            error);
    state.scene.restore(retained);
    wd::restore_scene_motion(frame.snapshot, motion);
    // Each call before its error text is read: a failure replaces that string.
    const auto check = [](int ok, const char *what) {
        if (!ok)
            fatal(std::string("display interpolation: ") + what + ": " +
                  od_renderer_error(state.renderer));
    };
    for (const auto &replay : frame.replays) {
        if (replay.line) {
            check(od_renderer_line_2d(state.renderer, interp.scratch, replay.x0, replay.y0,
                                      replay.x1, replay.y1, uint16_t(replay.colour),
                                      replay.format),
                  "line replay");
            continue;
        }
        auto command = replay.draw;
        command.target = interp.scratch;
        check(od_renderer_draw_2d(state.renderer, &command), "2D replay");
    }
    sg_swapchain swapchain{};
    const auto acquired = state.backend.acquire(state.window, swapchain, error);
    if (acquired == od::FrameState::failed)
        fatal(error);
    if (acquired == od::FrameState::ready) {
        sg_pass pass{};
        pass.swapchain = swapchain;
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {0, 0, 0, 1};
        sg_begin_pass(&pass);
        check(od_renderer_output(state.renderer, interp.scratch, 0.8f), "output");
        sg_end_pass();
    }
    sg_commit();
    if (acquired == od::FrameState::ready && !display_capture.empty()) {
        require(state.backend.capture(display_capture, error), error);
        std::fprintf(stderr, "[direct] display frame step %.3f written to %s\n",
                     double(frame.index) - 1 + alpha, display_capture.c_str());
        display_capture.clear();
    }
    if (acquired == od::FrameState::ready)
        require(state.backend.present(error), error);
    od_renderer_frame_complete(state.renderer);
    interp.busy = false;
    const uint64_t end = SDL_GetTicksNS();
    interp.render_ns = (interp.render_ns * 7 + (end - begin)) / 8;
    interp.shown = end;
    interp.vblank = when;
    interp.next_poll = when - interp.interval / 8;
    ++interp.frames;
    interp.in_present += in_present;
    if (interp.trace)
        std::fprintf(interp.trace, "%llu %llu %.4f %d %d %.2f %.2f %.2f\n",
                     (unsigned long long)end, (unsigned long long)when,
                     double(frame.index) - 1 + alpha, in_present ? 1 : 0,
                     alpha >= 1 && step(n - 1) > 0 ? 1 : 0, camera.eye[0], camera.eye[1],
                     camera.eye[2]);
}
// Waits until the display can take a frame, at most until `until`; true when
// one may be drawn now.
bool frame_slot(uint64_t until) {
    uint64_t now = SDL_GetTicksNS();
    // Never more than one frame a refresh, whatever the swapchain says (a
    // hidden window does not wait for the display).
    if (interp.shown) {
        const uint64_t earliest = interp.shown + interp.interval / 2;
        if (earliest >= until)
            return false;
        if (now < earliest) {
            SDL_DelayPrecise(earliest - now);
            now = SDL_GetTicksNS();
        }
    }
    if (now >= until)
        return false;
    if (state.backend.has_frame_waits()) {
        if (!state.backend.wait_frame(until - now))
            return false;
        const uint64_t after = SDL_GetTicksNS();
        if (after - now > 500000) // it blocked until the last frame went on screen
            interp.vblank = after;
    }
    return true;
}
void ensure_scratch(const GameFrame &frame, const Surface &surface) {
    if (interp.scratch && interp.scratch_w == frame.width && interp.scratch_h == frame.height &&
        interp.scratch_lw == surface.width && interp.scratch_lh == surface.height)
        return;
    if (interp.scratch)
        od_renderer_release(state.renderer, interp.scratch);
    interp.scratch =
        od_renderer_target(state.renderer, frame.width, frame.height, surface.width, surface.height);
    require(interp.scratch != 0, od_renderer_error(state.renderer));
    interp.scratch_w = frame.width;
    interp.scratch_h = frame.height;
    interp.scratch_lw = surface.width;
    interp.scratch_lh = surface.height;
}
// The present of a game frame: it joins the history, then display frames
// fill the refreshes until the game must go on at the grid's deadline.
void present_game_frame(Surface &surface) {
    const uint64_t arrival = SDL_GetTicksNS(), deadline = wd_pacing_deadline_ns(),
                   period = wd_pacing_period_ns();
    interp.interval = [] {
        const SDL_DisplayMode *mode =
            SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(state.window));
        return uint64_t(1e9 / (mode && mode->refresh_rate > 1 ? mode->refresh_rate : 60.0f));
    }();
    // The latency must cover the game's time to make a frame (and a display
    // frame's): grow at once, shrink slowly.
    if (interp.returned) {
        const uint64_t compute = arrival - interp.returned;
        // A display frame drawn just before the next game frame arrives is
        // for a refresh up to one interval later.
        const uint64_t want =
            std::min(compute + interp.interval + interp.render_ns + 1000000, period);
        interp.latency = want > interp.latency ? want : interp.latency - (interp.latency - want) / 32;
    }
    ++interp.game_frames;
    auto frame = std::move(interp.recording);
    interp.recording = GameFrame{};
    const char *why = !frame.scene                     ? "no 3D scene"
                      : !frame.clean                   ? frame.unclean
                      : frame.base != surface.base     ? "the scene went to another surface"
                      : frame.target != surface.target ? "the surface was resized"
                      : !deadline                      ? "no grid yet"
                                                       : nullptr;
    interp.showing = false;
    if (why) {
        refuse(why);
        for (auto id : frame.held)
            od_renderer_release(state.renderer, id);
        return;
    }
    if (!interp.history.empty() &&
        (interp.history.back().width != frame.width || interp.history.back().height != frame.height))
        refuse("the display was resized"); // older frames were drawn for another size
    frame.deadline = deadline;
    frame.index = interp.game_frames;
    interp.history.push_back(std::move(frame));
    if (interp.history.size() > 3) {
        for (auto id : interp.history.front().held)
            od_renderer_release(state.renderer, id);
        interp.history.pop_front();
    }
    const size_t n = interp.history.size();
    interp.motion[0] = std::move(interp.motion[1]);
    interp.motion[1] = wd::SceneMotion{};
    if (n < 2)
        return;
    wd::prepare_scene_motion(interp.history[n - 2].snapshot, interp.history[n - 1].snapshot,
                             interp.motion[1]);
    ensure_scratch(interp.history.back(), surface);
    interp.showing = true;
    while (frame_slot(deadline - std::min(deadline, interp.render_ns)))
        show_frame(display_time(SDL_GetTicksNS()), true);
    if (interp.game_frames % 300 == 0)
        std::fprintf(stderr,
                     "[direct] interpolation game_frames=%llu display_frames=%llu "
                     "in_present=%llu stalls=%llu refused=%llu latency=%.1fms render=%.2fms "
                     "nodes=%zu/%zu moving_vertices=%zu camera=%d\n",
                     (unsigned long long)interp.game_frames, (unsigned long long)interp.frames,
                     (unsigned long long)interp.in_present, (unsigned long long)interp.stalls,
                     (unsigned long long)interp.refused, double(interp.latency) / 1e6,
                     double(interp.render_ns) / 1e6, interp.motion[1].nodes,
                     interp.history.back().snapshot.nodes.size(), interp.motion[1].vertices,
                     interp.motion[1].camera);
}
} // namespace

// The next display-size 3D frame writes its scene inputs to path (render parity
// captures: recomp/windream/verify/render_parity.py). Guest thread only.
static std::string capture_request;
static uint32_t capture_root;
void wd_render_scene_capture_next(const char *path) { capture_request = path ? path : ""; }
int wd_render_display_capture_next(const char *path) {
    if (interp.enabled <= 0)
        return 0;
    display_capture = path ? path : "";
    return 1;
}
int wd_render_scene_capture_pending(void) { return capture_request.empty() ? 0 : 1; }
uint32_t wd_render_scene_capture_root(void) { return capture_root; } // of the last capture

int wd_render_requested(void) {
    static int requested = -1;
    if (requested < 0) {
        const char *mode = std::getenv("WD_RENDERER");
        if (!mode || !*mode) {
            // Unset: direct on Windows and in the browser (WebGL2), software
            // elsewhere (the GPU path has not been run there).
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
            requested = 1;
#else
            requested = 0;
#endif
        } else if (!std::strcmp(mode, "software"))
            requested = 0;
        else if (!std::strcmp(mode, "direct"))
            requested = 1;
        else
            fatal("WD_RENDERER must be software or direct");
    }
    return requested;
}
SDL_WindowFlags wd_render_window_flags(void) {
    if (!wd_render_requested())
        return 0;
    std::string error;
    require(od::GraphicsBackend::configure_window(error), error);
    return od::GraphicsBackend::window_flags();
}
int wd_render_open(SDL_Window *window) {
    std::string error;
    state.window = window;
    interp.thread = SDL_GetCurrentThreadID();
    if (interpolating())
        state.backend.request_frame_waits();
    if (!state.backend.init(window, error))
        fatal("cannot start the graphics backend: " + error);
    sg_desc desc{};
    desc.environment = state.backend.environment();
    desc.logger.func = log_sokol;
    desc.image_pool_size = 2048;
    desc.view_pool_size = 4096;
    desc.buffer_pool_size = 512;
    desc.uniform_buffer_size = 32 * 1024 * 1024;
    sg_setup(&desc);
    wd_render_metrics_initialize(const_cast<void *>(desc.environment.d3d11.device),
                                  const_cast<void *>(desc.environment.d3d11.device_context));
    state.renderer = od_renderer_create();
    require(state.renderer != nullptr, "cannot create direct renderer");
    SDL_GetWindowSizeInPixels(window, &state.drawable_w, &state.drawable_h);
    std::fprintf(stderr, "[direct] shared renderer ready at %dx%d\n", state.drawable_w,
                 state.drawable_h);
    return 1;
}
void wd_render_close(void) {
    wd_render_movie_shutdown();
    if (!state.renderer)
        return;
    state.scene.reset(state.renderer);
    interp.recording = GameFrame{};
    interp.history.clear();
    interp.motion[0] = interp.motion[1] = wd::SceneMotion{};
    interp.scratch = 0; // destroyed with the renderer
    if (interp.trace)
        std::fclose(interp.trace);
    interp.trace = nullptr;
    wd_render_metrics_shutdown();
    od_renderer_destroy(state.renderer);
    state.renderer = nullptr;
    state.surfaces.clear();
    state.callback_frames.clear();
    sg_shutdown();
    state.backend.shutdown();
    state.window = nullptr;
}
void wd_render_bind_surface(uint32_t base, uint32_t bytes, int w, int h, int pitch, int format,
                            int main) {
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    if (!state.renderer)
        return;
    if (auto *old = find(base, bytes)) {
        require(old->base == base, "ambiguous surface alias");
        return;
    }
    require(w > 0 && h > 0 && pitch == w * 2 && format >= 0 && format <= 1,
            "unsupported GPU surface layout");
    Surface surface;
    surface.base = base;
    surface.bytes = bytes;
    surface.width = w;
    surface.height = h;
    surface.pitch = pitch;
    surface.main = main != 0;
    surface.format = od_pixel_format(format);
    surface.physical_width = main ? state.drawable_w : w;
    surface.physical_height = main ? state.drawable_h : h;
    surface.target =
        od_renderer_target(state.renderer, surface.physical_width, surface.physical_height, w, h);
    require(surface.target != 0, od_renderer_error(state.renderer));
    surface.registry = wd_surface_find(base, bytes);
    if (!surface.registry) {
        wd_surface_desc d{base,
                          bytes,
                          uint32_t(w),
                          uint32_t(h),
                          uint32_t(pitch),
                          format ? WD_SURFACE_555 : WD_SURFACE_565,
                          state.allocation++};
        surface.registry = wd_surface_register(&d);
    }
    require(surface.registry != 0, "cannot register GPU surface");
    wd_surface_set_authority(surface.registry, WD_SURFACE_GPU);
    state.surfaces.emplace(base, surface);
}
void wd_render_forget_surface(uint32_t base) {
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    auto it = state.surfaces.find(base);
    if (it == state.surfaces.end())
        return;
    forget_target(it->second.target);
    od_renderer_release(state.renderer, it->second.target);
    wd_surface_unregister(it->second.registry);
    state.surfaces.erase(it);
}
void wd_render_begin_present(uint32_t base) {
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    auto &surface = exact(base);
    if (interpolating())
        present_game_frame(surface);
    MetricScope metric(WD_METRIC_OUTPUT);
    std::string error;
    sg_swapchain swapchain{};
    const auto acquired = state.backend.acquire(state.window, swapchain, error);
    if (acquired == od::FrameState::failed)
        fatal(error);
    state.pending_present = acquired == od::FrameState::ready;
    if (state.pending_present) {
        state.present_w = swapchain.width;
        state.present_h = swapchain.height;
        sg_pass pass{};
        pass.swapchain = swapchain;
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {0, 0, 0, 1};
        sg_begin_pass(&pass);
        require(od_renderer_output(state.renderer, surface.target, 0.8f) != 0,
                od_renderer_error(state.renderer));
        sg_end_pass();
    }
    // End timestamp before commit/retirement; otherwise the GPU interval can
    // include idle time after the driver's flush rather than the output pass.
    metric.finish();
    sg_commit();
    state.scene.finish_frame(state.renderer);
    od_renderer_frame_complete(state.renderer);
    ++state.frames;
    if (state.frames == 1 || state.frames % 250 == 0)
        std::fprintf(
            stderr,
            "[direct] frames=%llu scenes=%llu ui=%llu uploads=%llu copies=%llu; "
            "shadows=%llu resolves=%llu routine_readbacks=%llu exports=%llu export_bytes=%llu "
            "captures=%llu capture_bytes=%llu\n",
            (unsigned long long)state.frames, (unsigned long long)state.scenes,
            (unsigned long long)state.ui_draws, (unsigned long long)state.uploads,
            (unsigned long long)state.copies, (unsigned long long)state.shadows,
            (unsigned long long)od_renderer_stats(state.renderer).shadow_resolves,
            // GPU downloads the backend performed that no explicit reason accounts for
            (unsigned long long)(state.backend.downloads() - state.export_reads -
                                 state.capture_reads),
            (unsigned long long)state.export_reads, (unsigned long long)state.export_bytes,
            (unsigned long long)state.capture_reads, (unsigned long long)state.capture_bytes);
}
void wd_render_end_present(void) {
    if (state.pending_present) {
        std::string error;
        // Interpolating: the display frames stand for this one, which stays in
        // the back buffer for a capture. Otherwise it waits for its slot, as
        // the display frames do, before going out.
        if (!(interp.enabled > 0 && interp.showing)) {
            // At most one refresh: a window hidden behind another takes no
            // frames, and the game must not slow down for it.
            if (interp.enabled > 0)
                state.backend.wait_frame(interp.interval ? interp.interval : 16666667);
            require(state.backend.present(error), error);
        }
        state.pending_present = false;
        wd_render_metrics_frame_completed();
    }
    if (interp.enabled > 0)
        interp.returned = SDL_GetTicksNS();
}
// Host calls while the game makes a frame (user.c, winmm.c and the renderer's
// own entries): a display frame when the display can take one. Cheap until
// the next refresh is near; only on the renderer's thread, outside its calls.
void display_point(bool inside_scene);
void wd_render_display_point(void) {
    if (!state.surface_depth)
        display_point(false);
}
// inside_scene: wd_render_scene between its capture and its submit, a long
// stretch without other host calls; the frame drawn there touches neither.
void display_point(bool inside_scene) {
    if (interp.enabled <= 0 || SDL_GetCurrentThreadID() != interp.thread || interp.busy ||
        !interp.showing || (state.surface_depth && !inside_scene) || interp.history.size() < 2)
        return;
    const uint64_t now = SDL_GetTicksNS();
    if (now < interp.next_poll)
        return;
    if (!frame_slot(now + 1)) {
        interp.next_poll = now + 500000;
        return;
    }
    show_frame(display_time(SDL_GetTicksNS()), false);
}
void wd_render_capture(const char *path) {
    if (!state.pending_present) {
        std::fprintf(stderr, "[direct] capture skipped: no drawable\n");
        return;
    }
    std::string error;
    require(state.backend.capture(path, error), error);
    // The swapchain image may still have its pre-resize dimensions until the
    // next acquire. Account for the captured image, not a newer window size.
    const int width = state.present_w, height = state.present_h;
    const uint64_t bytes = uint64_t(width) * height * 4;
    ++state.capture_reads;
    state.capture_bytes += bytes;
    std::fprintf(stderr, "[direct] explicit readback reason=capture call=004458bb "
                         "surface=swapchain region=0,0,%d,%d bytes=%llu\n",
                 width, height, (unsigned long long)bytes);
    std::fprintf(stderr, "[direct] explicit output capture: %s\n", path);
}
void wd_render_mouse(SDL_Event *event) {
    if (!state.renderer || state.surfaces.empty())
        return;
    int ww, wh;
    if (!SDL_GetWindowSize(state.window, &ww, &wh) || ww <= 0 || wh <= 0)
        return;
    int drawable_w, drawable_h;
    if (SDL_GetWindowSizeInPixels(state.window, &drawable_w, &drawable_h) &&
        drawable_w > 0 && drawable_h > 0) {
        state.drawable_w = drawable_w;
        state.drawable_h = drawable_h;
    }
    const int lw = int(word(0x49d9fc)), lh = int(word(0x49da00));
    const auto canvas = od_centered_canvas(state.drawable_w, state.drawable_h, lw, lh);
    float *x = nullptr;
    float *y = nullptr;
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        x = &event->motion.x;
        y = &event->motion.y;
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
               event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        x = &event->button.x;
        y = &event->button.y;
    }
    if (!x)
        return;
    float ox, oy;
    od_canvas_point(canvas, lw, lh, *x * state.drawable_w / ww, *y * state.drawable_h / wh, &ox,
                    &oy);
    *x = ox;
    *y = oy;
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        event->motion.xrel *= float(state.drawable_w) * lw / (ww * canvas.width);
        event->motion.yrel *= float(state.drawable_h) * lh / (wh * canvas.height);
    }
}
int wd_render_surface_owned(uint32_t address) {
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    return state.renderer && find(address) != nullptr;
}
void wd_render_line(uint32_t destination, int x0, int y0, int x1, int y1, uint32_t colour) {
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    MetricScope metric(WD_METRIC_UI);
    auto &surface = exact(destination);
    require(!surface.shadow, "packed line into a shadow mask");
    require(surface.width == int(word(0x661ebc)) && surface.height == int(word(0x661ec8)),
            "line viewport differs from destination");
    resize(surface);
    require(od_renderer_line_2d(state.renderer, surface.target, x0, y0, x1, y1,
                                uint16_t(colour), surface.format) != 0,
            od_renderer_error(state.renderer));
    if (recording(surface.target)) {
        Replay replay;
        replay.line = true;
        replay.x0 = x0;
        replay.y0 = y0;
        replay.x1 = x1;
        replay.y1 = y1;
        replay.colour = colour;
        replay.format = surface.format;
        interp.recording.replays.push_back(replay);
    }
    ++state.ui_draws;
    if (++state.lines == 1 || state.lines % 1000 == 0)
        std::fprintf(stderr, "[direct] lines=%llu\n", (unsigned long long)state.lines);
}
void wd_render_ui(const uint32_t raw[8], uint32_t entry) {
    wd_render_display_point();
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    ReadScope scope;
    if (!state.renderer)
        fatal("UI before renderer initialization");
    wd::UiRegisters registers;
    std::memcpy(&registers, raw, sizeof registers);
    wd::UiBatch batch;
    std::string error;
    wd::SceneReader reader{(void *)(uintptr_t)entry, wd_render_read_arena};
    const bool ok = entry == 0x427b8c ? wd::normalize_masked64(reader, registers, batch, error)
                    : entry == 0x40368b
                        ? wd::normalize_gauge(reader, registers, batch, error)
                        : wd::normalize_sprite(reader, registers, entry == 0x403bcd, batch, error);
    require(ok, error);
    auto &surface = exact(batch.target_address);
    resize(surface);
    for (const auto &write : batch.metadata)
        wd_render_write_arena(write.address, &write.value, write.bytes);
    upload(surface, batch.rect, batch.pixels, batch.kind, batch.parameter, batch.lookup);
    ++state.ui_draws;
}
void wd_render_movie_upload(uint32_t source, uint32_t destination, int x, int y,
                            int width, int height, int pitch) {
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    ReadScope reads;
    auto &surface = exact(destination);
    require(!surface.shadow && x >= 0 && y >= 0 && width > 0 && height > 0 &&
                width <= surface.width && height <= surface.height &&
                x <= surface.width - width && y <= surface.height - height &&
                int64_t(pitch) >= int64_t(width) * 2,
            "invalid decoded movie rectangle");
    require(uint64_t(source) + uint64_t(height - 1) * uint32_t(pitch) + uint64_t(width) * 2 <=
                0x100000000ull, "decoded movie source overflow");
    std::vector<uint32_t> pixels(size_t(width) * height);
    std::vector<uint16_t> row(width);
    for (int line = 0; line < height; ++line) {
        require(wd_render_read_arena((void *)(uintptr_t)0x42665a,
                                      source + uint32_t(line) * uint32_t(pitch),
                                      row.data(), size_t(width) * 2), "unmapped decoded movie row");
        for (int column = 0; column < width; ++column)
            pixels[size_t(line) * width + column] = uint32_t(row[column]) | (64u << 16);
    }
    resize(surface);
    upload(surface, {x, y, width, height}, pixels);
    ++state.ui_draws;
    if (++state.hnm5_frames == 1 || state.hnm5_frames % 250 == 0)
        std::fprintf(stderr, "[direct] hnm5_frames=%llu rect=%d,%d,%d,%d pitch=%d\n",
                     (unsigned long long)state.hnm5_frames, x, y, width, height, pitch);
}
void wd_render_fog_update(void) {
    ReadScope scope;
    uint8_t project[0x1d0];
    double delta = 0;
    require(wd_render_read_arena(nullptr, word(0x661e04), project, sizeof project),
            "unmapped fog project");
    require(wd_render_read_arena(nullptr, 0x5e5388, &delta, sizeof delta), "unmapped fog delta");
    const int32_t mode = int32_t(word(0x4fbaac));
    od::port::SCENE_SetFog(state.fog, state.fog_water, project, delta, mode);
    ++state.fog_updates;
    std::fprintf(stderr, "[direct] fog_update=%llu colour=%06x density=%.9g phase=%.9g mode=%d\n",
                 (unsigned long long)state.fog_updates, state.fog.color, state.fog.density,
                 state.fog_water.phase, mode);
}
void wd_render_palette_rows(uint32_t slot, uint32_t page, int32_t r, int32_t g, int32_t b) {
    // Called before the lifted REND_UpdatePaletteRows writes the Windows rows
    // of this slot: run the DOS build's routine on a host copy of the bank.
    // The first call for a page finds the rows as loaded from the file.
    ReadScope scope;
    uint8_t record[0x420];
    if (page < 0x8000 || !guest(slot, record, sizeof record))
        return;
    auto found = state.dos_banks.find(page);
    if (found == state.dos_banks.end()) {
        std::vector<uint32_t> rows(32 * 256);
        if (!guest(page - 0x8000, rows.data(), 0x8000))
            return;
        found = state.dos_banks.emplace(page, std::array<uint16_t, 32 * 256>{}).first;
        for (size_t i = 0; i < rows.size(); ++i)
            found->second[i] = uint16_t(rows[i] >> 16);
    }
    od_dos_palette_update update{};
    update.rgb[0] = r;
    update.rgb[1] = g;
    update.rgb[2] = b;
    uint32_t actor, project = 0, loading = 0;
    float countdown = 0;
    std::memcpy(&actor, record + 0x1c, 4);
    update.actor_bound = (record[0x18] & 0x10) != 0;
    require(!update.actor_bound || guest(actor + 0x98, update.actor_rgb, 12),
            "unmapped palette actor");
    require(guest(0x4a0fd4, &update.cursor, 4) && guest(0x661e04, &project, 4) &&
                guest(0x5df49c, &loading, 4) && guest(0x5e5480, &countdown, 4),
            "unmapped palette state");
    if (project) {
        // DOS SCENE_InitLevel: both scales 0 unless the project names one.
        uint32_t lit = 0;
        require(guest(project + 0xc0, &update.actor_scale, 4) &&
                    guest(project + 0xc4, &update.scale, 4) && guest(project + 0xc8, &lit, 4),
                "unmapped palette project");
        update.lit_project = lit != 0;
    }
    // The DOS routine with its effect-light flag (0xfe790) clear: row 15, the
    // unlit row, is refreshed on every call. With the flag set DOS stops
    // refreshing it and rewrites the rows by its own ramp; the host generates
    // the lit rows from the unlit one instead (od_lit_palette_rows), with the
    // Windows scale of this slot.
    update.effect_light = 0;
    update.rebuild = loading != 0 || countdown > 0;
    od_dos_palette_rows(&update, record + 0x20, found->second.data());
    auto &lit = state.lit_sources[page];
    lit.update = update;
    std::copy_n(record + 0x20, lit.source.size(), lit.source.begin());
    require(guest(update.actor_bound ? 0x6262e8 : 0x626300, &lit.scale, 4),
            "unmapped palette scale");
    if (++state.dos_palette_updates == 1 || state.dos_palette_updates % 5000 == 0)
        std::fprintf(stderr,
                     "[direct] dos_palette updates=%llu pages=%zu scale=%d actor_scale=%d "
                     "lit_project=%d effect_light=%d\n",
                     (unsigned long long)state.dos_palette_updates, state.dos_banks.size(),
                     update.scale, update.actor_scale, update.lit_project, update.effect_light);
}
void wd_render_prepare_callback(uint32_t root, uint32_t destination, int main_frame,
                                 uint32_t caller) {
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    MetricScope metric(WD_METRIC_PREPARE);
    // A read scope must end before returning to guest execution. Only owned
    // source snapshots, never VirtualQuery caches, survive the callback.
    ReadScope scope;
    CallbackFrame frame{root, destination, caller, main_frame, {}, {}};
    std::string error;
    const bool shadow = !main_frame && destination == word(0x62b9a8) &&
                        word(0x661ebc) == 128 && word(0x661ec8) == 256;
    if (shadow) shadow_surface(destination);
    if (!main_frame && destination == 0x5d6b98 && word(0x661ebc) == 64 &&
        word(0x661ec8) == 64 && caller == 0x40fe13)
        wd_render_bind_surface(destination, 8192, 64, 64, 128, int(word(0x49da1c)), 0);
    require(wd::capture_scene(scene_reader(), root, frame.scene, error),
            error);
    frame.scene.fog.enabled = state.fog.table_mode;
    frame.scene.fog.colour = ((state.fog.color >> 16) & 255u) | (state.fog.color & 0xff00u) |
                             ((state.fog.color & 255u) << 16);
    std::copy(state.fog.table.begin(), state.fog.table.end(), frame.scene.fog.table);
    auto &surface = exact(destination);
    resize(surface);
    if (!shadow) {
        float vp[16];
        require(frame.scene.view_projection(surface.physical_width, surface.physical_height,
                                             main_frame != 0, vp), "invalid callback camera");
        require(wd::prepare_scene_lighting(frame.scene, vp, frame.lighting, error, true), error);
        for (const auto &write : frame.lighting.writes)
            wd_render_write_arena_at(write.entry, write.address, &write.value, write.bytes);
    } else {
        require(std::none_of(frame.scene.nodes.begin(), frame.scene.nodes.end(),
                    [](const wd::SceneNode &node) { return node.light_count || (node.flags & 0x800); }),
                "shaded shadow callback metadata is not validated");
    }
    std::fprintf(stderr, "[direct] callback_prepare root=%08x writes=%zu\n", root,
                 frame.lighting.writes.size());
    state.callback_frames.push_back(std::move(frame));
}
void wd_render_collect_scene(uint32_t root) {
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    MetricScope metric(WD_METRIC_PREPARE);
    ReadScope scope;
    wd::SceneSnapshot scene;
    wd::SceneLighting lighting;
    wd::SceneCollector collector;
    std::string error;
    require(wd::capture_scene(scene_reader(), root, scene, error), error);
    const int width = int(word(0x661ebc)), height = int(word(0x661ec8));
    float vp[16];
    require(scene.view_projection(width, height, false, vp), "invalid collector camera");
    require(wd::prepare_scene_lighting(scene, vp, lighting, error, true), error);
    require(wd::prepare_scene_collector(scene, width, height, false, collector, error), error);
    for (const auto &write : lighting.writes)
        wd_render_write_arena_at(write.entry, write.address, &write.value, write.bytes);
    // Retail's index array has room for 2048 shorts, so store complete
    // triangles only. The unbounded original collector can overwrite its
    // neighbouring arrays. Counts describe the records actually published.
    uint32_t count = 0;
    for (const auto &triangle : collector.triangles) {
        for (unsigned corner = 0; corner < 3; ++corner) {
            const uint16_t index = uint16_t(count + corner);
            wd_render_write_arena_at(0x478800, 0x6760e4 + (count + corner) * 8,
                                      triangle.points[corner].data(), 8);
            wd_render_write_arena_at(0x478800, 0x67f0e4 + count + corner,
                                      &triangle.clipped[corner], 1);
            wd_render_write_arena_at(0x478800, 0x67e0e4 + (count + corner) * 2, &index, 2);
        }
        wd_render_write_arena_at(0x478800, 0x6800e4 + count, &triangle.face_flags, 1);
        count += 3;
    }
    put(0x6808e4, count);
    put(0x6808e8, count);
    static uint64_t frames = 0;
    if (++frames == 1 || frames % 25 == 0)
        std::fprintf(stderr, "[direct] collector=%llu triangles=%zu stored=%zu no_draw=1\n",
                     (unsigned long long)frames, collector.total_triangles, collector.triangles.size());
}
void wd_render_scene(uint32_t root, uint32_t destination, int main_frame, uint32_t caller,
                     uint32_t frame_callback) {
    wd_render_display_point();
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    MetricScope preparation(WD_METRIC_PREPARE);
    ReadScope scope;
    const uint64_t begin = SDL_GetTicksNS();
    wd::SceneSnapshot scene;
    wd::SceneLighting prepared_lighting;
    std::string error;
    const bool shadow = !main_frame && destination == word(0x62b9a8) && word(0x661ebc) == 128 &&
                        word(0x661ec8) == 256;
    if (shadow)
        shadow_surface(destination);
    const bool thumbnail = !main_frame && destination == 0x5d6b98 && word(0x661ebc) == 64 &&
                           word(0x661ec8) == 64 && caller == 0x40fe13;
    if (thumbnail)
        wd_render_bind_surface(destination, 8192, 64, 64, 128, int(word(0x49da1c)), 0);
    if (frame_callback) {
        require(!state.callback_frames.empty(), "callback scene was not prepared");
        auto frame = std::move(state.callback_frames.back());
        state.callback_frames.pop_back();
        require(frame.root == root && frame.destination == destination && frame.caller == caller &&
                    frame.main_frame == main_frame, "callback scene nesting mismatch");
        scene = std::move(frame.scene);
        prepared_lighting = std::move(frame.lighting);
    } else {
        require(wd::capture_scene(scene_reader(), root, scene, error), error);
        scene.fog.enabled = state.fog.table_mode;
        scene.fog.colour = ((state.fog.color >> 16) & 255u) | (state.fog.color & 0xff00u) |
                           ((state.fog.color & 255u) << 16);
        std::copy(state.fog.table.begin(), state.fog.table.end(), scene.fog.table);
    }
    const uint64_t captured = SDL_GetTicksNS();
    preparation.finish();
    display_point(true);
    for (const auto &face : scene.faces) {
        const auto &node = scene.nodes[face.owner];
        if (!node.submitted && !node.visual_active) continue;
        if (state.observed_face_types.insert(face.type).second)
            std::fprintf(stderr, "[direct] source_mode=%d node=%08x block=%08x\n",
                         face.type, node.address, face.block);
        if (node.light_count && !state.observed_lighting) {
            std::fprintf(stderr, "[direct] source_lighting node=%08x count=%u\n",
                         node.address, node.light_count);
            state.observed_lighting = true;
        }
        if ((node.flags & 0x800) && !state.observed_environment) {
            std::fprintf(stderr, "[direct] source_environment node=%08x\n", node.address);
            state.observed_environment = true;
        }
    }
    auto *surface = find(destination);
    if (!surface)
        fatal("unregistered 3D destination " + std::to_string(destination));
    if (shadow) {
        draw_shadow(scene, *surface);
        return;
    }
    resize(*surface);
    if (!capture_request.empty() && !thumbnail && word(0x661ebc) >= 320) {
        // Development control channel ("scene_capture"): the inputs of this
        // display frame, as the renderer is about to draw them. Not only the
        // main frame: during dialogue the game draws through its second entry
        // (0x4593aa), whose root is not the dword at 0x661ee8.
        require(wd::write_scene(scene, capture_request.c_str(), error), error);
        capture_root = root;
        std::fprintf(stderr, "[render-capture] scene inputs written to %s\n",
                     capture_request.c_str());
        capture_request.clear();
    }
    const char *capture = std::getenv("WD_SCENE_CAPTURE");
    if (!state.captured_scene && main_frame && capture && *capture) {
        require(wd::write_scene(scene, capture, error), error);
        state.captured_scene = true;
        std::fprintf(stderr, "[render-capture] captured original scene inputs to %s\n", capture);
    }
    // Display interpolation keeps the main scene, its lighting and the retained
    // palette it was drawn from; any other scene into that target makes the
    // frame unreplayable.
    const bool record = interpolating() && main_frame && !thumbnail;
    if (!record && recording(surface->target)) {
        interp.recording.clean = false;
        interp.recording.unclean = main_frame ? "a thumbnail scene into the display surface"
                                              : "a second scene into the display surface";
    }
    wd::SceneLighting own_lighting;
    const wd::SceneLighting *lighting = frame_callback ? &prepared_lighting : nullptr;
    if (record && !lighting) {
        // What submit prepares for itself otherwise, with the same arguments.
        float vp[16];
        require(scene.view_projection(surface->physical_width, surface->physical_height, true, vp),
                "invalid scene camera");
        require(wd::prepare_scene_lighting(scene, vp, own_lighting, error, true), error);
        lighting = &own_lighting;
    }
    const auto retained = state.scene.retained();
    std::vector<wd::SceneLightingWrite> lighting_writes;
    {
        MetricScope metric(WD_METRIC_SCENE);
        require(state.scene.submit(state.renderer, scene, surface->target, surface->physical_width,
                               surface->physical_height, main_frame != 0, error,
                               frame_callback ? nullptr : &lighting_writes, lighting),
            error);
    }
    for (const auto &write : lighting_writes)
        wd_render_write_arena_at(write.entry, write.address, &write.value, write.bytes);
    if (!lighting_writes.empty() && (state.scenes == 0 || state.scenes % 25 == 0))
        std::fprintf(stderr, "[direct] scene_metadata writes=%zu environment_uv_writes=%zu\n",
                     lighting_writes.size(), size_t(std::count_if(
                         lighting_writes.begin(), lighting_writes.end(),
                         [](const auto &write) { return write.entry == 0x47e094; })));
    if (thumbnail) {
        const od_readback_request request{
            surface->target, {0, 0, 64, 64}, OD_READBACK_THUMBNAIL, caller};
        std::vector<uint32_t> rgba;
        const uint64_t export_begin = SDL_GetTicksNS();
        sg_commit(); // explicit CPU consumer; does not present or advance game time
        require(state.backend.read_image(od_renderer_image(state.renderer, request.target), 0, 0,
                                         64, 64, rgba, error),
                error);
        std::vector<uint16_t> packed(rgba.size());
        for (size_t i = 0; i < rgba.size(); ++i)
            packed[i] = od_pack_colour(rgba[i], surface->format);
        wd_surface_set_authority(surface->registry, WD_SURFACE_CPU);
        wd_render_write_arena(destination, packed.data(), packed.size() * 2);
        ++state.export_reads;
        state.export_bytes += packed.size() * 2;
        std::fprintf(stderr,
                     "[direct] explicit readback reason=thumbnail call=%08x surface=%08x "
                     "region=0,0,64,64 bytes=%zu exports=%llu barrier_ms=%.3f\n",
                     caller, destination, packed.size() * 2, (unsigned long long)state.export_reads,
                     double(SDL_GetTicksNS() - export_begin) / 1e6);
        wd_render_forget_surface(destination);
        od_renderer_frame_complete(state.renderer);
    }
    ++state.scenes;
    if (state.scenes == 1 || state.scenes % 25 == 0) {
        const auto stats = od_renderer_stats(state.renderer);
        std::fprintf(stderr,
                     "[direct] scene=%llu capture=%.3fms submit=%.3fms triangles=%zu "
                     "total_batches=%llu resources=%u\n",
                     (unsigned long long)state.scenes, double(captured - begin) / 1e6,
                     double(SDL_GetTicksNS() - captured) / 1e6, scene.faces.size(),
                     (unsigned long long)stats.scene_batches, stats.live_resources);
    }
    if (record) {
        // A later main scene into the same target clears it: the last one wins.
        auto &frame = interp.recording;
        for (auto id : frame.held)
            od_renderer_release(state.renderer, id);
        frame = GameFrame{};
        frame.scene = frame.clean = true;
        frame.base = destination;
        frame.target = surface->target;
        frame.width = surface->physical_width;
        frame.height = surface->physical_height;
        frame.snapshot = std::move(scene);
        frame.lighting = frame_callback ? std::move(prepared_lighting) : std::move(own_lighting);
        frame.before = retained;
    }
}
int wd_render_copy(uint32_t instruction, uint32_t source, uint32_t destination, uint32_t count,
                   uint32_t width, int direction) {
    wd_render_display_point();
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    if (!state.renderer || !count)
        return 0;
    ReadScope scope;
    const uint64_t size = uint64_t(count) * width;
    require(size <= UINT32_MAX, "GPU copy size overflow");
    if (direction < 0) {
        require(size - width <= source && size - width <= destination,
                "backward GPU copy overflow");
        source -= uint32_t(size - width);
        destination -= uint32_t(size - width);
    }
    auto *src = find(source, size);
    auto *dst = find(destination, size);
    const bool caption_copy = src && !dst && !state.caption_surfaces.empty() &&
                              wd_render_copy_caller(instruction) == 0x436e84;
    if (src && !dst &&
        (destination == word(0x5e1090) || destination == word(0x5e1094) || caption_copy)) {
        wd_render_bind_surface(destination, uint32_t(size), src->width, src->height, src->pitch,
                               src->format, 1);
        dst = find(destination, size);
        if (caption_copy)
            state.caption_surfaces.back().push_back(destination);
    }
    if (!src && !dst) {
        wd_surface_check(instruction, source, uint32_t(size), 0);
        wd_surface_check(instruction, destination, uint32_t(size), 1);
        return 0;
    }
    if (!dst) {
        char message[200];
        std::snprintf(message, sizeof message,
                      "CPU consumer at %08x caller %08x: %08x -> %08x, %llu bytes", instruction,
                      wd_render_copy_caller(instruction), source, destination,
                      (unsigned long long)size);
        fatal(message);
    }
    resize(*dst);
    if (src) {
        require(source == src->base && destination == dst->base && size == src->bytes &&
                    size == dst->bytes,
                "partial GPU copy is not implemented");
        draw({OD_DRAW_COPY,
              dst->target,
              src->target,
              {0, 0, dst->width, dst->height},
              dst->format,
              0,
              1});
        ++state.copies;
        return 1;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    require(
        wd_render_read_arena((void *)(uintptr_t)instruction, source, bytes.data(), bytes.size()),
        "CPU upload source is unmapped");
    size_t position = 0;
    for (auto rect : ranges(*dst, destination, uint32_t(size))) {
        std::vector<uint32_t> pixels(size_t(rect.width) * rect.height);
        for (auto &pixel : pixels) {
            pixel = uint32_t(bytes[position]) | (uint32_t(bytes[position + 1]) << 8) | (64u << 16);
            position += 2;
        }
        upload(*dst, rect, pixels);
    }
    return 1;
}
int wd_render_fill(uint32_t instruction, uint32_t destination, uint32_t value, uint32_t count,
                   uint32_t width, int direction) {
    wd_render_display_point();
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    if (!state.renderer || !count)
        return 0;
    const uint64_t size = uint64_t(count) * width;
    require(size <= UINT32_MAX, "GPU fill size overflow");
    if (direction < 0) {
        require(size - width <= destination, "backward GPU fill overflow");
        destination -= uint32_t(size - width);
    }
    auto *dst = find(destination, size);
    if (!dst) {
        wd_surface_check(instruction, destination, uint32_t(size), 1);
        return 0;
    }
    resize(*dst);
    const uint32_t colour = width == 1 ? (value & 255) * 257 : value & 65535;
    require(width <= 2 || (value >> 16) == colour, "alternating-word GPU fill is not implemented");
    for (auto rect : ranges(*dst, destination, uint32_t(size)))
        draw({OD_DRAW_FILL, dst->target, 0, rect, dst->format, colour, 1});
    return 1;
}
void wd_render_dim_background(uint32_t level) {
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    auto &target = exact(word(0x5e549c));
    auto &source = exact(word(0x5e1090));
    resize(target);
    require(level <= 3, "invalid background dim level");
    draw({OD_DRAW_DIM,
          target.target,
          source.target,
          {0, 0, target.width, target.height},
          target.format,
          level,
          1});
    put(0x5e1098, source.base + uint32_t(target.width * target.height * 2));
    put(0x5e1090, word(0x5e1094));
}
void wd_render_text_band(int y) {
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    auto &target = exact(word(0x5e549c));
    resize(target);
    draw({OD_DRAW_BAND,
          target.target,
          0,
          {0, y, target.width, target.width >= 640 ? 16 : 9},
          target.format,
          0,
          0});
}
void wd_render_caption_band(void) {
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    auto &target = exact(word(0x5e549c));
    const int scale = int(word(0x4a2f1d));
    require(scale > 0, "invalid caption scale");
    resize(target);
    draw({OD_DRAW_CAPTION,
          target.target,
          0,
          {0, 64 / scale, target.width, 132 / scale},
          target.format,
          0,
          0});
}
void wd_render_reset_scene(void) {
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    state.dos_banks.clear(); // SCENE_LoadLevel registers the level's materials again
    state.lit_sources.clear();
    if (interp.enabled > 0)
        refuse("a new level"); // nothing to blend from
    if (state.renderer) {
        state.scene.reset(state.renderer);
        std::vector<uint32_t> masks;
        for (const auto &item : state.surfaces)
            if (item.second.shadow)
                masks.push_back(item.first);
        for (auto base : masks)
            wd_render_forget_surface(base);
    }
}
void wd_render_caption_scope_begin(void) {
    state.caption_surfaces.emplace_back();
    std::fprintf(stderr, "[direct] caption_scope=begin depth=%zu\n", state.caption_surfaces.size());
}
void wd_render_caption_scope_end(void) {
    SurfaceScope surfaces(state.surface_depth, retire_invalid_surfaces);
    require(!state.caption_surfaces.empty(), "caption scope underflow");
    for (auto base : state.caption_surfaces.back())
        wd_render_forget_surface(base);
    state.caption_surfaces.pop_back();
    std::fprintf(stderr, "[direct] caption_scope=end depth=%zu\n", state.caption_surfaces.size());
}

#include "render_live.h"
#include "render_boundary.h"
#include "render_scene_draw.h"
#include "render_ui.h"
#include "render/direct_sokol.h"
#include "platform/graphics_backend.h"
#include "port/fog.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
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
    uint64_t allocation = 1, frames = 0, scenes = 0, ui_draws = 0, uploads = 0, copies = 0;
    uint64_t shadows = 0;
    uint64_t export_reads = 0, export_bytes = 0;
    int drawable_w = 640, drawable_h = 480;
    bool pending_present = false, captured_scene = false;
} state;
[[noreturn]] void fatal(const std::string &message) {
    std::fprintf(stderr, "[direct] FATAL: %s\n", message.c_str());
    std::abort();
}
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
Surface *find(uint32_t address, uint64_t bytes = 1) {
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
    std::string error;
    require(wd::SceneDraw::submit_shadow(state.renderer, scene, surface.target, error), error);
    ++surface.version;
    ++state.shadows;
}
void draw(od_draw_2d command) {
    require(od_renderer_draw_2d(state.renderer, &command) != 0, od_renderer_error(state.renderer));
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
    auto texture = od_renderer_upload_packed(state.renderer, rect.width, rect.height, pixels.data(),
                                             size_t(rect.width) * 4);
    require(texture != 0, od_renderer_error(state.renderer));
    od_render_id table = 0;
    if (!lookup.empty()) {
        table = od_renderer_upload_packed(state.renderer, 256, 28, lookup.data(), 256 * 4);
        require(table != 0, od_renderer_error(state.renderer));
    }
    draw({kind, surface.target, texture, rect, surface.format, parameter, 0, table});
    if (table)
        od_renderer_release(state.renderer, table);
    od_renderer_release(state.renderer, texture);
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
void log_sokol(const char *, uint32_t level, uint32_t, const char *message, uint32_t, const char *,
               void *) {
    if (level <= 1)
        fatal(message ? message : "sokol error");
}
} // namespace

int wd_render_requested(void) {
    static int requested = -1;
    if (requested < 0) {
        const char *mode = std::getenv("WD_RENDERER");
        if (!mode || !*mode || !std::strcmp(mode, "software"))
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
    if (!state.backend.init(window, error)) {
        std::fprintf(stderr, "[direct] %s\n", error.c_str());
        return 0;
    }
    sg_desc desc{};
    desc.environment = state.backend.environment();
    desc.logger.func = log_sokol;
    desc.image_pool_size = 2048;
    desc.view_pool_size = 4096;
    desc.buffer_pool_size = 512;
    desc.uniform_buffer_size = 32 * 1024 * 1024;
    sg_setup(&desc);
    state.renderer = od_renderer_create();
    require(state.renderer != nullptr, "cannot create direct renderer");
    SDL_GetWindowSizeInPixels(window, &state.drawable_w, &state.drawable_h);
    std::fprintf(stderr, "[direct] shared renderer ready at %dx%d\n", state.drawable_w,
                 state.drawable_h);
    return 1;
}
void wd_render_close(void) {
    if (!state.renderer)
        return;
    state.scene.reset(state.renderer);
    od_renderer_destroy(state.renderer);
    state.renderer = nullptr;
    state.surfaces.clear();
    sg_shutdown();
    state.backend.shutdown();
    state.window = nullptr;
}
void wd_render_bind_surface(uint32_t base, uint32_t bytes, int w, int h, int pitch, int format,
                            int main) {
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
    auto it = state.surfaces.find(base);
    if (it == state.surfaces.end())
        return;
    od_renderer_release(state.renderer, it->second.target);
    wd_surface_unregister(it->second.registry);
    state.surfaces.erase(it);
}
void wd_render_begin_present(uint32_t base) {
    auto &surface = exact(base);
    std::string error;
    sg_swapchain swapchain{};
    const auto acquired = state.backend.acquire(state.window, swapchain, error);
    if (acquired == od::FrameState::failed)
        fatal(error);
    state.pending_present = acquired == od::FrameState::ready;
    if (state.pending_present) {
        sg_pass pass{};
        pass.swapchain = swapchain;
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {0, 0, 0, 1};
        sg_begin_pass(&pass);
        require(od_renderer_output(state.renderer, surface.target, 0.8f) != 0,
                od_renderer_error(state.renderer));
        sg_end_pass();
    }
    sg_commit();
    state.scene.finish_frame(state.renderer);
    od_renderer_frame_complete(state.renderer);
    ++state.frames;
    if (state.frames == 1 || state.frames % 250 == 0)
        std::fprintf(
            stderr,
            "[direct] frames=%llu scenes=%llu ui=%llu uploads=%llu copies=%llu; "
            "shadows=%llu resolves=%llu routine_readbacks=0 exports=%llu export_bytes=%llu\n",
            (unsigned long long)state.frames, (unsigned long long)state.scenes,
            (unsigned long long)state.ui_draws, (unsigned long long)state.uploads,
            (unsigned long long)state.copies, (unsigned long long)state.shadows,
            (unsigned long long)od_renderer_stats(state.renderer).shadow_resolves,
            (unsigned long long)state.export_reads, (unsigned long long)state.export_bytes);
}
void wd_render_end_present(void) {
    if (state.pending_present) {
        std::string error;
        require(state.backend.present(error), error);
        state.pending_present = false;
    }
}
void wd_render_capture(const char *path) {
    if (!state.pending_present) {
        std::fprintf(stderr, "[direct] capture skipped: no drawable\n");
        return;
    }
    std::string error;
    require(state.backend.capture(path, error), error);
    std::fprintf(stderr, "[direct] explicit output capture: %s\n", path);
}
void wd_render_mouse(SDL_Event *event) {
    if (!state.renderer || state.surfaces.empty())
        return;
    int ww, wh;
    if (!SDL_GetWindowSize(state.window, &ww, &wh) || ww <= 0 || wh <= 0)
        return;
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
void wd_render_ui(const uint32_t raw[8], uint32_t entry) {
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
void wd_render_scene(uint32_t root, uint32_t destination, int main_frame, uint32_t caller,
                     uint32_t frame_callback) {
    ReadScope scope;
    const uint64_t begin = SDL_GetTicksNS();
    wd::SceneSnapshot scene;
    std::string error;
    const bool shadow = !main_frame && destination == word(0x62b9a8) && word(0x661ebc) == 128 &&
                        word(0x661ec8) == 256;
    if (shadow)
        shadow_surface(destination);
    const bool thumbnail = !main_frame && destination == 0x5d6b98 && word(0x661ebc) == 64 &&
                           word(0x661ec8) == 64 && caller == 0x40fe13;
    if (thumbnail)
        wd_render_bind_surface(destination, 8192, 64, 64, 128, int(word(0x49da1c)), 0);
    require(wd::capture_scene({nullptr, wd_render_read_arena, gpu_image}, root, scene, error),
            error);
    scene.fog.enabled = state.fog.table_mode;
    scene.fog.colour = ((state.fog.color >> 16) & 255u) | (state.fog.color & 0xff00u) |
                       ((state.fog.color & 255u) << 16);
    std::copy(state.fog.table.begin(), state.fog.table.end(), scene.fog.table);
    const uint64_t captured = SDL_GetTicksNS();
    auto *surface = find(destination);
    if (!surface)
        fatal("unregistered 3D destination " + std::to_string(destination));
    if (shadow) {
        draw_shadow(scene, *surface);
        return;
    }
    resize(*surface);
    if (frame_callback &&
        std::any_of(scene.nodes.begin(), scene.nodes.end(),
                    [](const wd::SceneNode &n) {
                        return (n.submitted || n.visual_active) && (n.light_count || (n.flags & 0x800));
                    }))
        fatal("shaded frame callback sequencing is not validated");
    const char *capture = std::getenv("WD_SCENE_CAPTURE");
    if (!state.captured_scene && main_frame && capture && *capture) {
        require(wd::write_scene(scene, capture, error), error);
        state.captured_scene = true;
        std::fprintf(stderr, "[render-capture] captured original scene inputs to %s\n", capture);
    }
    std::vector<wd::SceneLightingWrite> lighting_writes;
    require(state.scene.submit(state.renderer, scene, surface->target, surface->physical_width,
                               surface->physical_height, main_frame != 0, error, &lighting_writes),
            error);
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
}
int wd_render_copy(uint32_t instruction, uint32_t source, uint32_t destination, uint32_t count,
                   uint32_t width, int direction) {
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
void wd_render_caption_scope_begin(void) { state.caption_surfaces.emplace_back(); }
void wd_render_caption_scope_end(void) {
    require(!state.caption_surfaces.empty(), "caption scope underflow");
    for (auto base : state.caption_surfaces.back())
        wd_render_forget_surface(base);
    state.caption_surfaces.pop_back();
}

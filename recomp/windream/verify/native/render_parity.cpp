// Render parity: the game's 3D frame from captured inputs, drawn by the
// production scene adapter and renderer through od::GraphicsBackend, the same
// program on Windows (D3D11) and in the browser (WebGL2). The inputs come from
// the Windows build (recomp/windream/verify/render_parity.py):
//
//   <case>.wds   the scene inputs the live renderer was about to draw
//                (control channel "scene_capture")
//   <case>.wdmi  the guest's committed memory at that frame ("memory_dump":
//                "WDM2", the render root of that scene capture, then runs of
//                { u32 va, u32 bytes, data })
//
// For a .wds the snapshot is read and drawn. For a .wdmi the adapter first
// captures the scene from the memory image (capture_scene with a plain reader
// and the recorded root) and writes it to <out>.wds, so
// the adapter's reading of guest memory is compared between a 64-bit and a
// wasm32 host, byte for byte, and then draws it. Every case writes <out>.rgba:
// width, height (two 32-bit words), then RGBA8 rows, top first.
//
//   wd_render_parity <input> <output-prefix> [<input> <output-prefix> ...]
//
// In the browser main only sets up; the page calls wd_parity_case per case.
#include "render/direct_sokol.h"
#include "render/graphics_backend.h"
#include "render_scene.h"
#include "render_scene_draw.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

namespace {
constexpr int kWidth = 640, kHeight = 480;

SDL_Window *window;
od::GraphicsBackend backend;
od_renderer *renderer;
od_render_id target, output; // one pair for every case: the page runs them all in one process
std::string last_error;

int fail(const std::string &text) {
    last_error = text;
    std::fprintf(stderr, "FAIL: %s\n", text.c_str());
    std::fflush(stderr);
    return 1;
}
void sokol_log(const char *, uint32_t level, uint32_t item, const char *msg, uint32_t, const char *,
               void *) {
    std::fprintf(stderr, "sokol[level %u item %u]: %s\n", level, item, msg ? msg : "");
    if (level <= 1) last_error = "sokol error item " + std::to_string(item);
}

bool read_file(const std::string &path, std::vector<uint8_t> &bytes) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    bytes.resize(size > 0 ? size_t(size) : 0);
    const bool ok = bytes.empty() || std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    return ok;
}
bool write_file(const std::string &path, const void *data, size_t size) {
    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const bool ok = std::fwrite(data, 1, size, file) == size;
    return std::fclose(file) == 0 && ok;
}

// A guest memory image: the committed runs, sorted by address.
struct MemoryImage {
    struct Run {
        uint32_t va, bytes;
        size_t offset;
    };
    std::vector<uint8_t> file;
    std::vector<Run> runs;
    uint32_t root = 0; // the render root of the frame the image was taken at

    bool load(const std::string &path, std::string &error) {
        if (!read_file(path, file) || file.size() < 8 || std::memcmp(file.data(), "WDM2", 4)) {
            error = "not a guest memory image: " + path;
            return false;
        }
        std::memcpy(&root, file.data() + 4, 4);
        size_t at = 8;
        while (at + 8 <= file.size()) {
            Run run{};
            std::memcpy(&run.va, file.data() + at, 4);
            std::memcpy(&run.bytes, file.data() + at + 4, 4);
            run.offset = at + 8;
            if (run.offset + run.bytes > file.size()) {
                error = "truncated guest memory image: " + path;
                return false;
            }
            runs.push_back(run);
            at = run.offset + run.bytes;
        }
        std::sort(runs.begin(), runs.end(), [](const Run &a, const Run &b) { return a.va < b.va; });
        return true;
    }
    // Exact bytes or nothing: a range that leaves the committed runs fails.
    static bool read(void *context, uint32_t address, void *destination, size_t size) {
        const auto *self = static_cast<const MemoryImage *>(context);
        auto *target = static_cast<uint8_t *>(destination);
        uint64_t va = address;
        while (size) {
            auto next = std::upper_bound(self->runs.begin(), self->runs.end(), va,
                                         [](uint64_t v, const Run &r) { return v < r.va; });
            if (next == self->runs.begin()) return false;
            const Run &run = *(next - 1);
            if (va >= uint64_t(run.va) + run.bytes) return false;
            const size_t count = size_t(std::min<uint64_t>(size, uint64_t(run.va) + run.bytes - va));
            std::memcpy(target, self->file.data() + run.offset + size_t(va - run.va), count);
            target += count;
            va += count;
            size -= count;
        }
        return true;
    }
};

bool ends_with(const std::string &text, const char *tail) {
    const size_t n = std::strlen(tail);
    return text.size() >= n && !text.compare(text.size() - n, n, tail);
}
} // namespace

extern "C" {
EMSCRIPTEN_KEEPALIVE const char *wd_parity_error(void) { return last_error.c_str(); }

EMSCRIPTEN_KEEPALIVE int wd_parity_init(void) {
    std::string error;
    if (!SDL_Init(SDL_INIT_VIDEO)) return fail(std::string("SDL_Init: ") + SDL_GetError());
    if (!od::GraphicsBackend::configure_window(error)) return fail("configure_window: " + error);
    Uint32 flags = od::GraphicsBackend::window_flags();
#ifndef __EMSCRIPTEN__
    flags |= SDL_WINDOW_HIDDEN;
#endif
    window = SDL_CreateWindow("WINDREAM render parity", kWidth, kHeight, flags);
    if (!window) return fail(std::string("SDL_CreateWindow: ") + SDL_GetError());
    if (!backend.init(window, error)) return fail("backend.init: " + error);
    sg_desc desc{};
    desc.environment = backend.environment();
    desc.uniform_buffer_size = 32 * 1024 * 1024;
    desc.logger.func = sokol_log;
    sg_setup(&desc);
    if (!sg_isvalid()) return fail("sg_setup");
    renderer = od_renderer_create();
    if (!renderer) return fail("od_renderer_create");
    target = od_renderer_target(renderer, kWidth, kHeight, kWidth, kHeight);
    output = od_renderer_target(renderer, kWidth, kHeight, kWidth, kHeight);
    if (!target || !output) return fail(std::string("target: ") + od_renderer_error(renderer));
    std::printf("parity: sokol backend %d, origin_top_left %d, pointer bits %d\n",
                int(sg_query_backend()), int(sg_query_features().origin_top_left),
                int(sizeof(void *) * 8));
    return 0;
}

// 0 on success. The outputs are <out>.rgba and, for a memory image, <out>.wds.
EMSCRIPTEN_KEEPALIVE int wd_parity_case(const char *input, const char *out) {
    last_error.clear();
    std::string error;
    wd::SceneSnapshot snapshot;
    if (ends_with(input, ".wdmi")) {
        MemoryImage memory;
        if (!memory.load(input, error)) return fail(error);
        const uint32_t root = memory.root;
        if (!root) return fail("the memory image has no render root");
        wd::SceneReader reader{};
        reader.context = &memory;
        reader.read = MemoryImage::read;
        if (!wd::capture_scene(reader, root, snapshot, error)) return fail("capture_scene: " + error);
        if (!wd::write_scene(snapshot, (std::string(out) + ".wds").c_str(), error))
            return fail("write_scene: " + error);
    } else if (!wd::read_scene(input, snapshot, error)) {
        return fail("read_scene: " + error);
    }
    // A live capture carries the handles of the capturing process's GPU masks;
    // they mean nothing here.
    for (auto &material : snapshot.materials) {
        material.gpu_mask = 0;
        material.gpu_version = 0;
    }
    wd::SceneDraw draw;
    if (!draw.submit(renderer, snapshot, target, kWidth, kHeight, true, error))
        return fail("submit: " + error);
    sg_view_desc view_desc{};
    view_desc.color_attachment.image = od_renderer_image(renderer, output);
    const sg_view view = sg_make_view(&view_desc);
    sg_pass pass{};
    pass.attachments.colors[0] = view;
    pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
    sg_begin_pass(&pass);
    const bool drawn = od_renderer_output(renderer, target, 0.8f) != 0;
    sg_end_pass();
    sg_commit();
    draw.finish_frame(renderer);
    od_renderer_frame_complete(renderer);
    if (!drawn) return fail(std::string("output: ") + od_renderer_error(renderer));
    std::vector<uint32_t> pixels;
    if (!backend.read_image(od_renderer_image(renderer, output), 0, 0, kWidth, kHeight, pixels,
                            error))
        return fail("read_image: " + error);
    sg_destroy_view(view);
    draw.reset(renderer);
    if (!last_error.empty()) return fail(last_error);
    std::vector<uint32_t> file(2 + pixels.size());
    file[0] = kWidth;
    file[1] = kHeight;
    std::copy(pixels.begin(), pixels.end(), file.begin() + 2);
    if (!write_file(std::string(out) + ".rgba", file.data(), file.size() * 4))
        return fail(std::string("cannot write ") + out + ".rgba");
    std::printf("parity: %s: %zu nodes, %zu faces, %zu materials\n", input, snapshot.nodes.size(),
                snapshot.faces.size(), snapshot.materials.size());
    std::fflush(stdout);
    return 0;
}
}

int main(int argc, char **argv) {
    if (wd_parity_init()) return 2;
#ifdef __EMSCRIPTEN__
    (void)argc;
    (void)argv;
    EM_ASM({ if (Module.onParityReady) Module.onParityReady(); });
    return 0; // the runtime stays up (EXIT_RUNTIME=0); the page calls wd_parity_case
#else
    int failed = 0;
    for (int i = 1; i + 1 < argc; i += 2)
        failed += wd_parity_case(argv[i], argv[i + 1]);
    return failed ? 1 : 0;
#endif
}

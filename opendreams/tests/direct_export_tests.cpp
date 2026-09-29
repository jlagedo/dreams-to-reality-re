// Exercise the production explicit-export API, including non-aligned row widths.
#include "platform/graphics_backend.h"
#include "render/direct_sokol.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static void check(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
int main() {
    check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    od::GraphicsBackend backend;
    std::string error;
    std::vector<uint32_t> pixels{0x12345678};
    check(!backend.read_image({}, 0, 0, 1, 1, pixels, error), "uninitialized export accepted");
    check(od::GraphicsBackend::configure_window(error), error.c_str());
    auto *window = SDL_CreateWindow("Direct export validation", 64, 64,
                                    SDL_WINDOW_HIDDEN | od::GraphicsBackend::window_flags());
    check(window != nullptr, SDL_GetError());
    check(backend.init(window, error), error.c_str());
    sg_desc desc{};
    desc.environment = backend.environment();
    sg_setup(&desc);
    auto *renderer = od_renderer_create();
    check(renderer != nullptr, "renderer creation");
    constexpr int width = 67, height = 11;
    std::vector<uint32_t> source(width * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            source[y * width + x] =
                uint32_t(x * 3) | uint32_t(y * 19) << 8 | uint32_t((x + y) * 3) << 16 | 0xff000000;
    auto image = od_renderer_upload_rgba(renderer, width, height, source.data(), width * 4);
    check(image != 0, od_renderer_error(renderer));
    sg_commit();
    check(backend.read_image(od_renderer_image(renderer, image), 3, 2, 17, 5, pixels, error),
          error.c_str());
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 17; ++x)
            check(pixels[y * 17 + x] == source[(y + 2) * width + x + 3],
                  "export region/orientation/pitch changed bytes");
    auto saved = pixels;
    for (auto region :
         std::vector<od_rect>{{-1, 0, 1, 1}, {0, 0, 0, 1}, {66, 0, 2, 1}, {0, 10, 1, 2}}) {
        check(!backend.read_image(od_renderer_image(renderer, image), region.x, region.y,
                                  region.width, region.height, pixels, error),
              "invalid region accepted");
        check(pixels == saved, "failed export mutated output");
    }
    check(!backend.read_image({}, 0, 0, 1, 1, pixels, error), "invalid handle accepted");
    // Every packed value is exported before output gamma, including RGB555 bit 15.
    std::vector<uint32_t> packed(65536);
    for (uint32_t i = 0; i < packed.size(); ++i)
        packed[i] = i | (63u << 16);
    auto packed_source = od_renderer_upload_packed(renderer, 256, 256, packed.data(), 256 * 4);
    for (int format = 0; format < 2; ++format) {
        auto target = od_renderer_target(renderer, 256, 256, 256, 256);
        od_draw_2d draw{
            OD_DRAW_RAW, target, packed_source, {0, 0, 256, 256}, od_pixel_format(format), 0};
        check(od_renderer_draw_2d(renderer, &draw) != 0, od_renderer_error(renderer));
        sg_commit();
        check(
            backend.read_image(od_renderer_image(renderer, target), 0, 0, 256, 256, pixels, error),
            error.c_str());
        for (uint32_t i = 0; i < pixels.size(); ++i)
            check(od_pack_colour(pixels[i], od_pixel_format(format)) == i,
                  "packed export changed value");
        od_renderer_release(renderer, target);
        od_renderer_frame_complete(renderer);
    }
    od_renderer_destroy(renderer);
    sg_shutdown();
    backend.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::puts("direct export: region, row pitch, orientation, invalid requests and 131072 "
              "pre-gamma packed values passed");
}

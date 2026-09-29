// Scene diagnostic of the production input adapter. Full lighting/UI and
// live integration are not claimed by this test. Readback is an
// explicit capture of the offscreen target, never a game composition bridge.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <SDL3/SDL.h>
#include "render_scene.h"
#include "render_scene_draw.h"
#include "render/direct_sokol.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void require(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
static void log_sokol(const char *, uint32_t level, uint32_t, const char *message, uint32_t,
                      const char *, void *) {
    if (level <= 1) {
        std::fprintf(stderr, "sokol: %s\n", message ? message : "error");
        std::exit(2);
    }
}
int main(int argc, char **argv) {
    require(argc == 3 || (argc == 4 && (!std::strcmp(argv[3], "--geometry") ||
                                        !std::strcmp(argv[3], "--shadow"))),
            "usage: WDSceneGpuTests input.wds output-prefix [--geometry|--shadow]");
    const bool shadow = argc == 4 && !std::strcmp(argv[3], "--shadow");
    const bool geometry_only = argc == 4 && !shadow;
    wd::SceneSnapshot snapshot;
    std::string error;
    require(wd::read_scene(argv[1], snapshot, error), error.c_str());
    std::vector<od_scene_triangle> triangles;
    for (const auto &face : snapshot.faces) {
        if (!snapshot.nodes[face.owner].submitted)
            continue;
        od_scene_triangle triangle{};
        std::copy(face.corners.begin(), face.corners.end(), triangle.corners);
        // Stable diagnostic colours by source primitive type, not retail shading.
        uint32_t hash = uint32_t(face.type) * 0x9e3779b9u;
        triangle.colour = 0xff000000u | ((hash & 0x007f7f7fu) + 0x00404040u);
        triangle.mode = OD_FACE_OPAQUE;
        triangles.push_back(triangle);
    }
    require(!triangles.empty(), "scene has no submitted triangles");
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL actual;
    require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &requested,
                                        1, D3D11_SDK_VERSION, &device, &actual, &context)),
            "hardware D3D11 unavailable");
    sg_desc desc{};
    desc.environment.d3d11.device = device;
    desc.environment.d3d11.device_context = context;
    desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    desc.environment.defaults.depth_format = SG_PIXELFORMAT_NONE;
    desc.environment.defaults.sample_count = 1;
    desc.uniform_buffer_size = 32 * 1024 * 1024;
    desc.logger.func = log_sokol;
    sg_setup(&desc);
    auto *renderer = od_renderer_create();
    require(renderer != nullptr, "shared renderer unavailable");
    wd::SceneDraw scene_draw;
    const auto sizes = shadow ? std::vector<std::pair<int, int>>{{128, 256}}
                              : std::vector<std::pair<int, int>>{{640, 480}, {1920, 1080}};
    for (const auto size : sizes) {
        const int width = size.first, height = size.second;
        auto target = od_renderer_target(renderer, width, height, 640, 480);
        require(target != 0, od_renderer_error(renderer));
        od_scene_packet packet{};
        packet.target = target;
        packet.nodes = snapshot.poses.data();
        packet.node_count = snapshot.poses.size();
        packet.vertices = snapshot.vertices.data();
        packet.vertex_count = snapshot.vertices.size();
        packet.triangles = triangles.data();
        packet.triangle_count = triangles.size();
        packet.clear = 1;
        packet.clear_colour[3] = 1;
        require(snapshot.view_projection(width, height, true, packet.view_projection),
                "camera conversion");
        if (shadow)
            require(wd::SceneDraw::submit_shadow(renderer, snapshot, target, error), error.c_str());
        else if (geometry_only)
            require(od_renderer_scene(renderer, &packet) != 0, od_renderer_error(renderer));
        else
            require(scene_draw.submit(renderer, snapshot, target, width, height, true, error),
                    error.c_str());
        auto output = od_renderer_target(renderer, width, height, 640, 480);
        require(output != 0, od_renderer_error(renderer));
        sg_view_desc output_desc{};
        output_desc.color_attachment.image = od_renderer_image(renderer, output);
        auto output_view = sg_make_view(&output_desc);
        sg_pass output_pass{};
        output_pass.attachments.colors[0] = output_view;
        output_pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
        sg_begin_pass(&output_pass);
        require(od_renderer_output(renderer, target, geometry_only || shadow ? 1.0f : 0.8f) != 0,
                od_renderer_error(renderer));
        sg_end_pass();
        sg_commit();
        scene_draw.finish_frame(renderer);
        od_renderer_frame_complete(renderer);
        auto *image =
            (ID3D11Texture2D *)sg_d3d11_query_image_info(od_renderer_image(renderer, output)).tex2d;
        D3D11_TEXTURE2D_DESC td{};
        image->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.MiscFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D *staging = nullptr;
        require(SUCCEEDED(device->CreateTexture2D(&td, nullptr, &staging)),
                "capture staging allocation");
        context->CopyResource(staging, image);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)),
                "capture readback");
        std::vector<uint32_t> pixels(size_t(width) * height);
        for (int y = 0; y < height; ++y)
            std::memcpy(pixels.data() + size_t(y) * width,
                        (uint8_t *)mapped.pData + size_t(y) * mapped.RowPitch, size_t(width) * 4);
        context->Unmap(staging, 0);
        staging->Release();
        sg_reset_state_cache();
        size_t drawn = 0;
        for (auto pixel : pixels)
            drawn += (pixel & 0xffffffu) != 0;
        require(drawn > 1000, "geometry target is empty");
        if (shadow) {
            std::vector<uint8_t> p8(pixels.size() * 2);
            for (size_t i = 0; i < pixels.size(); ++i) {
                const auto index = pixels[i] & 255u;
                require(index <= 1, "shadow must contain only indices zero/one");
                p8[2 * i] = p8[2 * i + 1] = uint8_t(index);
                pixels[i] = index ? 0xffffffff : 0xff000000;
            }
            std::string raw = std::string(argv[2]) + ".p8";
            FILE *file = std::fopen(raw.c_str(), "wb");
            require(file != nullptr, "shadow capture output");
            require(std::fwrite(p8.data(), 1, p8.size(), file) == p8.size(),
                    "shadow capture write");
            std::fclose(file);
        }
        std::string path = std::string(argv[2]) + "-" + std::to_string(width) + "x" +
                           std::to_string(height) + ".png";
        SDL_Surface *surface =
            SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels.data(), width * 4);
        require(surface != nullptr && SDL_SavePNG(surface, path.c_str()), SDL_GetError());
        SDL_DestroySurface(surface);
        std::printf("%s: %zu original triangles, %zu covered pixels; explicit capture only\n",
                    path.c_str(), triangles.size(), drawn);
        require(od_renderer_release(renderer, target) != 0, "target retirement");
        sg_destroy_view(output_view);
        require(od_renderer_release(renderer, output) != 0, "output retirement");
        sg_commit();
        od_renderer_frame_complete(renderer);
    }
    scene_draw.reset(renderer);
    sg_commit();
    od_renderer_frame_complete(renderer);
    require(od_renderer_stats(renderer).live_resources == 0, "live target leak");
    od_renderer_destroy(renderer);
    sg_shutdown();
    context->Release();
    device->Release();
    return 0;
}

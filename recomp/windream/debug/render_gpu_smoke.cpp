// Offscreen sokol/D3D11 feasibility smoke, not a retail renderer port.
// Proves target creation, GREATER depth, native readback and RGB565 conversion.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#define SOKOL_IMPL
#define SOKOL_D3D11
#include "sokol_gfx.h"

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
static void log_message(const char*, uint32_t level, uint32_t, const char* message,
                        uint32_t, const char*, void*) {
    if (level <= 1) {
        std::fprintf(stderr, "sokol: %s\n", message ? message : "error");
        std::exit(2);
    }
}
struct Vertex { float position[4], color[4]; };
static const Vertex vertices[] = {
    {{-.8f,-.8f,.25f,1},{0,0,1,1}}, {{.8f,-.8f,.25f,1},{0,0,1,1}}, {{0,.8f,.25f,1},{0,0,1,1}},
    {{-.8f,-.8f,.75f,1},{1,0,0,1}}, {{.8f,-.8f,.75f,1},{1,0,0,1}}, {{0,.8f,.75f,1},{1,0,0,1}},
    {{-.8f,-.8f,.50f,1},{0,0,1,1}}, {{.8f,-.8f,.50f,1},{0,0,1,1}}, {{0,.8f,.50f,1},{0,0,1,1}},
};

int main() {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
    require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        requested, 1, D3D11_SDK_VERSION, &device, &level, &context)), "hardware D3D11 unavailable");
    sg_desc setup{};
    setup.environment.d3d11.device = device;
    setup.environment.d3d11.device_context = context;
    setup.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    setup.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH;
    setup.environment.defaults.sample_count = 1;
    setup.logger.func = log_message;
    sg_setup(&setup);
    require(sg_isvalid(), "sokol setup failed");

    sg_shader_desc shader_desc{};
    shader_desc.vertex_func.source =
        "struct V {float4 p:POSITION;float4 c:COLOR0;};"
        "struct O {float4 p:SV_Position;float4 c:COLOR0;};"
        "O main(V v){O o;o.p=v.p;o.c=v.c;return o;}";
    shader_desc.fragment_func.source =
        "float4 main(float4 p:SV_Position,float4 c:COLOR0):SV_Target0{return c;}";
    shader_desc.attrs[0].hlsl_sem_name = "POSITION";
    shader_desc.attrs[1].hlsl_sem_name = "COLOR";
    sg_shader shader = sg_make_shader(&shader_desc);
    require(sg_query_shader_state(shader) == SG_RESOURCESTATE_VALID, "shader failed");
    sg_pipeline_desc pipeline_desc{};
    pipeline_desc.shader = shader;
    pipeline_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT4;
    pipeline_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    pipeline_desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH;
    pipeline_desc.depth.compare = SG_COMPAREFUNC_GREATER;
    pipeline_desc.depth.write_enabled = true;
    pipeline_desc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    sg_pipeline pipeline = sg_make_pipeline(&pipeline_desc);
    require(sg_query_pipeline_state(pipeline) == SG_RESOURCESTATE_VALID, "pipeline failed");
    sg_buffer_desc buffer_desc{};
    buffer_desc.data = {vertices, sizeof vertices};
    sg_buffer buffer = sg_make_buffer(&buffer_desc);
    sg_bindings bindings{};
    bindings.vertex_buffers[0] = buffer;

    std::puts("{\"backend\":\"D3D11 hardware\",\"pixel_test\":\"GREATER, RGBA8, RGB565\",\"targets\":[");
    const int sizes[][2] = {{640,480},{64,64},{128,256},{1920,1080}};
    for (int t = 0; t < 4; ++t) {
        const int w = sizes[t][0], h = sizes[t][1];
        sg_image_desc image_desc{};
        image_desc.width = w; image_desc.height = h;
        image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        image_desc.usage.color_attachment = true;
        sg_image color = sg_make_image(&image_desc);
        image_desc.pixel_format = SG_PIXELFORMAT_DEPTH;
        image_desc.usage = {}; image_desc.usage.depth_stencil_attachment = true;
        sg_image depth = sg_make_image(&image_desc);
        sg_view_desc view_desc{};
        view_desc.color_attachment.image = color;
        sg_view color_view = sg_make_view(&view_desc);
        view_desc = {}; view_desc.depth_stencil_attachment.image = depth;
        sg_view depth_view = sg_make_view(&view_desc);
        auto native = sg_d3d11_query_image_info(color);
        auto* texture = (ID3D11Texture2D*)native.tex2d;
        require(texture != nullptr, "no native image");
        D3D11_TEXTURE2D_DESC staging_desc{};
        texture->GetDesc(&staging_desc);
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.BindFlags = 0; staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging_desc.MiscFlags = 0;
        ID3D11Texture2D* staging = nullptr;
        require(SUCCEEDED(device->CreateTexture2D(&staging_desc, nullptr, &staging)), "staging failed");
        std::vector<uint16_t> rgb565(static_cast<size_t>(w) * h);
        std::vector<double> times;
        UINT row_pitch = 0;
        for (int frame = 0; frame < 35; ++frame) {
            const auto start = std::chrono::steady_clock::now();
            sg_pass pass{};
            pass.attachments.colors[0] = color_view;
            pass.attachments.depth_stencil = depth_view;
            pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
            pass.action.colors[0].clear_value = {0,1,0,1};
            pass.action.depth.load_action = SG_LOADACTION_CLEAR;
            pass.action.depth.clear_value = 0;
            sg_begin_pass(&pass);
            sg_apply_pipeline(pipeline);
            sg_apply_bindings(&bindings);
            sg_draw(0, 9, 1);
            sg_end_pass();
            sg_commit();
            context->CopyResource(staging, texture);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)), "readback failed");
            row_pitch = mapped.RowPitch;
            for (int y = 0; y < h; ++y) {
                const auto* row = (const unsigned char*)mapped.pData + static_cast<size_t>(y) * row_pitch;
                for (int x = 0; x < w; ++x) {
                    const auto* p = row + x * 4;
                    rgb565[static_cast<size_t>(y) * w + x] =
                        static_cast<uint16_t>((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3);
                }
            }
            context->Unmap(staging, 0);
            sg_reset_state_cache();
            require(rgb565[static_cast<size_t>(h/2) * w + w/2] == 0xf800, "depth/center mismatch");
            require(rgb565[0] == 0x07e0 && rgb565.back() == 0x07e0, "clear/corner mismatch");
            // Top sample is outside this asymmetric triangle; bottom is inside.
            require(rgb565[static_cast<size_t>(h/4) * w + w/4] == 0x07e0, "top/origin mismatch");
            require(rgb565[static_cast<size_t>(3*h/4) * w + w/4] == 0xf800, "bottom/origin mismatch");
            if (frame >= 5) times.push_back(std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count());
        }
        std::sort(times.begin(), times.end());
        std::printf("%s{\"width\":%d,\"height\":%d,\"samples\":30,\"row_pitch\":%u,"
                    "\"draw_readback_convert_ms_p50\":%.4f,\"p95\":%.4f}",
                    t ? ",\n" : "", w, h, row_pitch, times[15], times[28]);
        staging->Release();
        sg_destroy_view(depth_view); sg_destroy_view(color_view);
        sg_destroy_image(depth); sg_destroy_image(color);
    }
    std::puts("\n],\"present_count\":0}");
    sg_destroy_buffer(buffer); sg_destroy_pipeline(pipeline); sg_destroy_shader(shader);
    sg_shutdown();
    context->Release(); device->Release();
}

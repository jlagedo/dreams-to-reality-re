#include "render/model_preview.h"
#include "platform/graphics_backend.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
static od::port::ModelMaterial material(uint16_t colour) {
    od::port::ModelMaterial m;
    m.bank.resize(0x18014);
    for (unsigned row = 0; row < 32; ++row) {
        const auto at = 0x14 + row * 0x400 + 6;
        m.bank[at] = uint8_t(colour);
        m.bank[at + 1] = uint8_t(colour >> 8);
        m.bank[at + 4] = 0xe0;
        m.bank[at + 5] = 7; // index 2 green
    }
    std::fill(m.bank.begin() + 0x8014, m.bank.end(), uint8_t(1));
    return m;
}
int main() {
    check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    od::GraphicsBackend backend;
    std::string error;
    check(backend.configure_window(error), "window configuration");
    auto *window = SDL_CreateWindow("Shared model adapter", 64, 64,
                                    SDL_WINDOW_HIDDEN | backend.window_flags());
    check(window != nullptr, SDL_GetError());
    check(backend.init(window, error), "backend initialization");
    sg_desc desc{};
    desc.environment = backend.environment();
    sg_setup(&desc);
    od::ModelPreview preview;
    check(preview.init(error, 64, 64), "preview init");
    od::port::ModelGraph graph;
    od::port::ModelNode node;
    node.local_rot = {32768, 0, 0, 0, 32768, 0, 0, 0, 32768};
    node.vertices = {{{-1, -1, 0}}, {{0, 1, 0}}, {{1, -1, 0}}};
    graph.nodes.push_back(node);
    auto child = node;
    child.parent = 0;
    child.local_xyz = {1, 0, 0};
    child.vertices = {{{0, -1, 0}}};
    graph.nodes.push_back(child);
    graph.materials.push_back(material(0xf800));
    od::port::ModelFace face;
    face.type = 3;
    face.material_index = 0;
    for (unsigned i = 0; i < 3; ++i)
        face.corners[i] = {0, i, 0, 0};
    graph.faces.push_back(face);
    face.corners[2] = {1, 0, 0, 0};
    graph.faces[0] = face;
    check(preview.load(graph, error), "preview graph load");
    od::ModelView view;
    view.yaw = view.pitch = 0;
    view.distance = 3;
    auto draw = [&]() {
        const bool ok = preview.draw(view, error);
        check(ok, error.c_str());
    };
    auto capture = [&]() {
        std::vector<uint32_t> pixels;
        const auto image = sg_query_view_desc(preview.texture_view()).texture.image;
        const bool ok = backend.read_image(image, 0, 0, preview.target_width(),
                                           preview.target_height(), pixels, error);
        check(ok, error.c_str());
        return pixels[pixels.size() / 2 + preview.target_width() / 2];
    };
    draw();
    sg_commit();
    check(capture() == 0xff0000f8, "original vertices through shared scene pipeline");
    auto changed_palette = material(0x001f).bank;
    preview.update_palette_rows(0, 1u << 15, changed_palette);
    draw();
    sg_commit();
    check(capture() == 0xff0000f8, "page-keyed palette cache changed without rebind");
    // Another material/face can appear in a later pose; old fixed draw layouts rejected this.
    graph.materials.push_back(material(0x001f));
    graph.nodes[0].vertices.insert(graph.nodes[0].vertices.end(),
                                   {{{-1, -1, 0}}, {{0, 1, 0}}, {{1, -1, 0}}});
    // Equal-depth later translucent face must fail GREATER and retain the red surface.
    auto added = face;
    added.type = -3;
    added.material_index = 1;
    graph.faces.push_back(added);
    check(preview.update_pose(graph, error), "dynamic draw batch change");
    draw();
    sg_commit();
    check(capture() == 0xff0000f8, "shared GREATER equality convention");
    // Empty draw lists are legitimate updates and must clear the previous scene.
    graph.faces.clear();
    check(preview.update_pose(graph, error), "empty draw list update");
    draw();
    sg_commit();
    check(capture() != 0xff0000f8, "empty frame retained previous geometry");
    graph.faces.push_back(face);
    check(preview.update_pose(graph, error), "restored draw list");
    draw();
    sg_commit();
    check(capture() == 0xfff80000, "palette staging lost across pose updates/page rebind");
    // Mutate a source twice and resize with pending work, without sg_update_image conflicts.
    draw();
    auto bank = graph.materials[0].bank;
    std::fill(bank.begin() + 0x8014, bank.end(), uint8_t(2));
    preview.update_material_pixels(0, bank);
    check(preview.resize_target(96, 64, error), "pending target resize");
    draw();
    bank.clear();
    sg_commit();
    check(capture() == 0xff00fc00, "immutable page version or resized target");
    for (int reload = 0; reload < 10; ++reload) {
        preview.clear_model();
        check(preview.load(graph, error), "reload");
        draw();
        sg_commit();
        check(preview.renderer_stats().live_resources == 3, "model resource growth across reloads");
    }
    // Framing fog uses original units, rather than normalized orbit coordinates.
    od::port::GlideFogState fog;
    fog.table_mode = true;
    fog.color = 0x0000ff;
    for (size_t i = 7; i < fog.table.size(); ++i)
        fog.table[i] = 255;
    preview.set_fog(fog);
    draw();
    sg_commit();
    check(capture() == 0xffff0000, "shared fog uses original model units");
    preview.set_fog({});
    preview.set_output_gamma(.8f);
    draw();
    sg_commit();
    check(std::abs(int(capture() & 255) - int(std::round(std::pow(248.0 / 255, 1.25) * 255))) <= 1,
          "final gamma");
    const auto narrow = od::with_horizontal_focal(view, 1.25f, 640, 480);
    const auto wide = od::with_horizontal_focal(view, 1.25f, 1920, 1080);
    check(narrow.focal_y == wide.focal_y && wide.focal_x < narrow.focal_x, "Hor+ preview camera");
    preview.shutdown();
    sg_commit(); // removed listener must not touch destroyed renderer
    sg_shutdown();
    backend.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::puts("model adapter: shared depth/fog/gamma, dynamic batches, source mutation, pending "
              "resize and ten reloads passed");
}

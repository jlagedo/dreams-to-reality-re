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
    // A complete source face must retain the three independent shade bytes,
    // including values above 31, rather than substituting its flat shade.
    std::vector<uint8_t> record(0x284);
    auto word = [&](size_t offset, uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte)
            record[offset + byte] = uint8_t(value >> (byte * 8));
    };
    word(0x14, 1);
    word(0x18, 0x40);
    word(0x40 + 0x90, 3);
    word(0x40 + 0x94, 0x130);
    word(0x40 + 0x9c, 0x1a8);
    word(0x40 + 0xb8, 0x200);
    word(0x204, 0x16);
    word(0x21c, 1);
    word(0x220, 0x240);
    word(0x22c, 68);
    word(0x248, 0x130);
    word(0x254, 0x158);
    word(0x260, 0x180);
    word(0x26c, 0x1b0);
    for (size_t corner = 0; corner < 3; ++corner)
        word(0x274 + corner * 4, 0x1c0);
    record[0x280] = 7;
    record[0x281] = 0;
    record[0x282] = 16;
    record[0x283] = 255;
    od::port::ModelGraph source_graph;
    std::string source_error;
    check(od::port::RES_Relocate(record, source_graph, source_error), "source shade relocation");
    check(source_graph.faces.size() == 1 && source_graph.faces[0].shade == 7 &&
              source_graph.faces[0].corner_shades == std::array<uint8_t, 3>{0, 16, 255},
          "source corner shades were discarded");
    record.pop_back();
    check(!od::port::RES_Relocate(record, source_graph, source_error),
          "truncated source corner shades accepted");
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
    for (int type = 0x16; type <= 0x18; ++type) {
        graph.faces[0].type = type;
        graph.faces[0].shade = 31; // Flat shade must not override the corners (including 0x18).
        graph.faces[0].corner_shades = {16, 16, 16};
        check(preview.update_pose(graph, error), "grayscale mode source update");
        draw();
        sg_commit();
        check(std::abs(int(capture() & 255) - 124) <= 1,
              "shared adapter grayscale modulation");
        graph.faces[0].corner_shades = {255, 255, 255};
        check(preview.update_pose(graph, error), "grayscale saturation update");
        draw();
        sg_commit();
        check(capture() == 0xff0000f8, "grayscale corner brightness must saturate");
        graph.faces[0].corner_shades = {0, 16, 31};
        check(preview.update_pose(graph, error), "unequal corner shade update");
        draw();
        sg_commit();
        check((capture() & 255) > 40 && (capture() & 255) < 200,
              "unequal source corner shades interpolate");
    }
    graph.faces[0] = face;
    check(preview.update_pose(graph, error), "restore unmodulated source");
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
    preview.set_output_gamma(1);
    // A diagnostic-only graph has flat faces and no texture/material binding.
    auto diagnostic = graph;
    diagnostic.faces.clear();
    diagnostic.materials.clear();
    auto small = face;
    small.type = 1;
    small.source_block = 7;
    small.material_index = SIZE_MAX;
    const size_t first_vertex = diagnostic.nodes[0].vertices.size();
    diagnostic.nodes[0].vertices.insert(diagnostic.nodes[0].vertices.end(),
                                         {{{-1, -1, 0}}, {{-1, 0, 0}}, {{0, -1, 0}}});
    for (unsigned c = 0; c < 3; ++c)
        small.corners[c] = {0, first_vertex + c, 0, 0};
    auto central = face;
    central.type = 1;
    central.source_block = 7;
    central.material_index = SIZE_MAX;
    auto reversed = central;
    std::swap(reversed.corners[0], reversed.corners[2]);
    diagnostic.flat_faces = {reversed, small, central};
    check(preview.load(diagnostic, error), "diagnostic-only flat graph load");
    draw();
    sg_commit();
    check(capture() == 0xfff94b02, "diagnostic colour counted culled faces or lost block identity");
    diagnostic.flat_faces.back().source_block = 8;
    check(preview.update_pose(diagnostic, error), "diagnostic source block change");
    draw();
    sg_commit();
    check(capture() == 0xff000000, "diagnostic colour failed to reset at source block");
    preview.shutdown();
    sg_commit(); // removed listener must not touch destroyed renderer
    sg_shutdown();
    backend.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::puts("model adapter: shared depth/fog/gamma, dynamic batches, source mutation, pending "
              "resize and ten reloads passed");
}

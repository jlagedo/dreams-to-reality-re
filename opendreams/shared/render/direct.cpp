#include "render/direct_sokol.h"
#include <direct.glsl.h>
#include "render/direct_shadow.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace {
struct Image {
    sg_image image{};
    sg_view texture{}, attachment{};
};
struct Resource {
    uint32_t generation = 1;
    bool alive = false, target = false, packed = false;
    int width = 0, height = 0, logical_width = 0, logical_height = 0;
    Image sides[2], depth;
    unsigned current = 0;
};
void destroy_image(Image &image) {
    if (image.texture.id)
        sg_destroy_view(image.texture);
    if (image.attachment.id)
        sg_destroy_view(image.attachment);
    if (image.image.id)
        sg_destroy_image(image.image);
    image = {};
}
void destroy_resource(Resource &r) {
    for (auto &i : r.sides)
        destroy_image(i);
    destroy_image(r.depth);
}
bool make_image(Image &image, int w, int h, sg_pixel_format fmt, bool target,
                const void *data = nullptr) {
    sg_image_desc d{};
    d.width = w;
    d.height = h;
    d.pixel_format = fmt;
    const bool depth = fmt == SG_PIXELFORMAT_DEPTH;
    d.usage.color_attachment = target && !depth;
    d.usage.depth_stencil_attachment = depth;
    if (data)
        d.data.mip_levels[0] = {data, size_t(w) * size_t(h) * 4};
    image.image = sg_make_image(&d);
    if (sg_query_image_state(image.image) != SG_RESOURCESTATE_VALID)
        return false;
    sg_view_desc v{};
    if (!depth) {
        v.texture.image = image.image;
        image.texture = sg_make_view(&v);
        if (sg_query_view_state(image.texture) != SG_RESOURCESTATE_VALID)
            return false;
    }
    if (target) {
        v = {};
        if (depth)
            v.depth_stencil_attachment.image = image.image;
        else
            v.color_attachment.image = image.image;
        image.attachment = sg_make_view(&v);
        if (sg_query_view_state(image.attachment) != SG_RESOURCESTATE_VALID)
            return false;
    }
    return true;
}
} // namespace

struct od_renderer {
    std::string error;
    std::vector<Resource> resources, retired;
    std::vector<sg_buffer> frame_buffers;
    sg_shader draw_shader{}, output_shader{}, scene_shader{};
    sg_shader shadow_shader{};
    sg_pipeline shadow_pipeline{};
    sg_shader mask_shader{};
    sg_pipeline mask_pipeline{};
    sg_pipeline draw_pipeline{}, output_pipeline{}, scene_pipeline{}, alpha_pipeline{};
    sg_pipeline output_rgba_pipeline{};
    sg_pipeline scene_culled_pipeline{}, alpha_culled_pipeline{};
    sg_sampler point{}, linear{}, repeat_linear{};
    Image dummy_packed, dummy_rgba;
    od_render_stats stats{};
    bool flip = false;
    int fail(const char *message) {
        error = message;
        return 0;
    }
    Resource *find(od_render_id id) {
        const uint32_t slot = uint32_t(id), generation = uint32_t(id >> 32);
        if (!slot || slot > resources.size())
            return nullptr;
        auto &r = resources[slot - 1];
        return r.alive && r.generation == generation ? &r : nullptr;
    }
    od_render_id add(Resource r) {
        r.alive = true;
        for (size_t i = 0; i < resources.size(); ++i)
            if (!resources[i].alive) {
                r.generation = resources[i].generation;
                resources[i] = r;
                ++stats.live_resources;
                return (uint64_t(r.generation) << 32) | uint32_t(i + 1);
            }
        resources.push_back(r);
        ++stats.live_resources;
        return (uint64_t(r.generation) << 32) | uint32_t(resources.size());
    }
    bool dimensions_ok(int w, int h) {
        const int limit = sg_query_limits().max_image_size_2d;
        return w > 0 && h > 0 && w <= limit && h <= limit;
    }
};

od_renderer *od_renderer_create() {
    if (!sg_isvalid())
        return nullptr;
    auto *r = new (std::nothrow) od_renderer;
    if (!r)
        return nullptr;
    r->flip = !sg_query_features().origin_top_left;
    r->draw_shader = sg_make_shader(direct_draw_shader_desc(sg_query_backend()));
    r->output_shader = sg_make_shader(direct_output_shader_desc(sg_query_backend()));
    r->scene_shader = sg_make_shader(direct_scene_shader_desc(sg_query_backend()));
    r->shadow_shader = sg_make_shader(direct_shadow_shader_desc(sg_query_backend()));
    r->mask_shader = sg_make_shader(direct_mask_shader_desc(sg_query_backend()));
    sg_pipeline_desc p{};
    p.shader = r->draw_shader;
    p.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    p.depth.pixel_format = SG_PIXELFORMAT_NONE;
    p.sample_count = 1;
    r->draw_pipeline = sg_make_pipeline(&p);
    p.shader = r->shadow_shader;
    r->shadow_pipeline = sg_make_pipeline(&p);
    p = {};
    p.shader = r->mask_shader;
    p.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    p.depth.pixel_format = SG_PIXELFORMAT_NONE;
    p.sample_count = 1;
    p.layout.buffers[0].stride = 2*sizeof(float)+12*sizeof(int32_t);
    p.layout.attrs[ATTR_direct_mask_position].format = SG_VERTEXFORMAT_FLOAT2;
    for (unsigned i = 0; i < 3; ++i) {
        p.layout.attrs[ATTR_direct_mask_edge0+i].format = SG_VERTEXFORMAT_INT4;
        p.layout.attrs[ATTR_direct_mask_edge0+i].offset = 2*sizeof(float)+i*4*sizeof(int32_t);
    }
    r->mask_pipeline = sg_make_pipeline(&p);
    p = {};
    p.shader = r->output_shader;
    r->output_pipeline = sg_make_pipeline(&p);
    p.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    p.depth.pixel_format = SG_PIXELFORMAT_NONE;
    p.sample_count = 1;
    r->output_rgba_pipeline = sg_make_pipeline(&p);
    p = {};
    p.shader = r->scene_shader;
    p.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    p.depth.pixel_format = SG_PIXELFORMAT_DEPTH;
    p.depth.compare = SG_COMPAREFUNC_GREATER;
    p.depth.write_enabled = true;
    p.sample_count = 1;
    p.layout.buffers[0].stride = 6 * sizeof(float);
    p.layout.attrs[ATTR_direct_scene_position].format = SG_VERTEXFORMAT_FLOAT3;
    p.layout.attrs[ATTR_direct_scene_texcoord].format = SG_VERTEXFORMAT_FLOAT2;
    p.layout.attrs[ATTR_direct_scene_texcoord].offset = 3 * sizeof(float);
    p.layout.attrs[ATTR_direct_scene_brightness].format = SG_VERTEXFORMAT_FLOAT;
    p.layout.attrs[ATTR_direct_scene_brightness].offset = 5 * sizeof(float);
    // Visibility/front-face decisions belong to the scene adapter/core, not
    // the source's old clipped lists. Initial stream is explicitly two-sided.
    r->scene_pipeline = sg_make_pipeline(&p);
    p.colors[0].blend.enabled = true;
    p.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    p.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    p.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    p.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ZERO;
    r->alpha_pipeline = sg_make_pipeline(&p);
    p.cull_mode = SG_CULLMODE_BACK;
    p.face_winding = SG_FACEWINDING_CCW;
    r->alpha_culled_pipeline = sg_make_pipeline(&p);
    p.colors[0].blend.enabled = false;
    r->scene_culled_pipeline = sg_make_pipeline(&p);
    sg_sampler_desc s{};
    s.min_filter = SG_FILTER_NEAREST;
    s.mag_filter = SG_FILTER_NEAREST;
    s.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    s.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    r->point = sg_make_sampler(&s);
    s.min_filter = SG_FILTER_LINEAR;
    s.mag_filter = SG_FILTER_LINEAR;
    r->linear = sg_make_sampler(&s);
    s.wrap_u = SG_WRAP_REPEAT;
    s.wrap_v = SG_WRAP_REPEAT;
    r->repeat_linear = sg_make_sampler(&s);
    const uint32_t zero = 0, white = 0xffffffffu;
    bool ok = make_image(r->dummy_packed, 1, 1, SG_PIXELFORMAT_R32UI, false, &zero) &&
              make_image(r->dummy_rgba, 1, 1, SG_PIXELFORMAT_RGBA8, false, &white);
    for (auto pipeline :
         {r->draw_pipeline, r->shadow_pipeline, r->mask_pipeline, r->output_pipeline, r->output_rgba_pipeline,
          r->scene_pipeline, r->alpha_pipeline, r->scene_culled_pipeline, r->alpha_culled_pipeline})
        ok = ok && sg_query_pipeline_state(pipeline) == SG_RESOURCESTATE_VALID;
    ok = ok && sg_query_sampler_state(r->point) == SG_RESOURCESTATE_VALID &&
         sg_query_sampler_state(r->linear) == SG_RESOURCESTATE_VALID &&
         sg_query_sampler_state(r->repeat_linear) == SG_RESOURCESTATE_VALID;
    if (!ok) {
        od_renderer_destroy(r);
        return nullptr;
    }
    return r;
}
void od_renderer_frame_complete(od_renderer *r) {
    if (!r)
        return;
    for (auto b : r->frame_buffers)
        sg_destroy_buffer(b);
    r->frame_buffers.clear();
    for (auto &resource : r->retired)
        destroy_resource(resource);
    r->retired.clear();
}
void od_renderer_destroy(od_renderer *r) {
    if (!r)
        return;
    od_renderer_frame_complete(r);
    for (auto &resource : r->resources)
        if (resource.alive)
            destroy_resource(resource);
    destroy_image(r->dummy_rgba);
    destroy_image(r->dummy_packed);
    for (auto p :
         {r->draw_pipeline, r->shadow_pipeline, r->mask_pipeline, r->output_pipeline, r->output_rgba_pipeline,
          r->scene_pipeline, r->alpha_pipeline, r->scene_culled_pipeline, r->alpha_culled_pipeline})
        if (p.id)
            sg_destroy_pipeline(p);
    for (auto s : {r->draw_shader, r->output_shader, r->scene_shader, r->shadow_shader, r->mask_shader})
        if (s.id)
            sg_destroy_shader(s);
    if (r->point.id)
        sg_destroy_sampler(r->point);
    if (r->linear.id)
        sg_destroy_sampler(r->linear);
    if (r->repeat_linear.id)
        sg_destroy_sampler(r->repeat_linear);
    delete r;
}
const char *od_renderer_error(const od_renderer *r) {
    return r ? r->error.c_str() : "renderer unavailable";
}
od_render_stats od_renderer_stats(const od_renderer *r) { return r ? r->stats : od_render_stats{}; }
od_render_id od_renderer_target(od_renderer *r, int w, int h, int lw, int lh) {
    if (!r)
        return 0;
    if (!r->dimensions_ok(w, h) || lw <= 0 || lh <= 0)
        return r->fail("invalid target dimensions");
    Resource t;
    t.target = true;
    t.width = w;
    t.height = h;
    t.logical_width = lw;
    t.logical_height = lh;
    if (!make_image(t.sides[0], w, h, SG_PIXELFORMAT_RGBA8, true) ||
        !make_image(t.sides[1], w, h, SG_PIXELFORMAT_RGBA8, true) ||
        !make_image(t.depth, w, h, SG_PIXELFORMAT_DEPTH, true)) {
        destroy_resource(t);
        return r->fail("target allocation failed");
    }
    for (auto &side : t.sides) {
        sg_pass pass{};
        pass.attachments.colors[0] = side.attachment;
        pass.attachments.depth_stencil = t.depth.attachment;
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {0, 0, 0, 1};
        pass.action.depth.load_action = SG_LOADACTION_CLEAR;
        pass.action.depth.clear_value = 0.0f;
        pass.action.depth.store_action = SG_STOREACTION_STORE;
        sg_begin_pass(&pass);
        sg_end_pass();
    }
    return r->add(t);
}
static od_render_id upload(od_renderer *r, int w, int h, const void *data, size_t pitch,
                           bool packed) {
    if (!r)
        return 0;
    if (!r->dimensions_ok(w, h) || !data || pitch < size_t(w) * 4 ||
        pitch > std::numeric_limits<size_t>::max() / size_t(h))
        return r->fail("invalid upload layout");
    std::vector<uint8_t> bytes(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        std::memcpy(bytes.data() + size_t(y) * w * 4,
                    static_cast<const uint8_t *>(data) + size_t(y) * pitch, size_t(w) * 4);
    Resource t;
    t.width = w;
    t.height = h;
    t.logical_width = w;
    t.logical_height = h;
    t.packed = packed;
    if (!make_image(t.sides[0], w, h, packed ? SG_PIXELFORMAT_R32UI : SG_PIXELFORMAT_RGBA8, false,
                    bytes.data())) {
        destroy_resource(t);
        return r->fail("upload allocation failed");
    }
    ++r->stats.uploads;
    r->stats.upload_bytes += bytes.size();
    return r->add(t);
}
od_render_id od_renderer_upload_packed(od_renderer *r, int w, int h, const uint32_t *p,
                                       size_t pitch) {
    return upload(r, w, h, p, pitch, true);
}
od_render_id od_renderer_upload_rgba(od_renderer *r, int w, int h, const void *p, size_t pitch) {
    return upload(r, w, h, p, pitch, false);
}
int od_renderer_release(od_renderer *r, od_render_id id) {
    if (!r)
        return 0;
    auto *t = r->find(id);
    if (!t)
        return r->fail("stale resource handle");
    r->retired.push_back(*t);
    uint32_t next = t->generation + 1;
    if (!next)
        next = 1;
    *t = {};
    t->generation = next;
    --r->stats.live_resources;
    return 1;
}
sg_view od_renderer_view(od_renderer *r, od_render_id id) {
    auto *t = r ? r->find(id) : nullptr;
    return t ? t->sides[t->current].texture : sg_view{};
}
sg_image od_renderer_image(od_renderer *r, od_render_id id) {
    auto *t = r ? r->find(id) : nullptr;
    return t ? t->sides[t->current].image : sg_image{};
}
int od_renderer_draw_2d(od_renderer *r, const od_draw_2d *c) {
    if (!r || !c)
        return 0;
    auto *t = r->find(c->target);
    auto *source = r->find(c->source);
    if (!t || !t->target)
        return r->fail("2D target is stale or not a target");
    if (c->kind < OD_DRAW_KEEP || c->kind > OD_DRAW_LOOKUP || c->format < OD_RGB565 ||
        c->format > OD_RGB555)
        return r->fail("unknown 2D operation/format");
    if (c->rect.width < 0 || c->rect.height < 0)
        return r->fail("negative 2D extent");
    const bool copy = c->kind == OD_DRAW_COPY || c->kind == OD_DRAW_DIM;
    const bool packed = c->kind == OD_DRAW_BLEND || c->kind == OD_DRAW_HALF ||
                        c->kind == OD_DRAW_MOVIE || c->kind == OD_DRAW_RAW ||
                        c->kind == OD_DRAW_LOOKUP;
    auto *lookup = r->find(c->lookup);
    if (c->kind == OD_DRAW_LOOKUP &&
        (!lookup || !lookup->packed || lookup->width != 256 || lookup->height != 28))
        return r->fail("lookup blend requires a 256x28 packed table snapshot");
    if (copy && (!source || !source->target))
        return r->fail("copy requires a GPU source target");
    if (packed && (!source || !source->packed || source->width < c->rect.width ||
                   source->height < c->rect.height))
        return r->fail("packed source is stale or too small");
    if (c->kind == OD_DRAW_DIM && c->parameter > 3)
        return r->fail("invalid dim level");
    sg_pass pass{};
    pass.attachments.colors[0] = t->sides[1 - t->current].attachment;
    pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
    draw_params_t p{};
    p.draw_rect[0] = float(c->rect.x);
    p.draw_rect[1] = float(c->rect.y);
    p.draw_rect[2] = float(c->rect.width);
    p.draw_rect[3] = float(c->rect.height);
    auto canvas = od_centered_canvas(t->width, t->height, t->logical_width, t->logical_height);
    if (c->full_surface)
        canvas = {0, 0, t->width, t->height};
    p.canvas[0] = float(canvas.x);
    p.canvas[1] = float(canvas.y);
    p.canvas[2] = float(canvas.width);
    p.canvas[3] = float(canvas.height);
    p.dimensions[0] = float(t->width);
    p.dimensions[1] = float(t->height);
    p.dimensions[2] = float(t->logical_width);
    p.dimensions[3] = float(t->logical_height);
    p.operation[0] = int(c->kind);
    p.operation[1] = int(c->parameter);
    p.operation[2] = int(c->format);
    p.operation[3] = r->flip;
    sg_bindings b{};
    b.views[VIEW_previous_tex] = t->sides[t->current].texture;
    b.views[VIEW_source_tex] =
        copy ? source->sides[source->current].texture : r->dummy_rgba.texture;
    b.views[VIEW_packed_tex] = packed ? source->sides[0].texture : r->dummy_packed.texture;
    b.views[VIEW_lookup_tex] =
        c->kind == OD_DRAW_LOOKUP ? lookup->sides[0].texture : r->dummy_packed.texture;
    b.samplers[SMP_point_smp] = r->point;
    sg_begin_pass(&pass);
    sg_apply_pipeline(r->draw_pipeline);
    sg_apply_bindings(&b);
    sg_range u{&p, sizeof p};
    sg_apply_uniforms(UB_draw_params, &u);
    sg_draw(0, 3, 1);
    sg_end_pass();
    t->current = 1 - t->current;
    ++r->stats.draws_2d;
    if (copy)
        ++r->stats.gpu_copies;
    return 1;
}
od_render_id od_renderer_resolve_shadow(od_renderer *r, od_render_id mask,
                                        const uint16_t *palette) {
    if (!r || !palette)
        return 0;
    auto *source = r->find(mask);
    if (!source || !source->target || source->width != 128 || source->height != 256)
        return r->fail("invalid packed shadow target");
    const sg_view source_view = source->sides[source->current].texture;
    const auto id = od_renderer_target(r, 128, 128, 128, 128);
    if (!id)
        return 0;
    auto *target = r->find(id);
    shadow_params_t p{};
    auto colour = [](uint16_t value, float *out) {
        out[0] = float(((value >> 11) & 31) * 8) / 255;
        out[1] = float(((value >> 5) & 63) * 4) / 255;
        out[2] = float((value & 31) * 8) / 255;
    };
    colour(palette[0], p.shadow_zero);
    colour(palette[1], p.shadow_one);
    p.shadow_zero[3] = 0;
    p.shadow_one[3] = palette[0] == palette[1] ? 0.0f : 1.0f;
    p.shadow_layout[0] = r->flip ? 1.0f : 0.0f;
    sg_bindings b{};
    b.views[VIEW_shadow_tex] = source_view;
    b.samplers[SMP_shadow_smp] = r->point;
    sg_pass pass{};
    pass.attachments.colors[0] = target->sides[0].attachment;
    pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
    sg_begin_pass(&pass);
    sg_apply_pipeline(r->shadow_pipeline);
    sg_apply_bindings(&b);
    sg_range u{&p, sizeof p};
    sg_apply_uniforms(UB_shadow_params, &u);
    sg_draw(0, 3, 1);
    sg_end_pass();
    ++r->stats.shadow_resolves;
    return id;
}
static int apply_output(od_renderer *r, Resource *t, float gamma, sg_pipeline pipeline) {
    sg_bindings b{};
    b.views[VIEW_output_tex] = t->sides[t->current].texture;
    b.samplers[SMP_output_smp] = r->point;
    output_params_t p{};
    p.output_mode[0] = 1.0f / gamma;
    p.output_mode[1] = r->flip ? 1.0f : 0.0f;
    sg_apply_pipeline(pipeline);
    sg_apply_bindings(&b);
    sg_range u{&p, sizeof p};
    sg_apply_uniforms(UB_output_params, &u);
    sg_draw(0, 3, 1);
    return 1;
}
int od_renderer_output(od_renderer *r, od_render_id id, float gamma) {
    auto *t = r ? r->find(id) : nullptr;
    if (!t || !t->target)
        return r ? r->fail("output target is stale") : 0;
    if (!std::isfinite(gamma) || gamma <= 0)
        return r->fail("invalid output gamma");
    return apply_output(r, t, gamma, r->output_pipeline);
}
int od_renderer_output_target(od_renderer *r, od_render_id source, od_render_id destination,
                              float gamma) {
    auto *src = r ? r->find(source) : nullptr;
    auto *dst = r ? r->find(destination) : nullptr;
    if (!src || !src->target || !dst || !dst->target || source == destination)
        return r ? r->fail("invalid output targets") : 0;
    if (!std::isfinite(gamma) || gamma <= 0)
        return r->fail("invalid output gamma");
    sg_pass pass{};
    pass.attachments.colors[0] = dst->sides[1 - dst->current].attachment;
    pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
    sg_begin_pass(&pass);
    apply_output(r, src, gamma, r->output_rgba_pipeline);
    sg_end_pass();
    dst->current = 1 - dst->current;
    return 1;
}
int od_renderer_shadow_mask(od_renderer *r, const od_shadow_packet *p) {
    if (!r || !p) return 0;
    auto *target = r->find(p->target);
    if (!target || !target->target || target->width != 128 || target->height != 256)
        return r->fail("invalid paired-P8 shadow target");
    std::vector<od::ShadowTriangle> triangles;
    if (!od::prepare_shadow(*p, triangles, r->error))
        return 0;
    struct Vertex {
        float xy[2];
        int32_t edges[3][4];
    };
    std::vector<Vertex> vertices;
    for (const auto &triangle : triangles) {
        int xmin = 128, xmax = 0, ymin = 256, ymax = 0;
        Vertex prototype{};
        for (unsigned i = 0; i < 3; ++i) {
            xmin = std::min(xmin, triangle[i][0]);
            xmax = std::max(xmax, triangle[i][0]);
            ymin = std::min(ymin, triangle[i][1]);
            ymax = std::max(ymax, triangle[i][1]);
            auto a = triangle[i], b = triangle[(i + 1) % 3];
            if (a[1] > b[1])
                std::swap(a, b);
            auto *edge = prototype.edges[i];
            edge[0] = a[1];
            edge[1] = b[1];
            if (a[1] != b[1]) {
                auto chop = [](double v) {
                    return v >= 2147483648.0 || v < -2147483648.0 ? INT32_MIN : int32_t(v);
                };
                edge[3] = chop((double(b[0]) - a[0]) * 4096.0 / (double(b[1]) - a[1]));
                edge[2] = chop(double(a[0]) * 4096.0 - double(edge[3]) * .5);
            }
        }
        xmin = std::max(0, xmin);
        xmax = std::min(128, xmax);
        ymin = std::max(0, ymin);
        ymax = std::min(256, ymax);
        if (xmin >= xmax || ymin >= ymax)
            continue;
        const int corners[6][2] = {{xmin, ymin}, {xmax, ymin}, {xmin, ymax},
                                   {xmin, ymax}, {xmax, ymin}, {xmax, ymax}};
        for (auto &corner : corners) {
            Vertex vertex = prototype;
            vertex.xy[0] = float(corner[0]);
            vertex.xy[1] = float(corner[1]);
            vertices.push_back(vertex);
        }
    }
    sg_buffer buffer{};
    if (!vertices.empty()) {
        sg_buffer_desc d{};
        d.data = {vertices.data(), vertices.size() * sizeof(Vertex)};
        buffer = sg_make_buffer(&d);
        if (sg_query_buffer_state(buffer) != SG_RESOURCESTATE_VALID) {
            sg_destroy_buffer(buffer);
            return r->fail("shadow vertex upload failed");
        }
        r->frame_buffers.push_back(buffer);
        ++r->stats.uploads;
        r->stats.upload_bytes += d.data.size;
    }
    sg_pass pass{};
    pass.attachments.colors[0] = target->sides[target->current].attachment;
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0, 0, 0, 1};
    sg_begin_pass(&pass);
    if (!vertices.empty()) {
        mask_params_t params{};
        params.mask_dimensions[0] = 128;
        params.mask_dimensions[1] = 256;
        sg_bindings bindings{};
        bindings.vertex_buffers[0] = buffer;
        sg_apply_pipeline(r->mask_pipeline);
        sg_apply_bindings(&bindings);
        sg_range uniform{&params, sizeof params};
        sg_apply_uniforms(UB_mask_params, &uniform);
        sg_draw(0, int(vertices.size()), 1);
        ++r->stats.scene_batches;
    }
    sg_end_pass();
    r->stats.scene_triangles += triangles.size();
    return 1;
}
int od_renderer_scene(od_renderer *r, const od_scene_packet *p) {
    if (!r || !p)
        return 0;
    if (!std::isfinite(p->fog_depth_scale) || p->fog_depth_scale < 0)
        return r->fail("invalid fog depth scale");
    auto *target = r->find(p->target);
    if (!target || !target->target)
        return r->fail("scene target is stale");
    if (p->triangle_count > 1000000 || p->node_count > 1000000 ||
        (p->vertex_count && !p->vertices) || (p->triangle_count && !p->triangles))
        return r->fail("invalid scene arrays");
    for (float v : p->view_projection)
        if (!std::isfinite(v))
            return r->fail("invalid scene camera");
    std::vector<float> world(p->node_count * 12);
    if (!od_compose_pose(p->nodes, p->node_count, world.data()))
        return r->fail("invalid posed hierarchy");
    struct Vertex {
        float xyz[3], uv[2], brightness;
    };
    std::vector<Vertex> vertices;
    vertices.reserve(p->triangle_count * 3);
    for (size_t i = 0; i < p->triangle_count; ++i) {
        const auto &tri = p->triangles[i];
        auto *texture = r->find(tri.texture);
        if (tri.texture && (!texture || texture->packed || tri.texture == p->target))
            return r->fail("invalid material texture");
        if (tri.mode < OD_FACE_OPAQUE || tri.mode > OD_FACE_TRANSLUCENT)
            return r->fail("unknown face mode");
        for (size_t j = 0; j < 3; ++j) {
            const auto &corner = tri.corners[j];
            if (corner.node >= p->node_count || corner.vertex >= p->vertex_count)
                return r->fail("unresolved face corner");
            const float *m = world.data() + size_t(corner.node) * 12;
            const float *v = p->vertices[corner.vertex].xyz;
            Vertex out{};
            for (int axis = 0; axis < 3; ++axis) {
                out.xyz[axis] = m[axis * 4] * v[0] + m[axis * 4 + 1] * v[1] +
                                m[axis * 4 + 2] * v[2] + m[axis * 4 + 3];
                if (!std::isfinite(out.xyz[axis]))
                    return r->fail("non-finite posed vertex");
            }
            std::copy_n(corner.uv, 2, out.uv);
            out.brightness = tri.use_corner_brightness
                                 ? float(tri.corner_brightness[j]) / 255.0f
                                 : 1.0f;
            if (!std::isfinite(out.uv[0]) || !std::isfinite(out.uv[1]))
                return r->fail("invalid texture coordinate");
            vertices.push_back(out);
        }
    }
    sg_buffer buffer{};
    if (!vertices.empty()) {
        sg_buffer_desc d{};
        d.data = {vertices.data(), vertices.size() * sizeof(Vertex)};
        buffer = sg_make_buffer(&d);
        if (sg_query_buffer_state(buffer) != SG_RESOURCESTATE_VALID) {
            sg_destroy_buffer(buffer);
            return r->fail("vertex upload failed");
        }
        r->frame_buffers.push_back(buffer);
        ++r->stats.uploads;
        r->stats.upload_bytes += d.data.size;
    }
    sg_pass pass{};
    pass.attachments.colors[0] = target->sides[target->current].attachment;
    pass.attachments.depth_stencil = target->depth.attachment;
    pass.action.colors[0].load_action = p->clear ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    std::copy_n(p->clear_colour, 4, &pass.action.colors[0].clear_value.r);
    pass.action.depth.load_action = p->clear ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.depth.clear_value = 0.0f;
    pass.action.depth.store_action = SG_STOREACTION_STORE;
    scene_vs_params_t vp{};
    std::copy_n(p->view_projection, 16, vp.view_projection);
    vp.depth_mode[0] = r->flip ? 1.0f : 0.0f;
    vp.depth_mode[1] = p->fog_depth_scale > 0 ? p->fog_depth_scale : 1.0f;
    sg_begin_pass(&pass);
    // Preserve the supplied order. The adapter puts deferred faces after the
    // opaque traversal; this layer never sorts translucent geometry by depth.
    for (size_t i = 0; i < p->triangle_count;) {
        const auto &tri = p->triangles[i];
        size_t end = i + 1;
        while (end < p->triangle_count) {
            const auto &next = p->triangles[end];
            if (next.texture != tri.texture || next.colour != tri.colour || next.mode != tri.mode ||
                next.wrap_texture != tri.wrap_texture || next.cull_back != tri.cull_back)
                break;
            ++end;
        }
        auto *texture = r->find(tri.texture);
        sg_bindings b{};
        b.vertex_buffers[0] = buffer;
        b.views[VIEW_material_tex] =
            texture ? texture->sides[texture->current].texture : r->dummy_rgba.texture;
        b.samplers[SMP_material_smp] = tri.wrap_texture ? r->repeat_linear : r->linear;
        scene_fs_params_t fp{};
        for (int a = 0; a < 4; ++a)
            fp.colour[a] = float((tri.colour >> (a * 8)) & 255) / 255.0f;
        fp.material_mode[0] = texture ? 1.0f : 0.0f;
        fp.material_mode[1] = tri.mode == OD_FACE_CHROMA ? 1.0f : 0.0f;
        fp.material_mode[2] = tri.mode == OD_FACE_TRANSLUCENT ? 128.0f / 255.0f : 1.0f;
        for (int a = 0; a < 3; ++a)
            fp.fog_colour[a] = float((p->fog.colour >> (8 * a)) & 255) / 255.0f;
        fp.fog_colour[3] = p->fog.enabled ? 1.0f : 0.0f;
        for (int entry = 0; entry < 64; ++entry)
            fp.fog_table[entry / 4][entry % 4] = p->fog.table[entry];
        const auto opaque = tri.cull_back ? r->scene_culled_pipeline : r->scene_pipeline;
        const auto alpha = tri.cull_back ? r->alpha_culled_pipeline : r->alpha_pipeline;
        sg_apply_pipeline(tri.mode == OD_FACE_TRANSLUCENT ? alpha : opaque);
        sg_apply_bindings(&b);
        sg_range u{&vp, sizeof vp};
        sg_apply_uniforms(UB_scene_vs_params, &u);
        u = {&fp, sizeof fp};
        sg_apply_uniforms(UB_scene_fs_params, &u);
        sg_draw(int(i * 3), int((end - i) * 3), 1);
        ++r->stats.scene_batches;
        i = end;
    }
    sg_end_pass();
    r->stats.scene_triangles += p->triangle_count;
    return 1;
}

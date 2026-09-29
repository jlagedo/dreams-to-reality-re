#ifndef OD_DIRECT_RENDER_H
#define OD_DIRECT_RENDER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Host-only API. The caller owns the sokol context and presentation clock.
 * All functions run on that context's thread. Inputs are consumed before
 * return; handles identify immutable upload versions or mutable targets. */
typedef struct od_renderer od_renderer;
typedef uint64_t od_render_id;
typedef enum od_readback_reason {
    OD_READBACK_THUMBNAIL,
    OD_READBACK_CAPTURE,
    OD_READBACK_ORACLE
} od_readback_reason;
typedef enum od_pixel_format { OD_RGB565, OD_RGB555 } od_pixel_format;
typedef struct od_rect {
    int x, y, width, height;
} od_rect;
typedef struct od_readback_request {
    od_render_id target;
    od_rect region;
    od_readback_reason reason;
    uint32_t call_site;
} od_readback_request;
typedef enum od_draw_kind {
    OD_DRAW_KEEP = 0,
    OD_DRAW_BLEND = 1,
    OD_DRAW_HALF = 2,
    OD_DRAW_FILL = 3,
    OD_DRAW_COPY = 4,
    OD_DRAW_DIM = 5,
    OD_DRAW_MOVIE = 6,
    OD_DRAW_CAPTION = 7,
    OD_DRAW_BAND = 8,
    OD_DRAW_RAW = 9,
    OD_DRAW_LOOKUP = 10
} od_draw_kind;
typedef struct od_draw_2d {
    od_draw_kind kind;
    od_render_id target, source;
    od_rect rect; /* Logical target coordinates, source starts at (0,0). */
    od_pixel_format format;
    uint32_t parameter;    /* fill colour, dim level, or darken flag */
    uint32_t full_surface; /* frame operation covers drawable instead of UI safe area */
    od_render_id lookup;   /* OD_DRAW_LOOKUP: 256x28 R32UI blend-table snapshot */
} od_draw_2d;
typedef struct od_render_stats {
    uint64_t uploads, upload_bytes, scene_triangles, draws_2d, gpu_copies;
    uint64_t scene_batches;
    uint64_t shadow_resolves;
    uint32_t live_resources;
} od_render_stats;

/* Floating-point, row-major affine transform; parent -1 is a model root.
 * The camera is never included in these transforms. */
typedef struct od_pose_node {
    int32_t parent;
    float local[12];
} od_pose_node;
typedef struct od_scene_vertex {
    float xyz[3];
} od_scene_vertex;
typedef struct od_scene_corner {
    uint32_t node, vertex;
    float uv[2];
} od_scene_corner;
typedef enum od_face_mode { OD_FACE_OPAQUE, OD_FACE_CHROMA, OD_FACE_TRANSLUCENT } od_face_mode;
typedef struct od_scene_fog {
    uint32_t enabled, colour; /* colour 0xAABBGGRR; fog does not change alpha */
    uint8_t table[64];        /* SST1 reciprocal-W selection and wrapped 8-bit deltas */
} od_scene_fog;
typedef struct od_scene_triangle {
    od_scene_corner corners[3];
    od_render_id texture; /* RGBA8 upload; 0 selects the flat colour. */
    uint32_t colour;      /* 0xAABBGGRR */
    od_face_mode mode;
    uint32_t wrap_texture; /* clamp=0, repeat=1, after palette expansion */
    uint32_t cull_back;    /* original triangles, independent of retail visible lists */
} od_scene_triangle;
typedef struct od_scene_packet {
    od_render_id target;
    const od_pose_node *nodes;
    size_t node_count;
    const od_scene_vertex *vertices;
    size_t vertex_count;
    const od_scene_triangle *triangles;
    size_t triangle_count;
    float view_projection[16]; /* Column-major; GPU depth range [0,1]. */
    float clear_colour[4];
    int clear;
    od_scene_fog fog;
    float fog_depth_scale; /* 0 means 1; preview normalization converts W back to source units */
} od_scene_packet;

od_renderer *od_renderer_create(void);
void od_renderer_destroy(od_renderer *renderer);
const char *od_renderer_error(const od_renderer *renderer);
od_render_stats od_renderer_stats(const od_renderer *renderer);
od_render_id od_renderer_target(od_renderer *, int width, int height, int logical_width,
                                int logical_height);
/* Source uint32: low 16 bits colour, high 16 bits coverage. This API does
 * not interpret retail descriptors or marker bytes; the adapter does. */
od_render_id od_renderer_upload_packed(od_renderer *, int width, int height, const uint32_t *,
                                       size_t pitch);
od_render_id od_renderer_upload_rgba(od_renderer *, int width, int height, const void *,
                                     size_t pitch);
int od_renderer_release(od_renderer *, od_render_id);
int od_renderer_draw_2d(od_renderer *, const od_draw_2d *);
int od_renderer_scene(od_renderer *, const od_scene_packet *);
/* Final colour correction into a distinct RGBA8 target. No commit/present. */
int od_renderer_output_target(od_renderer *, od_render_id source, od_render_id target, float gamma);
/* 128x256 index-0/1 shadow -> 128-square material LOD, on the GPU. Source
 * words represent paired horizontal P8 indices; LOD uses every second row. */
od_render_id od_renderer_resolve_shadow(od_renderer *, od_render_id mask,
                                        const uint16_t palette[256]);
/* After the host's sg_commit: retire objects released during this frame. */
void od_renderer_frame_complete(od_renderer *);

/* Pure host helpers, also used by input mapping and CPU oracle tests. */
od_rect od_centered_canvas(int width, int height, int logical_width, int logical_height);
int od_canvas_point(od_rect viewport, int logical_width, int logical_height, float x, float y,
                    float *logical_x, float *logical_y);
uint16_t od_pack_colour(uint32_t rgba, od_pixel_format);
uint32_t od_expand_colour(uint16_t packed, od_pixel_format);
/* P8 palette lookup precedes bilinear filtering. LOD 128 samples the even
 * texels of the 256-square source; zero-entry COLOUR supplies the chroma key.
 * Palette expansion is Glide R/B*8, G*4, deliberately not bit replication. */
int od_expand_material_page(const uint8_t *indices, size_t source_pitch, const uint16_t *palette,
                            uint32_t *rgba, int lod);
int od_compose_pose(const od_pose_node *, size_t count, float *world_affines);
/* Homogeneous clip-space triangle, depth [0,w], CCW front winding.
 * Returns -1 for invalid input, 0 for clipped/back-facing/degenerate, 1 otherwise.
 * This is geometric visibility, not occlusion or sample-coverage testing. */
int od_triangle_visible(const float clip_vertices[12], int cull_back);
typedef struct od_radial_light {
    int32_t position[3]; /* owner-local, after the recovered light transform */
    int32_t inner_radius, outer_radius, intensity;
} od_radial_light;
/* Retail flat-shade kernel. Original local corners/normal/plane, including
 * integer centroid and multiply wrapping. Returns the resulting shade byte. */
int od_radial_flat_shade(const int32_t vertices[9], const int32_t normal[3], int32_t plane,
                         const od_radial_light *, size_t count, uint8_t *shade);
/* Retail light transform uses the rotation transpose, even for non-unit
 * matrices. Input affine is renderer-composed world space, without camera. */
int od_radial_light_local(const float world_affine[12], const int32_t world_position[3],
                          int32_t local[3]);
int32_t od_radial_normal_dot(const int32_t normal[3], const od_radial_light *light);
int od_projection_hor_plus(float vertical_focal, float aspect, float near_plane, float far_plane,
                           float matrix[16]);

#ifdef __cplusplus
}
#endif
#endif

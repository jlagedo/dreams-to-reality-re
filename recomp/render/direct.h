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
    uint64_t stitched_edges;
    uint64_t stitch_cpu_nanoseconds;
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
    uint8_t corner_brightness[3]; /* optional iterated RGB, 0..255 at each corner */
    uint8_t use_corner_brightness;
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
    /* Optional visual compatibility: join near-coincident opaque boundary
     * edges at intersecting face planes within this world-space source unit.
     * Zero disables. Separate model roots and parallel/alpha layers never join.
     * Guest transforms, topology, metadata and draw ordering are unchanged. */
    float source_edge_quantum;
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
/* Inclusive logical-pixel endpoints on the centered UI canvas. Packed stores
 * preserve RGB555's high bit. Coordinates must lie in [-32767,32767]. */
int od_renderer_line_2d(od_renderer *, od_render_id target, int x0, int y0, int x1, int y1,
                        uint16_t colour, od_pixel_format);
int od_renderer_scene(od_renderer *, const od_scene_packet *);
/* Paired-P8 shadow appearance: exact source Q15 poses and integer local
 * vertices. The renderer derives projection/coverage internally; no guest
 * projected vertices, span lists or CPU mask are accepted. Node 0 is camera. */
typedef struct od_shadow_node {
    int32_t parent, rotation[9], translation[3];
} od_shadow_node;
typedef struct od_shadow_vertex { uint32_t node; int32_t xyz[3]; } od_shadow_vertex;
typedef struct od_shadow_triangle {
    uint32_t vertices[3], owner, flags;
    int32_t normal[3], plane, retained_dot;
    uint32_t stale_normal;
} od_shadow_triangle;
typedef struct od_shadow_packet {
    od_render_id target;
    const od_shadow_node *nodes; size_t node_count;
    const od_shadow_vertex *vertices; size_t vertex_count;
    const od_shadow_triangle *triangles; size_t triangle_count;
    int32_t camera_rotation[9], camera_eye[3], center[2], near_plane, far_plane;
    float focal[2];
} od_shadow_packet;
int od_renderer_shadow_mask(od_renderer *, const od_shadow_packet *);
/* Optional outputs: per-node translation+rotation (12 ints), per-vertex
 * camera XYZ (3 floats), screen XY (2 ints). Oracle/metadata preparation. */
int od_shadow_project(const od_shadow_packet *, int32_t *, float *, int32_t *);
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
/* The DOS 3dfx build's palette-row generation (DREAMSFX.EXE), which decides
 * the colours Glide binds. Its Windows twin differs: it has no project +0xc8
 * branch, refreshes row 16 instead of 15 and never has scale 0. */
typedef struct od_dos_palette_update {
    int32_t rgb[3];             /* caller's current global R, G, B offsets */
    int32_t actor_rgb[3];       /* bound actor +0x98/+0x9c/+0xa0, read when actor_bound */
    int32_t actor_scale, scale; /* 0x19f710, 0x19f718: project +0xc0, +0xc4, default 0 */
    uint32_t cursor;            /* 0xf6354: rolling row cursor, 0..31 */
    uint8_t actor_bound;        /* slot flag 0x10 */
    uint8_t effect_light;       /* 0xfe790: an effect light exists */
    uint8_t rebuild;            /* scene still loading, or the exit countdown runs */
    uint8_t lit_project;        /* project +0xc8 non-zero */
} od_dos_palette_update;
/* REND_ApplyPaletteOffsets (0x2f933), RGB565 branch: one row from 256 BGR0
 * source colours; channel = clamp(c + 2 * clamp(offset, -127, 127), 0, 255). */
void od_dos_palette_apply(const uint8_t source[1024], int32_t r, int32_t g, int32_t b,
                          uint16_t row[256]);
/* REND_UpdatePaletteRows (0x3fca4) for one material slot. bank is the 32
 * physical rows below the page (row 0 at page - 0x8000); Glide binds row =
 * shade. Returns a bit mask of the rows written. */
uint32_t od_dos_palette_rows(const od_dos_palette_update *, const uint8_t source[1024],
                             uint16_t bank[32 * 256]);
/* The rows a face lit by an attack light binds, indexed by its shade 0..31.
 * Host rule, not a port: the offsets the DOS routine gives row 15 with no
 * effect light (the unlit row) plus the Windows step, shade * scale >> 2, the
 * one REND_UpdatePaletteRows (0x42e8b1) uses when it rebuilds all 32 rows.
 * Row 0 equals that unlit row. scale is the Windows one (0x626300 scene,
 * 0x6262e8 actor). The update's effect_light, rebuild and cursor are unused. */
void od_lit_palette_rows(const od_dos_palette_update *, const uint8_t source[1024], int32_t scale,
                         uint16_t bank[32 * 256]);
/* Homogeneous clip-space triangle, depth [0,w], CCW front winding.
 * Returns -1 for invalid input, 0 for clipped/back-facing/degenerate, 1 otherwise.
 * This is geometric visibility, not occlusion or sample-coverage testing. */
int od_triangle_visible(const float clip_vertices[12], int cull_back);
typedef struct od_radial_light {
    int32_t position[3]; /* owner-local, after the recovered light transform */
    int32_t inner_radius, outer_radius, intensity;
} od_radial_light;
typedef struct od_local_light {
    od_radial_light radial;
    uint32_t type;   /* retail 1 radial, 2 oriented with radial range; other: inactive slot */
    int32_t axis[3]; /* owner-local Q15 direction, retail l_ldirection */
} od_local_light;
/* carry is retail's per-light contribution variable, which lives for the whole
 * REND_LightObject call: an active light stores min(contribution, 0) in it, an
 * inactive slot adds whatever it still holds. NULL starts a private one at 0. */
int od_flat_light_shade(const int32_t vertices[9], const int32_t normal[3], int32_t plane,
                        const od_local_light *, size_t count, int32_t *carry, uint8_t *shade);
int32_t od_light_normal_dot(const int32_t normal[3], const od_local_light *light);
/* One 0x16/0x17 corner contribution. normal_dot is the current normal +0xc
 * scratch (possibly stale for a normal outside the owner's refreshed pool).
 * The caller accumulates positive contributions with byte wrapping per light. */
int32_t od_gouraud_light_contribution(const int32_t vertex[3], const int32_t normal[3],
                                     int32_t normal_dot, const od_local_light *light);
/* Retail post-draw sphere-map UV update. Parent camera-space Q15 rotation,
 * source corner normal, exact int32 UV output. Corners 1/2 spill normals to
 * float in retail; corner 0 retains integer precision on the x87 stack. */
int od_environment_uv(const int32_t parent_rotation[9], const int32_t normal[3],
                       unsigned corner, int32_t uv[2]);
int od_oriented_light_axis(const float world_affine[12], const int32_t orientation[9],
                           int32_t local_axis[3]);
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

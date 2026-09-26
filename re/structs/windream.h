/* Verified, partial layouts for Dreams to Reality's 32-bit Windows build.
 *
 * This header is parsed into WINDREAM.EXE's Ghidra Data Type Manager by
 * ghidra_scripts/ImportStructs.java. Unknown spans stay byte arrays on purpose.
 * Field comments note evidence and unresolved semantics; do not promote a
 * candidate field name to a confirmed meaning without a new code/data trace.
 */
#ifndef DREAMS_WINDREAM_STRUCTS_H
#define DREAMS_WINDREAM_STRUCTS_H

typedef unsigned char  dreams_u8;
typedef unsigned short dreams_u16;
typedef unsigned int   dreams_u32;
typedef signed char    dreams_i8;
typedef signed short   dreams_i16;
typedef signed int     dreams_i32;
typedef float          dreams_f32;
typedef double         dreams_f64;

/* Zero-run decoded DREAMS.DAT record, exactly 0x2200 bytes. */
typedef struct DREAMS_ProjectHeader {
    char project_name[16];                 /* +0x000 */
    dreams_u8 unknown_010[8];               /* +0x010 */
    dreams_i32 light1_direction[3];         /* +0x018 */
    dreams_i32 light2_direction[3];         /* +0x024 */
    dreams_i32 ambient_rgb[3];              /* +0x030 */
    /* Six 16-byte slots; their video/material roles overlap in old notes. */
    char scene_asset_name_slots[6][16];     /* +0x03c .. +0x09b */
    dreams_i32 camera_projection_mode;      /* +0x09c */
    dreams_i32 camera_near_clip;            /* +0x0a0 */
    dreams_i32 camera_fov_degrees;          /* +0x0a4 */
    dreams_u8 unknown_0a8[12];              /* +0x0a8 */
    dreams_i32 player_spawn_xyz[3];         /* +0x0b4 */
    dreams_u8 unknown_0c0[32];              /* +0x0c0 */
    dreams_i32 fog_parameters[4];           /* +0x0e0 */
    dreams_i32 clear_color[4];              /* +0x0f0 */
    dreams_u8 unknown_100[12];              /* +0x100 */
    dreams_i32 player_spawn_heading;        /* +0x10c */
    dreams_u8 unknown_110[40];              /* +0x110 */
    dreams_i32 lighting_mode;               /* +0x138 */
    dreams_u8 unknown_13c[0xbc];            /* +0x13c */
    dreams_i32 redbook_track;               /* +0x1f8 */
    dreams_u8 unknown_1fc[4];               /* +0x1fc */
} DREAMS_ProjectHeader;

typedef struct DREAMS_LinkRecord {
    char name[12];                          /* +0x00 */
    char destination_project[12];           /* +0x0c */
    dreams_u8 unknown_018[12];              /* +0x18 */
    dreams_i32 volume_min_xyz[3];            /* +0x24 */
    dreams_i32 volume_max_xyz[3];            /* +0x30 */
    dreams_u8 unknown_03c[0x44];             /* +0x3c */
} DREAMS_LinkRecord;

typedef struct DREAMS_ObjetRecord {
    char name[12];                          /* +0x00 */
    char asset_name[16];                    /* +0x0c */
    char instance_label[16];                /* +0x1c */
    dreams_u8 unknown_02c[8];                /* +0x2c */
    dreams_u16 flags;                       /* +0x34 */
    dreams_u8 unknown_036[6];                /* +0x36 */
    dreams_i32 collision_radius;             /* +0x3c */
    dreams_i32 spawn_xyz[3];                 /* +0x40 */
    dreams_u8 unknown_04c[16];               /* +0x4c */
    dreams_i32 heading_12bit;                /* +0x5c */
    dreams_u8 unknown_060[4];                /* +0x60 */
    dreams_i32 behavior_selector;            /* +0x64 */
    dreams_u32 movement_parameter_raw;       /* +0x68; runtime use is model-dependent */
    dreams_u32 parameter_6c_unresolved;      /* +0x6c; not confirmed as a BOX route index */
    dreams_u32 behavior_parameter_70;        /* +0x70; selector-5 launch magnitude input */
    dreams_u8 animation_and_state[0x28];     /* +0x74 .. +0x9b */
    dreams_u8 unknown_09c[0x24];             /* +0x9c .. +0xbf */
} DREAMS_ObjetRecord;

typedef struct DREAMS_BoxRecord {
    char name[12];                          /* +0x00 */
    dreams_u8 unknown_00c[24];               /* +0x0c */
    dreams_i32 points_xyz[16][3];            /* +0x24 */
    dreams_i32 point_count;                  /* +0xe4 */
    dreams_u8 unknown_0e8[8];                /* +0xe8 */
    dreams_i32 path_kind;                    /* +0xf0: 0 ground, 1 aerial */
    dreams_u8 unknown_0f4[12];               /* +0xf4 */
} DREAMS_BoxRecord;

typedef struct DREAMS_LinkAdventRecord {
    char name[12];                          /* +0x00 */
    dreams_u8 unknown_00c[8];                /* +0x0c */
    dreams_i32 target_object_slot;           /* +0x14 */
    dreams_u8 unknown_018[4];                /* +0x18 */
    dreams_i32 dialogue_id_or_opcode_arg;    /* +0x1c: one-based DRD ID for opcode 0x40 */
    dreams_i32 condition_opcode;              /* +0x20 */
    dreams_i32 action_parameter;              /* +0x24 */
    dreams_u8 unknown_028[4];                /* +0x28 */
    char cutscene_filename[16];              /* +0x2c */
    dreams_u8 unknown_03c[4];                /* +0x3c */
} DREAMS_LinkAdventRecord;

typedef struct DREAMS_ProjectRecord {
    DREAMS_ProjectHeader header;             /* +0x0000, size 0x0200 */
    DREAMS_LinkRecord links[8];              /* +0x0200, size 0x0400 */
    DREAMS_ObjetRecord objects[16];          /* +0x0600, size 0x0c00 */
    DREAMS_BoxRecord boxes[12];              /* +0x1200, size 0x0c00 */
    DREAMS_LinkAdventRecord adventures[16];  /* +0x1e00, size 0x0400 */
} DREAMS_ProjectRecord;

/* Shared decompressed .DSN/.DAN scene-graph node header; vertices follow at +0xf0. */
typedef struct CryoSceneNodeHeader {
    dreams_u8 unknown_000[0x24];             /* +0x00 */
    dreams_u32 parent_reference;              /* +0x24; stored address/reference */
    dreams_u32 first_child_reference;         /* +0x28 */
    dreams_u32 next_sibling_reference;        /* +0x2c */
    dreams_i32 local_translation_xyz[3];      /* +0x30 */
    dreams_i32 local_rotation_q15[3][3];      /* +0x3c */
    dreams_i32 exported_world_translation[3];/* +0x60; zero in shipped exports */
    dreams_i32 exported_world_rotation[3][3]; /* +0x6c; copy of local matrix on disk */
    dreams_u32 vertex_count;                  /* +0x90 */
    dreams_u32 vertex_array_reference;         /* +0x94 */
    dreams_u32 unknown_098;                    /* +0x98 */
    dreams_u32 vertex_array_end_check;         /* +0x9c: equals +0x94 + 40*count */
    dreams_u8 unknown_0a0[0x24];              /* +0xa0 */
    dreams_i32 bounding_sphere_radius;         /* +0xc4 */
    dreams_i32 bounding_sphere_center_xyz[3];  /* +0xc8 */
    dreams_u32 vertex_record_stride;            /* +0xd4: literal 40 */
    dreams_u8 unknown_0d8[0x18];               /* +0xd8 */
} CryoSceneNodeHeader;

/* ---- Scene-graph node at run time (docs/scene-geometry.md, "The engine's view").
 * The runtime node starts 0x14 bytes into CryoSceneNodeHeader: runtime +X is
 * file +X+0x14. Every pointer is relocated by MDL_RelocNode (0x455d6c). */

/* 40-byte vertex; node +0x80, count +0x7c. */
typedef struct MDL_Vertex {
    dreams_u32 flags;          /* +0x00 outcodes: 0x10 behind near, 0x20 past far, 0x40 to project, 0x80 shared with children */
    dreams_i32 local_xyz[3];   /* +0x04 node space, integer units */
    dreams_f32 camera_xyz[3];  /* +0x10 written by REND_TransformClipVertices */
    dreams_i32 screen_x;       /* +0x1c */
    dreams_i32 screen_y;       /* +0x20 */
    dreams_f32 inv_z;          /* +0x24 K/z, the perspective factor the rasterizers read */
} MDL_Vertex;

/* 16-byte normal; vertex normals at node +0x90 (count +0x8c), face normals at +0x98 (count +0x94). */
typedef struct MDL_Normal {
    dreams_i32 xyz_q15[3];     /* +0x00 unit vector, 32768 = 1.0 */
    dreams_i32 eye_dot;        /* +0x0c n . eye, written each frame by 0x478f80; 0 on disk */
} MDL_Normal;

/* 8-byte texture coordinate: texels in 16.16 on the 256x256 page. */
typedef struct MDL_UV {
    dreams_i32 u;
    dreams_i32 v;
} MDL_UV;

/* 100-byte shared edge: rasterizer scratch, set up by the first face that
 * draws it and reused by its neighbour. Only +0x04 is read before written. */
typedef struct MDL_Edge {
    dreams_u8 unknown_00[4];
    dreams_u32 setup;          /* +0x04 0 = not yet set up this frame */
    dreams_u8 scratch_08[0x5c];
} MDL_Edge;

/* 68-byte face record, an element of MDL_FaceBlock.records. */
typedef struct MDL_Face {
    dreams_u32 flags;          /* +0x00 1 culled, 2 skip, 8 normal recomputed from the vertices */
    struct MDL_Face *next;     /* +0x04 visible-list link; 0 ends the list */
    MDL_Vertex *v0;            /* +0x08 */
    MDL_Normal *n0;            /* +0x0c corner normal */
    MDL_Edge *e0;              /* +0x10 */
    MDL_Vertex *v1;            /* +0x14 */
    MDL_Normal *n1;            /* +0x18 */
    MDL_Edge *e1;              /* +0x1c */
    MDL_Vertex *v2;            /* +0x20 */
    MDL_Normal *n2;            /* +0x24 */
    MDL_Edge *e2;              /* +0x28 */
    MDL_Normal *plane;         /* +0x2c face normal */
    dreams_i32 plane_d;        /* +0x30 n . v0 >> 15; back-facing when eye_dot - plane_d < 0 */
    MDL_UV *uv[3];             /* +0x34 */
    dreams_u8 shade;           /* +0x40 light level, used when the node has lights */
    dreams_u8 unknown_41[3];
} MDL_Face;

/* Primitive block; node +0xa4 heads a list of them (edges: +0xa8). */
typedef struct MDL_FaceBlock {
    struct MDL_FaceBlock *next;/* +0x00 */
    dreams_i32 type;           /* +0x04 rasterizer selector; 3 = perspective-textured (all levels) */
    void *material;            /* +0x08 texture-page slot, or the colour word for flat types (0x4554e0) */
    char name[16];             /* +0x0c equals a material name */
    dreams_u32 count;          /* +0x1c */
    MDL_Face *records;         /* +0x20 */
    MDL_Face *visible;         /* +0x24 list head, reset to records each frame (0x47b0bc) */
    dreams_u8 unknown_28[4];
    dreams_u32 stride;         /* +0x2c 68 (0x38 for types -2, 1, 4, 0x11, 0x1b) */
    void *owner;               /* +0x30 relocated; points at the node's edge block */
} MDL_FaceBlock;

typedef struct MDL_Node {
    dreams_u8 unknown_00[0x0c];
    dreams_u32 flags;          /* +0x0c cull bits (0x478980): 8 outside, 0x20 inside, 0x40 crosses near/far; +0x0d: 8 env-map, 0x10 skip the face hook */
    struct MDL_Node *parent;   /* +0x10 */
    struct MDL_Node *child;    /* +0x14 */
    struct MDL_Node *sibling;  /* +0x18 */
    dreams_i32 local_xyz[3];   /* +0x1c */
    dreams_i32 local_rot[3][3];/* +0x28 Q15 */
    dreams_i32 view_xyz[3];    /* +0x4c composed each frame by REND_DrawObject */
    dreams_i32 view_rot[3][3]; /* +0x58 */
    dreams_u32 vertex_count;   /* +0x7c */
    MDL_Vertex *vertices;      /* +0x80 */
    dreams_u32 unknown_84;
    MDL_Vertex *vertices_end;  /* +0x88 */
    dreams_u32 vnormal_count;  /* +0x8c */
    MDL_Normal *vnormals;      /* +0x90 */
    dreams_u32 fnormal_count;  /* +0x94 */
    MDL_Normal *fnormals;      /* +0x98 */
    dreams_u32 unknown_9c;
    void *unknown_a0;          /* +0xa0 relocated when non-zero */
    MDL_FaceBlock *faces;      /* +0xa4 */
    MDL_FaceBlock *edges;      /* +0xa8 edge block: count +0x08, records +0x0c, stride 100 at +0x10 */
    dreams_u32 unknown_ac;
    dreams_i32 radius;         /* +0xb0 bounding sphere */
    dreams_i32 centre[3];      /* +0xb4 */
    dreams_u32 vertex_stride;  /* +0xc0 40 */
    dreams_u32 light_count;    /* +0xc4 */
    dreams_u8 lights[8];       /* +0xc8 indices into the 0x94-byte light table at 0x672700 */
    dreams_u32 shade;          /* +0xd0 used as the face shade when light_count is 0 */
} MDL_Node;

/* .3DI collision mesh (resource type 5), relocated by 0x455fb4 from resource +0x14. */
typedef struct COLL_Triangle {
    dreams_u32 v[3];           /* +0x00 pointers to 12-byte points */
    dreams_u32 normal;         /* +0x0c pointer */
    dreams_u8 unknown_10[0x34];
    dreams_u32 link;           /* +0x44 pointer, relocated */
    dreams_u8 unknown_48[0x18];
} COLL_Triangle;

typedef struct DAN_TrackHeader {
    dreams_u32 unknown_00[5];                 /* +0x00 */
    dreams_u32 duration_frames;                /* +0x14 */
    dreams_u32 rotation_key_count;             /* +0x18 */
    dreams_u32 translation_key_count;          /* +0x1c */
    dreams_u32 rotation_keys_offset;           /* +0x20, relative to payload +0x14 */
    dreams_u32 translation_keys_offset;        /* +0x24, relative to payload +0x14 */
} DAN_TrackHeader;

typedef struct DAN_RotationKey20 {
    dreams_u32 frame_index;
    dreams_i32 quaternion_q15[4];
} DAN_RotationKey20;

typedef struct DAN_RotationKey60 {
    dreams_u32 frame_index;
    dreams_i32 quaternion_q15[4];
    dreams_u32 curve_control[2];
    dreams_i32 outgoing_control_q15[4];
    dreams_i32 incoming_control_q15[4];
} DAN_RotationKey60;

typedef struct DAN_TranslationKey16 {
    dreams_u32 frame_index;
    dreams_i32 translation_xyz[3];
} DAN_TranslationKey16;

typedef struct DAN_TranslationKey48 {
    dreams_u32 frame_index;
    dreams_i32 translation_xyz[3];
    dreams_u8 unknown_010[32];
} DAN_TranslationKey48;

/* Partial runtime actor layout. Unknown bytes preserve offsets; this is not a
 * claim that every actor class has identical semantics for every field. */
typedef struct RuntimeActorObserved {
    dreams_u8 unknown_000[0x1c];              /* +0x000 */
    dreams_u32 unknown_01c;                   /* +0x01c; no confirmed maximum-vitality meaning */
    dreams_u8 unknown_020[0x14];              /* +0x020 */
    dreams_i32 behavior_class;                 /* +0x034 */
    dreams_f32 vitality_current;               /* +0x038 */
    dreams_f32 magic_current;                  /* +0x03c */
    dreams_f32 oxygen_current;                 /* +0x040 */
    dreams_u8 unknown_044[12];                 /* +0x044 */
    dreams_f32 hud_state_or_blink;             /* +0x050 */
    dreams_u8 unknown_054[0x54];               /* +0x054 */
    dreams_u8 flags_a8;                        /* +0x0a8 */
    dreams_u8 flags_a9;                        /* +0x0a9 */
    dreams_u8 flags_aa;                        /* +0x0aa */
    dreams_u8 flags_ab;                        /* +0x0ab */
    dreams_u8 flags_ac;                        /* +0x0ac */
    dreams_u8 flags_ad;                        /* +0x0ad */
    dreams_u8 flags_ae;                        /* +0x0ae */
    dreams_u8 flags_af;                        /* +0x0af */
    dreams_u8 unknown_0b0[0x54];               /* +0x0b0 */
    dreams_u32 movement_parameter_104;         /* +0x104 */
    dreams_u32 unknown_108;                    /* +0x108 */
    dreams_i32 launch_magnitude_10c;            /* +0x10c */
    dreams_u8 unknown_110[8];                  /* +0x110 */
    dreams_i32 target_distance_parameter_118;   /* +0x118 */
    dreams_u8 unknown_11c[12];                 /* +0x11c */
    dreams_u8 previous_root_position_128[12];  /* +0x128 */
    dreams_u8 unknown_134[0x24];               /* +0x134 */
    dreams_i32 model_family_158;                /* +0x158 */
    dreams_i32 current_action_15c;              /* +0x15c */
    dreams_i32 requested_action_160;            /* +0x160 */
    dreams_i32 transition_weight_164;           /* +0x164 */
    dreams_u8 unknown_168[8];                  /* +0x168 */
    dreams_f32 current_frame_170;               /* +0x170 */
    dreams_f32 incoming_frame_174;              /* +0x174 */
    dreams_f32 playback_speed_178;              /* +0x178 */
    dreams_u8 unknown_17c[0x18];                /* +0x17c */
    dreams_i32 random_action_threshold_194;     /* +0x194 */
    dreams_u8 unknown_198[4];                  /* +0x198 */
    dreams_u32 group_member_count_19c;          /* +0x19c */
    dreams_u32 group_members_1a0[3];            /* +0x1a0 */
    dreams_u32 group_controller_1ac;             /* +0x1ac */
    dreams_u32 ai_status_bits_1b0;               /* +0x1b0 */
    dreams_i32 ai_mode_1b4;                      /* +0x1b4 */
    dreams_u8 unknown_1b8[4];                  /* +0x1b8 */
    dreams_i32 previous_ai_mode_1bc;            /* +0x1bc */
    dreams_u8 unknown_1c0[0x10];                /* +0x1c0 */
    dreams_u32 current_target_1d0;               /* +0x1d0 */
    dreams_u32 damage_source_1d4;                /* +0x1d4 */
    dreams_u8 unknown_1d8[4];                  /* +0x1d8 */
    dreams_u32 target_candidates_1dc[5];         /* +0x1dc */
    dreams_u32 secondary_candidates_1f0[5];      /* +0x1f0 */
    dreams_u32 candidate_count_204;              /* +0x204 */
    dreams_u32 secondary_candidate_count_208;    /* +0x208 */
    dreams_u8 unknown_20c[0x2c];               /* +0x20c */
    dreams_f64 velocity_xyz_238[3];              /* +0x238, +0x240, +0x248 */
} RuntimeActorObserved;

#endif

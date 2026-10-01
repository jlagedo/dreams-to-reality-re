/* Original July 1997 DREAMSFX.EXE Watcom type layouts.
 * Extracted with re/tools/watcom_types.py; docs/research/wip-editor-discovery.md.
 * Original member names and offsets; external pointees remain opaque void *.
 * These are demo types, not replacements for retail layouts. */
#pragma pack(push, 1)

typedef struct WIP_C3D_VERTEX WIP_C3D_VERTEX;
typedef struct WIP_C3D_BOX WIP_C3D_BOX;
typedef struct WIP_objet WIP_objet;
typedef struct WIP_light WIP_light;
typedef struct WIP_CSPHERE WIP_CSPHERE;
typedef struct WIP_C3D_ANIM_ROT WIP_C3D_ANIM_ROT;
typedef struct WIP_C3D_ANIM_POS WIP_C3D_ANIM_POS;
typedef struct WIP_C3D_ANIM_ROT2 WIP_C3D_ANIM_ROT2;
typedef struct WIP_C3D_ANIM_POS2 WIP_C3D_ANIM_POS2;

/* E:\ENGINE\BEN11\3DC_FX.C; 40 bytes. */
struct WIP_C3D_VERTEX {
    int v_flags; /* +0x00, 4 bytes */
    int v_local[3]; /* +0x04, 12 bytes */
    float v_global[3]; /* +0x10, 12 bytes */
    int v_screen[2]; /* +0x1c, 8 bytes */
    float v_unz; /* +0x24, 4 bytes */
};

/* E:\ENGINE\BEN11\3DC_FX.C; 280 bytes. */
struct WIP_C3D_BOX {
    float v0[8][3]; /* +0x00, 96 bytes */
    int Xe[8]; /* +0x60, 32 bytes */
    int Ye[8]; /* +0x80, 32 bytes */
    float v[8][3]; /* +0xa0, 96 bytes */
    float minv[3]; /* +0x100, 12 bytes */
    float maxv[3]; /* +0x10c, 12 bytes */
};

/* E:\ENGINE\BEN11\3DC_FX.C; 220 bytes. */
struct WIP_objet {
    unsigned char o_nom[12]; /* +0x00, 12 bytes */
    int o_flags; /* +0x0c, 4 bytes */
    WIP_objet *o_ptr_pere; /* +0x10, 4 bytes */
    WIP_objet *o_ptr_fils; /* +0x14, 4 bytes */
    WIP_objet *o_ptr_frere; /* +0x18, 4 bytes */
    int o_lpos[3]; /* +0x1c, 12 bytes */
    int o_lmat[3][3]; /* +0x28, 36 bytes */
    int o_gpos[3]; /* +0x4c, 12 bytes */
    int o_gmat[3][3]; /* +0x58, 36 bytes */
    int o_nbr_vertex; /* +0x7c, 4 bytes */
    WIP_C3D_VERTEX *o_ptr_vertex; /* +0x80, 4 bytes */
    int o_nbr_uvtext; /* +0x84, 4 bytes */
    void *o_ptr_uvtext; /* +0x88, 4 bytes */
    int o_nbr_vnorm; /* +0x8c, 4 bytes */
    void *o_ptr_vnorm; /* +0x90, 4 bytes */
    int o_nbr_fnorm; /* +0x94, 4 bytes */
    void *o_ptr_fnorm; /* +0x98, 4 bytes */
    int o_nbr_box; /* +0x9c, 4 bytes */
    WIP_C3D_BOX *o_ptr_box; /* +0xa0, 4 bytes */
    void *o_ptr_face; /* +0xa4, 4 bytes */
    void *o_ptr_edge; /* +0xa8, 4 bytes */
    int o_user; /* +0xac, 4 bytes */
    int o_rayon; /* +0xb0, 4 bytes */
    int o_center[3]; /* +0xb4, 12 bytes */
    int o_size_vertex; /* +0xc0, 4 bytes */
    int o_nb_lights; /* +0xc4, 4 bytes */
    unsigned char o_light[8]; /* +0xc8, 8 bytes */
    int o_illum; /* +0xd0, 4 bytes */
    int o_nbr_virt_vertex; /* +0xd4, 4 bytes */
    WIP_C3D_VERTEX *o_ptr_virt_vertex; /* +0xd8, 4 bytes */
};

/* E:\ENGINE\BEN11\3DC_LIGH.C; 148 bytes. */
struct WIP_light {
    int l_flags; /* +0x00, 4 bytes */
    int l_lpos[3]; /* +0x04, 12 bytes */
    int l_lmat[3][3]; /* +0x10, 36 bytes */
    int l_gpos[3]; /* +0x34, 12 bytes */
    int l_gmat[3][3]; /* +0x40, 36 bytes */
    int l_temp[3]; /* +0x64, 12 bytes */
    int l_ldirection[3]; /* +0x70, 12 bytes */
    int l_gdirection[3]; /* +0x7c, 12 bytes */
    int l_near; /* +0x88, 4 bytes */
    int l_far; /* +0x8c, 4 bytes */
    int l_intensity; /* +0x90, 4 bytes */
};

/* E:\ENGINE\BEN11\3DC_COL2.C; 56 bytes. */
struct WIP_CSPHERE {
    int v[3]; /* +0x00, 12 bytes */
    int r; /* +0x0c, 4 bytes */
    unsigned char flag; /* +0x10, 1 bytes */
    unsigned char _padding_11[3];
    unsigned char axis_position[3][8]; /* +0x14, 24 bytes */
    void *overlap_head; /* +0x2c, 4 bytes */
    void *collision_head; /* +0x30, 4 bytes */
    void *over_head; /* +0x34, 4 bytes */
};

/* E:\ENGINE\BEN11\3DC_MEM.C; 20 bytes. */
struct WIP_C3D_ANIM_ROT {
    int key; /* +0x00, 4 bytes */
    int rot[4]; /* +0x04, 16 bytes */
};

/* E:\ENGINE\BEN11\3DC_MEM.C; 16 bytes. */
struct WIP_C3D_ANIM_POS {
    int key; /* +0x00, 4 bytes */
    int pos[3]; /* +0x04, 12 bytes */
};

/* E:\ENGINE\BEN11\3DC_MEM.C; 60 bytes. */
struct WIP_C3D_ANIM_ROT2 {
    int key; /* +0x00, 4 bytes */
    int rot[4]; /* +0x04, 16 bytes */
    float easeTo; /* +0x14, 4 bytes */
    float easeFrom; /* +0x18, 4 bytes */
    int ta[4]; /* +0x1c, 16 bytes */
    int tb[4]; /* +0x2c, 16 bytes */
};

/* E:\ENGINE\BEN11\3DC_MEM.C; 48 bytes. */
struct WIP_C3D_ANIM_POS2 {
    int key; /* +0x00, 4 bytes */
    int pos[3]; /* +0x04, 12 bytes */
    float easeTo; /* +0x10, 4 bytes */
    float easeFrom; /* +0x14, 4 bytes */
    int din[3]; /* +0x18, 12 bytes */
    int dout[3]; /* +0x24, 12 bytes */
};

#pragma pack(pop)

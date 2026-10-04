/*
 * WINDREAM recompilation - the Dreams Editor menu of Develop
 * (docs/specs/008-editor-restoration/spec.md, phase 1).
 *
 * Retail keeps Cryo's editor (WORKS.C: the tree walker 0x44d346, the row
 * drawer 0x44d1a6, the leaf editor 0x44cd62) but only two menu nodes: the
 * root "Dreams Editor" (0x4a47c4) and "Exit To DOS" (0x4a4784). The July 1997
 * demo keeps the whole menu. The build extracts it, binds every leaf to its
 * retail address (recomp/windream/editor/bindings.tsv, with the retail reader
 * that proves each binding) and puts the result beside the executable as
 * resources/editor-tree.tsv. In Develop, before the game starts, this file
 * builds those nodes in guest memory and hangs them under the retail root.
 *
 * Host-only structure, all of it data (no retail instruction changes):
 *   - the nodes, 0x40 bytes each as retail lays them out (label[24], five
 *     child pointers, value pointer, min, max, mask, flags; spec 008 A4.5),
 *     in one shim_alloc block;
 *   - the host cells, one dword each (shim_alloc): the six Option > Camera
 *     sliders that are shown but not wired (A4.4) point at them, each
 *     starting at the value of the retail global it stands for;
 *   - the root's five child pointers (0x4a47dc..0x4a47ec): the July root's
 *     branches with the retail exit node last. No retail code writes them;
 *     the walker only sets bit 0 of each node's flags (open or selected).
 * Without the resource (a build made without the July demo), or when it fails
 * a check, nothing is written and the editor shows the retail menu.
 *
 * What the Windows builds dropped from the July DOS editor, ported (owner,
 * 2026-10-04: what the Windows version does not do is ported; checked live,
 * spec 008 phase 1):
 *   - The sliders. Each slider row draws eight 16-px track segments and a
 *     knob (leaf editor 0x44ce22, 0x44d09f) by calling 0x402406 with the
 *     sprite descriptor _Objet0 (0x661da8: x +0x2c, y +0x2e, mode +0x34,
 *     sprite +0x3e: 0x9400 track, 0x9800 knob) and the data of sprite set 3,
 *     [_TableObjetIdent + 0xc] + 0x2c (0x62bdb8). DOS called _ZoomSpriteL16
 *     there (spritea.asm, July 0x1ca86); both Windows builds left an empty
 *     function (July 0x402386, retail 0x402406), and retail's
 *     InitWorksSprite_ (0x44d57e) loads particle.spr three times instead of
 *     the three July sets, so set 3, alphabe2.spr, whose descriptor retail
 *     kept (0x4a4684, index 3), is never loaded and the first slider row
 *     read address 0x2c and crashed. The host loads it with retail's own
 *     LoadFileSpr_ (0x43eea0) at the first editor frame, and sprite_blit
 *     below replaces 0x402406 with the DOS blit (lift/replacements.py
 *     HOST_ENTRIES). If the file cannot be loaded, 0x62bdb8 points at a
 *     zeroed block and the rows show label and value only.
 *   - The fonts. The editor prints its title in font slot 0 and its rows and
 *     values in slot 1 (TEXT_DrawString 0x44d5c7 -> TEXT_PrintAt, which adds
 *     2 below 640 wide), the same code as July's CompPrintSprite_ and
 *     GPrintf_. July's InitGame_ loaded COURE.016, DOSAPP.008, SMALLE.008 and
 *     SMALLE.006 into slots 0-3 with 1 px of letter spacing; retail loads
 *     HI640, HI480, HI320, HI320 with none, whose HI480 capitals (13 px)
 *     overlap at the compiled 10-px row spacing. The July files ship in
 *     resources/fonts (owner, 2026-10-04; the build checks their SHA-256).
 *     The host copies them into the data tree's DATA\FONT if missing (the
 *     developers' tree had them there), loads them at the first editor frame
 *     with retail's TEXT_LoadFont (0x425c61) into slots 4-7, which retail
 *     leaves empty (and so sprite sets 4-7: SPR_LoadSet's table has 24, retail
 *     uses 0-3 and 10), and while the inserted editor call runs (user.c
 *     wd_editor_frame) slots 0-3 hold the records of slots 4-7: the editor
 *     prints exactly as July did. Without the files, slot 1 holds slot 3's
 *     record (HI320, capitals 8 px) instead, which fits the rows.
 *
 * Resource format (written by editor_tree.py resource_text): '#' comments;
 * "version 1", "source <SHA-256 of the July DREAMS.EXE>", "bindings <SHA-256>",
 * "nodes <n>"; then one tab-separated row per node, depth first:
 *   index kind address min max mask flags children label
 * kind is root (index 0, the retail root: only its children are written),
 * branch, value (bound to a retail address), cell (a host cell starting at
 * the retail global at address) or retail (the existing exit node).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RECOMP_GENERATED_CODE   /* the guest registers: sprite_blit reads its stack arguments */
#include "host.h"
#include "render_boundary.h"

#define ROOT_VA     0x004A47C4u
#define EXIT_VA     0x004A4784u
#define EXIT_VALUE  0x004A4780u
#define NODE_SIZE   0x40u
#define LABEL_SIZE  24
#define NCHILD      5
#define MAX_NODES   512
#define IMAGE_LO    0x00401000u   /* code through the end of .bss: data, globals, working records */
#define IMAGE_HI    0x006B1400u
#define RESOURCE    "resources/editor-tree.tsv"
#define SLIDER_SET  0x0062BDB8u   /* _TableObjetIdent[3]: sprite set 3, the sliders' sprites */
#define SET3_DESCRIPTOR 0x004A4684u /* index 3, "data\objet\alphabe2.spr": retail data, never loaded */
#define LOAD_FILE_SPR   0x0043EEA0u /* LoadFileSpr_: reads the file, palette x4, fills the tables */
#define SPRITE_BLIT 0x00402406u   /* empty in the Windows build; the DOS _ZoomSpriteL16 */
#define VIDEO_WIDTH 0x0049D9FCu   /* the frame buffer's width and height (SPR_BlitSprite's pitch is 2 x width) */
#define VIDEO_HEIGHT 0x0049DA00u
#define TEXT_LOAD_FONT 0x00425C61u /* TEXT_LoadFont(name, slot, letter spacing), __watcall */
#define JULY_SLOT   4             /* the July fonts' first slot: 4-7 */
#define NFONTS      4

/* July InitGame_'s fonts, slot 0 to 3 (editor_tree.py FONTS). */
static const char* const k_fonts[NFONTS] = {"COURE.016", "DOSAPP.008", "SMALLE.008", "SMALLE.006"};
static int g_july_fonts;   /* 0 none, 1 in the data tree (load pending), 2 loaded into slots 4-7 */
static void july_fonts_install(void);
#define FONT_SLOTS  0x005EA4BCu   /* TEXT_LoadFont's slot records: 0 HI640, 1 HI480, 2 and 3 HI320 */
#define FONT_SLOT_SIZE 0x40Cu

/* The July demo's DREAMS.EXE the resource must come from (editor_tree.py JULY_SHA256). */
static const char k_source[] = "45d00510f993d321d2288b0791a96297893de9e5e0dc24d36246e41e144008ce";

typedef enum { K_ROOT, K_BRANCH, K_VALUE, K_CELL, K_RETAIL } Kind;
typedef struct {
    Kind kind;
    uint32_t address, mask, flags;
    int32_t min, max;
    int nchild, child[NCHILD];
    char label[LABEL_SIZE];
} Node;

static Node g_nodes[MAX_NODES];
static uint32_t g_set3_fallback;   /* a zeroed block for 0x62bdb8 if alphabe2.spr cannot be loaded; 0 = no menu */
static int g_set3_tried;

static int parse_u32(const char* s, int base, uint32_t* out) {
    char* end;
    errno = 0;
    unsigned long v = strtoul(s, &end, base);
    if (errno || end == s || *end || v > 0xFFFFFFFFul) return 0;
    *out = (uint32_t)v;
    return 1;
}
static int parse_i32(const char* s, int32_t* out) {
    char* end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end || v < INT32_MIN || v > INT32_MAX) return 0;
    *out = (int32_t)v;
    return 1;
}

/* Split a line on tabs in place; returns the field count (at most cap, the
 * last field keeps any further tabs). */
static int split(char* line, char** field, int cap) {
    int n = 0;
    field[n++] = line;
    for (char* p = line; *p && n < cap; p++)
        if (*p == '\t') { *p = 0; field[n++] = p + 1; }
    return n;
}

static int parse_children(char* s, Node* node) {
    node->nchild = 0;
    if (!strcmp(s, "-")) return 1;
    for (char* p = s; ; ) {
        char* comma = strchr(p, ',');
        if (comma) *comma = 0;
        uint32_t c;
        if (node->nchild == NCHILD || !parse_u32(p, 10, &c) || c == 0 || c >= MAX_NODES) return 0;
        node->child[node->nchild++] = (int)c;
        if (!comma) return 1;
        p = comma + 1;
    }
}

/* At most 24 characters: a label that fills the field is ended by the first
 * child pointer, which must then be 0 (checked by the caller), as five July
 * leaves rely on. */
static int parse_label(const char* s, Node* node) {
    size_t n = strlen(s);
    if (n > LABEL_SIZE) return 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7E) return 0;
    memset(node->label, 0, sizeof node->label);
    memcpy(node->label, s, n);
    return 1;
}

/* Parse and check the whole resource into g_nodes; returns the node count, or
 * 0 after printing the first problem. */
static int parse_resource(char* text, const char* path) {
    static const char* kinds[] = {"root", "branch", "value", "cell", "retail"};
    int n = 0, declared = -1, version = 0, source = 0, line_no = 0;
    char* next;
    for (char* line = text; line; line = next) {
        line_no++;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r') line[--len] = 0;
        if (!len || line[0] == '#') continue;
        char* f[9];
        int nf = split(line, f, 9);
        const char* problem = NULL;
        if (nf == 2 && !strcmp(f[0], "version")) version = !strcmp(f[1], "1");
        else if (nf == 2 && !strcmp(f[0], "source")) source = !strcmp(f[1], k_source);
        else if (nf == 2 && !strcmp(f[0], "bindings")) ;
        else if (nf == 2 && !strcmp(f[0], "nodes")) {
            uint32_t v;
            if (!parse_u32(f[1], 10, &v) || v == 0 || v > MAX_NODES) problem = "bad node count";
            else declared = (int)v;
        } else if (nf == 9) {
            Node* node = &g_nodes[n];
            uint32_t index;
            int k;
            if (!version) problem = "not version 1";
            else if (!source) problem = "not made from the July 1997 DREAMS.EXE";
            else if (declared < 0 || n >= declared) problem = "more rows than declared";
            else if (!parse_u32(f[0], 10, &index) || index != (uint32_t)n) problem = "rows out of order";
            else {
                for (k = 0; k < 5 && strcmp(f[1], kinds[k]); k++) {}
                if (k == 5) problem = "unknown kind";
                else if (!parse_u32(f[2], 16, &node->address) || !parse_i32(f[3], &node->min) ||
                         !parse_i32(f[4], &node->max) || !parse_u32(f[5], 16, &node->mask) ||
                         !parse_u32(f[6], 16, &node->flags)) problem = "bad number";
                else if (!parse_children(f[7], node)) problem = "bad children";
                else if (!parse_label(f[8], node) || (strlen(f[8]) == LABEL_SIZE && node->nchild))
                    problem = "label not printable or too long";
                else {
                    node->kind = (Kind)k;
                    if ((n == 0) != (node->kind == K_ROOT)) problem = "the root is row 0 and only row 0";
                    else if (node->kind == K_ROOT && node->address != ROOT_VA) problem = "root not at 0x4a47c4";
                    else if (node->kind == K_RETAIL && (node->address != EXIT_VA || node->nchild))
                        problem = "the only retail node is the exit node 0x4a4784";
                    else if ((node->kind == K_VALUE || node->kind == K_CELL) &&
                             (node->address < IMAGE_LO || node->address >= IMAGE_HI || (node->address & 3) || node->nchild))
                        problem = "value outside the retail image";
                    else if (node->kind == K_BRANCH && node->address) problem = "branch with a value";
                    else n++;
                }
            }
        } else problem = "unknown line";
        if (problem) {
            fprintf(stderr, "[editor] %s:%d: %s; keeping the retail menu\n", path, line_no, problem);
            return 0;
        }
    }
    if (declared != n) {
        fprintf(stderr, "[editor] %s: %d rows, %d declared; keeping the retail menu\n", path, n, declared);
        return 0;
    }
    for (int i = 0; i < n; i++)
        for (int c = 0; c < g_nodes[i].nchild; c++)
            if (g_nodes[i].child[c] >= n) {
                fprintf(stderr, "[editor] %s: node %d names child %d of %d; keeping the retail menu\n", path, i,
                        g_nodes[i].child[c], n);
                return 0;
            }
    return n;
}

/* 0x402406 (descriptor, sprite file, zoom, x0, y0, x1, y1, packed, selector,
 * frame buffer), arguments on the stack, the caller pops them: the July DOS
 * _ZoomSpriteL16 (spritea.asm, 0x1ca86) for what its callers use.
 *   sprite  the descriptor's word +0x3e: high byte = entry of the file's table
 *           at +0x408 (offsets from +0x400); a negative word draws the entry
 *           of its negation mirrored when the file's dword +0x404 is set
 *   record  width, height, hotspot x, y, then width x height palette indices,
 *           0 transparent; palette: the file's first 256 dwords (x4 by
 *           LoadFileSpr_) -> RGB565 as DOS did
 *   place   left = x(+0x2c) - hotspot x (mirrored: right = x + hotspot x),
 *           top = y(+0x2e) - hotspot y; sizes x 256 / zoom (0x100 = 1:1,
 *           below 0x80 nothing), sampled nearest; clipped to [x0, x1) x
 *           [y0, y1) and to the frame (pitch 2 x width, as SPR_BlitSprite)
 *   mode    descriptor +0x34 bit 0x2000: half the sprite, half the frame
 *           (mask 0xf7df); otherwise a plain copy. The editor's callers get
 *           0x4000 there (WorksEdit_ sets it for the cursor every frame), the
 *           plain copy. DOS's mode without either bit, which tints the
 *           palette from the frame and blends the edges, is reached by no
 *           retail caller and not ported; nor is the packed source (arg 8).
 * Host-only structure: a whole-function replacement of a retail stub. */
static void sprite_blit(void) {
    uint32_t sp = g_esp;
    uint32_t desc = WD_HOST_READ32(sp + 4), file = WD_HOST_READ32(sp + 8), zoom = WD_HOST_READ32(sp + 12);
    int32_t x0 = (int32_t)WD_HOST_READ32(sp + 16), y0 = (int32_t)WD_HOST_READ32(sp + 20);
    int32_t x1 = (int32_t)WD_HOST_READ32(sp + 24), y1 = (int32_t)WD_HOST_READ32(sp + 28);
    uint32_t packed = WD_HOST_READ32(sp + 32), fb = WD_HOST_READ32(sp + 40);
    g_esp += 4;   /* RET */
    int32_t fw = (int32_t)WD_HOST_READ32(VIDEO_WIDTH), fh = (int32_t)WD_HOST_READ32(VIDEO_HEIGHT);
    if (!desc || !file || !fb || packed || zoom < 0x80 || fw <= 0 || fh <= 0 || fw > 4096 || fh > 4096) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > fw) x1 = fw;
    if (y1 > fh) y1 = fh;
    int16_t code = (int16_t)WD_HOST_READ16(desc + 0x3E);
    int mirror = code < 0 && WD_HOST_READ32(file + 0x404);
    uint32_t entry = (uint32_t)(uint16_t)(mirror ? -code : code) >> 8;
    uint32_t rec = file + 0x400 + WD_HOST_READ32(file + 0x408 + 4u * entry);
    int32_t w = (int32_t)WD_HOST_READ32(rec), h = (int32_t)WD_HOST_READ32(rec + 4);
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return;
    uint64_t step = (1ull << 32) / zoom;   /* DOS: size = v * (2^32 / zoom) >> 24 */
#define SCALED(v) ((int32_t)(((int64_t)(v) * (int64_t)step) >> 24))
    int32_t sw = SCALED(w), sh = SCALED(h);
    int32_t hx = SCALED((int32_t)WD_HOST_READ32(rec + 8)), hy = SCALED((int32_t)WD_HOST_READ32(rec + 12));
#undef SCALED
    int32_t x = (int16_t)WD_HOST_READ16(desc + 0x2C), y = (int16_t)WD_HOST_READ16(desc + 0x2E);
    int32_t first = mirror ? x + hx : x - hx, dir = mirror ? -1 : 1, top = y - hy;
    int half = (WD_HOST_READ32(desc + 0x34) & 0x2000) != 0;
    const uint8_t* src = (const uint8_t*)wd_host_range(rec + 16, (size_t)w * (size_t)h, 0);
    const uint32_t* pal = (const uint32_t*)wd_host_range(file, 0x400, 0);
    for (int32_t dv = 0; dv < sh; dv++) {
        int32_t py = top + dv, sv = (int32_t)(((int64_t)dv * zoom) >> 8);
        if (sv >= h) break;
        if (py < y0 || py >= y1) continue;
        uint16_t* row = (uint16_t*)wd_host_range(fb + (uint32_t)py * (uint32_t)fw * 2u, (size_t)fw * 2u, 1);
        for (int32_t du = 0; du < sw; du++) {
            int32_t px = first + dir * du, su = (int32_t)(((int64_t)du * zoom) >> 8);
            if (su >= w) break;
            uint8_t index = src[sv * w + su];
            if (!index || px < x0 || px >= x1) continue;
            uint32_t c = pal[index];
            uint16_t rgb = (uint16_t)(((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu));
            if (half) rgb = (uint16_t)((((uint32_t)rgb & 0xF7DFu) + ((uint32_t)row[px] & 0xF7DFu)) >> 1 & 0xF7DFu);
            row[px] = rgb;
        }
    }
}

/* The guest is the retail program these addresses belong to. */
static int retail_nodes_present(void) {
    const char* root = (const char*)wd_host_range(ROOT_VA, NODE_SIZE, 0);
    const char* exit_node = (const char*)wd_host_range(EXIT_VA, NODE_SIZE, 0);
    return !strcmp(root, "Dreams Editor") && WD_HOST_READ32(ROOT_VA + 0x18) == EXIT_VA &&
           !strcmp(exit_node, "Exit To DOS") && WD_HOST_READ32(EXIT_VA + 0x2C) == EXIT_VALUE;
}

void editor_menu_install(void) {
    char path[1024];
    const char* base = SDL_GetBasePath();
    snprintf(path, sizeof path, "%s%s", base ? base : "", RESOURCE);
    size_t size = 0;
    char* text = (char*)SDL_LoadFile(path, &size);
    if (!text) {
        fprintf(stderr, "[editor] no %s; the editor keeps the retail menu\n", path);
        return;
    }
    if (!retail_nodes_present()) {
        fprintf(stderr, "[editor] the retail editor root (0x4a47c4) is not where expected; keeping its menu\n");
        SDL_free(text);
        return;
    }
    int n = parse_resource(text, path);
    SDL_free(text);
    if (!n) return;

    int owned = 0, cells = 0;
    for (int i = 1; i < n; i++) {
        owned += g_nodes[i].kind != K_RETAIL;
        cells += g_nodes[i].kind == K_CELL;
    }
    uint32_t block = shim_alloc((uint32_t)owned * NODE_SIZE, 16);
    uint32_t cell = cells ? shim_alloc((uint32_t)cells * 4u, 16) : 0;
    static uint32_t va[MAX_NODES];
    va[0] = ROOT_VA;
    for (int i = 1, k = 0; i < n; i++)
        va[i] = g_nodes[i].kind == K_RETAIL ? g_nodes[i].address : block + (uint32_t)k++ * NODE_SIZE;
    for (int i = 1; i < n; i++) {
        const Node* node = &g_nodes[i];
        if (node->kind == K_RETAIL) continue;
        uint32_t value = 0;
        if (node->kind == K_VALUE) value = node->address;
        if (node->kind == K_CELL) {
            value = cell;
            WD_HOST_WRITE32(cell) = WD_HOST_READ32(node->address);
            cell += 4;
        }
        memcpy(wd_host_range(va[i], LABEL_SIZE, 1), node->label, LABEL_SIZE);
        for (int c = 0; c < node->nchild; c++) WD_HOST_WRITE32(va[i] + 0x18 + 4u * (uint32_t)c) = va[node->child[c]];
        WD_HOST_WRITE32(va[i] + 0x2C) = value;
        WD_HOST_WRITE32(va[i] + 0x30) = (uint32_t)node->min;
        WD_HOST_WRITE32(va[i] + 0x34) = (uint32_t)node->max;
        WD_HOST_WRITE32(va[i] + 0x38) = node->mask;
        WD_HOST_WRITE32(va[i] + 0x3C) = node->flags;
    }
    for (int c = 0; c < NCHILD; c++)
        WD_HOST_WRITE32(ROOT_VA + 0x18 + 4u * (uint32_t)c) = c < g_nodes[0].nchild ? va[g_nodes[0].child[c]] : 0;
    g_set3_fallback = shim_alloc(NODE_SIZE, 16);
    july_fonts_install();
    if (!wd_install_replacement(SPRITE_BLIT, sprite_blit))
        fprintf(stderr, "[editor] WARNING: cannot replace the sprite blit (0x402406); sliders show no track\n");
    fprintf(stderr, "[editor] menu: %d nodes, %d host cells, from %s\n", n, cells, path);
}

/* A font file the retail loader can take (SPR_LoadSet dies on a short one):
 * a 512-byte palette, then the bitmaps, 256 descriptors and a count of 256. */
static int font_file_ok(const char* path) {
    size_t size = 0;
    uint8_t* data = (uint8_t*)SDL_LoadFile(path, &size);
    int ok = data && size >= 0x200 + 0x1C04 && data[size - 4] == 0 && data[size - 3] == 1 &&
             data[size - 2] == 0 && data[size - 1] == 0;
    SDL_free(data);
    return ok;
}

/* The July fonts into the data tree (WD_TREE)'s DATA\FONT, from resources/fonts
 * beside the executable, where missing. Sets g_july_fonts to 1 when all four
 * are there. */
static void july_fonts_install(void) {
    const char* tree = host_env("WD_TREE");
    const char* base = SDL_GetBasePath();
    if (!tree || !base) {
        fprintf(stderr, "[editor] no data tree; the editor prints in the retail fonts\n");
        return;
    }
    char dir[1024], src[1024], dst[1100];
    snprintf(dir, sizeof dir, "%s/DATA/FONT", tree);
    for (int i = 0; i < NFONTS; i++) {
        snprintf(dst, sizeof dst, "%s/%s", dir, k_fonts[i]);
        if (font_file_ok(dst)) continue;
        snprintf(src, sizeof src, "%sresources/fonts/%s", base, k_fonts[i]);
        if (!font_file_ok(src) || !SDL_CreateDirectory(dir) || !SDL_CopyFile(src, dst) || !font_file_ok(dst)) {
            fprintf(stderr, "[editor] no July font %s (%s); the editor prints in the retail fonts\n", k_fonts[i], src);
            return;
        }
        fprintf(stderr, "[editor] %s -> %s\n", src, dst);
    }
    g_july_fonts = 1;
}

/* Slots 4-7 <- the July fonts, through retail's own loader, at the first
 * editor frame (the guest's heap and files are up). */
static void july_fonts_load(void) {
    g_july_fonts = 0;
    for (int i = 0; i < NFONTS; i++) {
        char name[32];
        snprintf(name, sizeof name, "data\\font\\%s", k_fonts[i]);
        guest_call_regs(TEXT_LOAD_FONT, shim_strdup(name), (uint32_t)(JULY_SLOT + i), 1, 0);
        if (!WD_HOST_READ32(FONT_SLOTS + (uint32_t)(JULY_SLOT + i) * FONT_SLOT_SIZE)) {
            fprintf(stderr, "[editor] %s did not load; the editor prints in the retail fonts\n", name);
            return;
        }
    }
    g_july_fonts = 2;
    fprintf(stderr, "[editor] July fonts loaded into slots %d-%d\n", JULY_SLOT, JULY_SLOT + NFONTS - 1);
}

static uint8_t g_font_saved[NFONTS * FONT_SLOT_SIZE];
static int g_font_swapped;   /* how many slots, from 0, editor_frame_end puts back */

/* Around the inserted editor call. Sprite set 3 and the July fonts are loaded
 * at the first editor frame (0x62bdb8 is .bss, which the Watcom start-up
 * clears after editor_menu_install). Until editor_frame_end, slots 0-3 hold
 * the July fonts, or slot 1 holds HI320 without them. */
void editor_frame_begin(void) {
    editor_bank_frame_begin();
    editor_pickers_frame_begin();
    if (g_set3_fallback && !WD_HOST_READ32(SLIDER_SET)) {
        if (!g_set3_tried) {
            g_set3_tried = 1;
            uint32_t ok = guest_call_regs(LOAD_FILE_SPR, SET3_DESCRIPTOR, 0, 0, 0);
            fprintf(stderr, "[editor] sprite set 3 (alphabe2.spr): %s\n",
                    ok && WD_HOST_READ32(SLIDER_SET) ? "loaded" : "not loaded; sliders show no track");
        }
        if (!WD_HOST_READ32(SLIDER_SET)) WD_HOST_WRITE32(SLIDER_SET) = g_set3_fallback;
    }
    if (!WD_HOST_READ32(FONT_SLOTS + 3u * FONT_SLOT_SIZE)) return;   /* retail's fonts not loaded yet */
    if (g_july_fonts == 1) july_fonts_load();
    uint8_t* slots = (uint8_t*)wd_host_range(FONT_SLOTS, (JULY_SLOT + NFONTS) * FONT_SLOT_SIZE, 1);
    if (g_july_fonts == 2) {
        memcpy(g_font_saved, slots, NFONTS * FONT_SLOT_SIZE);
        memcpy(slots, slots + JULY_SLOT * FONT_SLOT_SIZE, NFONTS * FONT_SLOT_SIZE);
        g_font_swapped = NFONTS;
    } else {
        memcpy(g_font_saved, slots + FONT_SLOT_SIZE, FONT_SLOT_SIZE);
        memcpy(slots + FONT_SLOT_SIZE, slots + 3 * FONT_SLOT_SIZE, FONT_SLOT_SIZE);
        g_font_swapped = -1;   /* slot 1 only */
    }
}
void editor_frame_end(void) {
    editor_bank_frame_end();   /* its message in the editor's fonts */
    uint8_t* slots = (uint8_t*)wd_host_range(FONT_SLOTS, NFONTS * FONT_SLOT_SIZE, 1);
    if (g_font_swapped == NFONTS) memcpy(slots, g_font_saved, NFONTS * FONT_SLOT_SIZE);
    if (g_font_swapped == -1) memcpy(slots + FONT_SLOT_SIZE, g_font_saved, FONT_SLOT_SIZE);
    g_font_swapped = 0;
}

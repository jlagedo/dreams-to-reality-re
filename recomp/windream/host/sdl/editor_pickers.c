/*
 * WINDREAM recompilation - the Dreams Editor's pickers in Develop
 * (docs/specs/008-editor-restoration/spec.md, phase 3).
 *
 * Every picker page of WORKS.C is one compiled template (title, UP, DOWN,
 * EXIT, eight rows, a held-button hit test on the mouse's y, Space and Esc),
 * and every list it pages is a table of names. July's tables held 13- or
 * 16-byte entries; retail kept the code and shrank each table to one byte
 * (0x661e1e..0x661e27), so entry i+1 overwrites entry i from its second byte
 * and a list of n names runs n+12 bytes past its table, over the resource
 * heap's globals that follow (0x661e28, the heap top 0x661e2c, 0x661e3c,
 * 0x661e44, ...). Retail also never calls the four directory fillers. Here the
 * lists, pages, finds and fillers are replaced (lift/replacements.py
 * HOST_ENTRIES; Develop only, from host_mode_install), with host tables in
 * guest memory (shim_alloc) and nothing written at 0x661e1e or above:
 *
 *   fillers   0x4486bd data\3dc\*.3dc, *.dan, *.dsn   0x4487ed data\hnm\*.ubb, *.Hnm
 *             0x448937 data\anim\*.hnm                0x4489ab data\sym\*.sym
 *             (the retail pattern strings, through the game's own _dos_findfirst_,
 *             _dos_findnext_, _dos_findclose_, so the host file layer resolves
 *             them in the developer folder, sorted by upper-cased name per
 *             pattern; 13-byte entries)
 *             0x4488bf the scene's 64 materials (0x615ad8, name +8, used if the
 *             dword at +0x18 is set; 16-byte entries)
 *   lists     0x44a14a objets (16 at 0x660104, used +0x34; names and meshes)
 *             0x44b3df links (8 at 0x65fd04, used +0x18)
 *             0x44ad7d link adventures (16 at 0x661904, used +0x28)
 *             0x44bb5a boxes (16 at 0x660d04, used +0xec)
 *   finds     0x44a4a9, 0x44b6df, 0x44b07a, 0x44be5d: the set's real slots only
 *             (retail scanned 150), in-use bit not checked (as retail)
 *   pages     0x44a6e4 LOAD MESH, 0x44a990 LOAD SYMBOLE, 0x449577 LOAD HNM,
 *             0x44902b LOAD Anim, 0x4492d1 LOAD Map, 0x44991e LOAD Project
 *             (phase 4's bank list, rebuilt every frame: Load, Delete and the
 *             link-target page 0x44b9a7), 0x44a1d3 LOAD OBJET, 0x44b452 LOAD
 *             LINK, 0x44aded LOAD LINKADVENTURE, 0x44bbd0 LOAD BOX
 *
 * Kept from retail: each page's layout, fonts and draw order, the hit tests
 * on the raw mouse y 0x4a3150 (which WorksEdit_ raises by 6 around the
 * editor), the held-button test, Sleep(50) per scroll step, each page's step
 * and selection bound, the start clamp (-1 for an empty list), Space and Esc
 * on the word 0x626fd8 (zeroed after either), the retail Nb / selection /
 * start globals (0x661d14..0x661d98) as the page's state, the semaphore each
 * page clears, and confirm as a strcpy into the EAX target that leaves the
 * rest of the field alone.
 *
 * Host-only rules (spec 008 phase 3, decided 2026-10-04):
 *   - A file list is refilled when its page opens (it was not drawn in the
 *     previous editor frame); the map list is rebuilt every frame, as retail.
 *   - Confirm with no valid row (none selected, or past the end of the list)
 *     closes the page without writing; retail copied whatever lay there.
 *   - A file whose name is not 8.3 is skipped, logged once.
 *   - The objet, link, link-adventure and box names are 12-byte fields that
 *     the shipped data fills without a terminator ("LINKADVENT10" runs into
 *     +0x0c); their lists keep 12 characters, and their finds compare the
 *     12-byte field (strncmp), where retail's strcmp read past it.
 */
#include <stdio.h>
#include <string.h>
#define RECOMP_GENERATED_CODE   /* the guest registers: the handlers' arguments and returns */
#include "host.h"
#include "render_boundary.h"

#define TEXT_DRAW    0x0044D5C7u  /* TEXT_DrawString(EAX s, EDX x, EBX y, ECX 0x100, [esp+4] font), ret 4 */
#define DELAY        0x00478388u  /* Watcom delay(EAX ms): the pages' Sleep(50) per scroll step */
#define FIND_FIRST   0x004550B0u  /* _dos_findfirst_(EAX pattern, EDX attr, EBX find_t): 0 = found */
#define FIND_NEXT    0x00455124u  /* _dos_findnext_(EAX find_t): 0 = found */
#define FIND_CLOSE   0x0045516Eu  /* _dos_findclose_(EAX find_t) */
#define FIND_T_SIZE  0x130u       /* the fillers' frame buffer; name at +0x1e, 256 bytes */
#define FIND_T_NAME  0x1Eu
#define MOUSE_Y      0x004A3150u
#define MOUSE_BTN    0x004A315Cu  /* bit 0: left button held */
#define KEY_WORD     0x00626FD8u  /* the frame's key code (dev_keys.c puts the DOS character here) */
#define STR_UP       0x004C5E5Au
#define STR_DOWN     0x004C5E5Du
#define STR_EXIT     0x004C5E62u
#define MATERIALS    0x00615AD8u  /* 64 x 0x420; name at +8 (16 bytes), used if the dword at +0x18 is set */
#define NAME_FIELD   12           /* objet, link, link-adventure and box names */

enum { L_3DC, L_HNM, L_MAP, L_ANIM, L_SYM, L_PROJECT, L_OBJET, L_LINK, L_LADV, L_BOX, NLISTS };

typedef struct {
    const char* what;
    int stride, cap;       /* entry size and July's capacity (the table has cap + 1 entries) */
    uint32_t nb;           /* the retail count global */
    uint32_t table, second;   /* guest VAs (second: the objet meshes, the project meshes) */
} List;

static List g_lists[NLISTS] = {
    [L_3DC] = {"mesh", 13, 512, 0x00661D6Cu, 0, 0},
    [L_HNM] = {"hnm", 13, 256, 0x00661D98u, 0, 0},
    [L_MAP] = {"map", 16, 128, 0x00661D7Cu, 0, 0},
    [L_ANIM] = {"anim", 13, 128, 0x00661D64u, 0, 0},
    [L_SYM] = {"symbol", 13, 256, 0x00661D60u, 0, 0},
    [L_PROJECT] = {"project", 16, 150, 0x00661D5Cu, 0, 0},
    [L_OBJET] = {"objet", 16, 16, 0x00661D70u, 0, 0},
    [L_LINK] = {"link", 16, 8, 0x00661D34u, 0, 0},
    [L_LADV] = {"linkadventure", 16, 16, 0x00661D30u, 0, 0},
    [L_BOX] = {"box", 16, 16, 0x00661D18u, 0, 0},
};

typedef struct {
    uint32_t va, title;
    int list;
    uint32_t sel, start;
    int step, bound;       /* bound -1: the list's count (one past the end accepted, as retail) */
    uint32_t sema;
    int current;           /* draws the target field at (150,30): the asset pages */
    uint32_t sel_font;     /* the selected entry's font: 1, the box page 0 */
    uint32_t last_frame;   /* the editor frame it was last drawn in */
} Page;

static Page g_pages[] = {
    {0x0044A6E4u, 0x004C5ED9u, L_3DC, 0x00661D54u, 0x00661D50u, 8, -1, 0x004A4718u, 1, 1, 0},
    {0x0044A990u, 0x004C5EE3u, L_SYM, 0x00661D74u, 0x00661D3Cu, 1, -1, 0x004A4714u, 1, 1, 0},
    {0x00449577u, 0x004C5E70u, L_HNM, 0x00661D40u, 0x00661D68u, 8, 0xFF, 0x004A471Cu, 1, 1, 0},
    {0x0044902Bu, 0x004C5E50u, L_ANIM, 0x00661D58u, 0x00661D80u, 1, 0x3F, 0x004A474Cu, 1, 1, 0},
    {0x004492D1u, 0x004C5E67u, L_MAP, 0x00661D48u, 0x00661D84u, 1, 0x3F, 0x004A473Cu, 1, 1, 0},
    {0x0044991Eu, 0x004C5E79u, L_PROJECT, 0x00661D78u, 0x00661D4Cu, 8, 0x95, 0x004A46BCu, 0, 1, 0},
    {0x0044A1D3u, 0x004C5EACu, L_OBJET, 0x00661D1Cu, 0x00661D44u, 1, 0x0F, 0x004A46CCu, 0, 1, 0},
    {0x0044B452u, 0x004C5F35u, L_LINK, 0x00661D28u, 0x00661D20u, 1, 0x07, 0x004A46F0u, 0, 1, 0},
    {0x0044ADEDu, 0x004C5EF0u, L_LADV, 0x00661D14u, 0x00661D2Cu, 1, 0x0F, 0x004A46DCu, 0, 1, 0},
    {0x0044BBD0u, 0x004C5F6Au, L_BOX, 0x00661D38u, 0x00661D24u, 1, 0x0F, 0x004A4700u, 0, 0, 0},
};
#define NPAGES ((int)(sizeof g_pages / sizeof g_pages[0]))

typedef struct { uint32_t va; int list; uint32_t patterns[3]; } Filler;
static const Filler g_fillers[] = {
    {0x004486BDu, L_3DC, {0x004C5DE0u, 0x004C5DEFu, 0x004C5DFEu}},   /* data\3dc\*.3dc, *.dan, *.dsn */
    {0x004487EDu, L_HNM, {0x004C5E0Du, 0x004C5E1Cu, 0}},              /* data\hnm\*.ubb, data\hnm\*.Hnm */
    {0x00448937u, L_ANIM, {0x004C5E2Bu, 0, 0}},                       /* data\anim\*.hnm */
    {0x004489ABu, L_SYM, {0x004C5E3Bu, 0, 0}},                        /* data\sym\*.sym */
};
#define NFILLERS ((int)(sizeof g_fillers / sizeof g_fillers[0]))

typedef struct { uint32_t list_va, find_va; int list; uint32_t base, stride; int slots; uint32_t used; int mesh; } RecordSet;
static const RecordSet g_sets[] = {
    {0x0044A14Au, 0x0044A4A9u, L_OBJET, 0x00660104u, 0xC0u, 16, 0x34u, 0x0C},
    {0x0044B3DFu, 0x0044B6DFu, L_LINK, 0x0065FD04u, 0x80u, 8, 0x18u, -1},
    {0x0044AD7Du, 0x0044B07Au, L_LADV, 0x00661904u, 0x40u, 16, 0x28u, -1},
    {0x0044BB5Au, 0x0044BE5Du, L_BOX, 0x00660D04u, 0x100u, 16, 0xECu, -1},
};
#define NSETS ((int)(sizeof g_sets / sizeof g_sets[0]))

static uint32_t g_frame;      /* editor frames seen (editor_pickers_frame_begin) */
static uint32_t g_find_t;     /* the fillers' find_t */
static char g_skipped[64][32];   /* not-8.3 names already logged */
static int g_skipped_n, g_skipped_more;

void editor_pickers_frame_begin(void) { g_frame++; }

static void draw_text(uint32_t s, uint32_t x, uint32_t y, uint32_t font) {
    wd_host_range(g_esp - 4, 4, 1);
    PUSH32(g_esp, font);
    guest_call_regs(TEXT_DRAW, s, x, y, 0x100);
    g_esp += 4;
}

static uint32_t entry(const List* l, uint32_t table, int i) { return table + (uint32_t)i * (uint32_t)l->stride; }
/* How many entries the list's table holds: July's capacity plus one spare zeroed
 * entry (a bound one past the end), the project list exactly its 150 rows. */
static int entries(int which) { return which == L_PROJECT ? g_lists[which].cap : g_lists[which].cap + 1; }

/* ---- fillers ---- */

static int is_8_3(const char* name) {
    const char* dot = strchr(name, '.');
    size_t n = strlen(name);
    if (!n || n > 12) return 0;
    if (!dot) return n <= 8;
    if (strchr(dot + 1, '.')) return 0;
    return dot > name && (size_t)(dot - name) <= 8 && strlen(dot + 1) <= 3;
}

static void skip_once(const char* name) {
    for (int i = 0; i < g_skipped_n; i++)
        if (!strncmp(g_skipped[i], name, sizeof g_skipped[0] - 1)) return;
    if (g_skipped_n < (int)(sizeof g_skipped / sizeof g_skipped[0])) {
        snprintf(g_skipped[g_skipped_n++], sizeof g_skipped[0], "%s", name);
        fprintf(stderr, "[pickers] \"%s\" skipped: not an 8.3 name\n", name);
    } else if (!g_skipped_more) {
        g_skipped_more = 1;
        fprintf(stderr, "[pickers] more names that are not 8.3 skipped; not logged\n");
    }
}

static void fill_files(const Filler* f) {
    List* l = &g_lists[f->list];
    int n = 0, per[3] = {0, 0, 0}, full = 0;
    memset(wd_host_range(l->table, (uint32_t)(l->cap + 1) * (uint32_t)l->stride, 1), 0, (size_t)(l->cap + 1) * (size_t)l->stride);
    for (int p = 0; p < 3 && f->patterns[p]; p++) {
        uint32_t r = guest_call_regs(FIND_FIRST, f->patterns[p], 0, g_find_t, 0);
        if (r) continue;
        while (!r) {
            char name[256];
            guest_str(g_find_t + FIND_T_NAME, name, sizeof name);
            if (!is_8_3(name)) skip_once(name);
            else if (n == l->cap) full = 1;
            else { guest_strcpy_out(entry(l, l->table, n++), (uint32_t)l->stride, name); per[p]++; }
            r = guest_call_regs(FIND_NEXT, g_find_t, 0, 0, 0);
        }
        guest_call_regs(FIND_CLOSE, g_find_t, 0, 0, 0);
    }
    WD_HOST_WRITE32(l->nb) = (uint32_t)n;
    char line[160];
    int at = snprintf(line, sizeof line, "[pickers] %s list: %d files (", l->what, n);
    for (int p = 0; p < 3 && f->patterns[p]; p++) {
        char pattern[32];
        guest_str(f->patterns[p], pattern, sizeof pattern);
        at += snprintf(line + at, sizeof line - (size_t)at, "%s%s %d", p ? ", " : "", pattern, per[p]);
    }
    fprintf(stderr, "%s)%s\n", line, full ? "; full, the rest left out" : "");
}

static void fill_map(void) {
    List* l = &g_lists[L_MAP];
    int n = 0;
    memset(wd_host_range(l->table, (uint32_t)(l->cap + 1) * (uint32_t)l->stride, 1), 0, (size_t)(l->cap + 1) * (size_t)l->stride);
    for (int i = 0; i < 64; i++) {
        uint32_t m = MATERIALS + (uint32_t)i * 0x420u;
        if (!WD_HOST_READ32(m + 0x18)) continue;
        char name[16];
        guest_str(m + 8, name, sizeof name);
        guest_strcpy_out(entry(l, l->table, n++), (uint32_t)l->stride, name);
    }
    WD_HOST_WRITE32(l->nb) = (uint32_t)n;
}

static void filler_handler(uint32_t va) {
    for (int i = 0; i < NFILLERS; i++)
        if (g_fillers[i].va == va) fill_files(&g_fillers[i]);
}
static void fill_3dc(void) { filler_handler(0x004486BDu); g_esp += 4; }
static void fill_hnm(void) { filler_handler(0x004487EDu); g_esp += 4; }
static void fill_anim(void) { filler_handler(0x00448937u); g_esp += 4; }
static void fill_sym(void) { filler_handler(0x004489ABu); g_esp += 4; }
static void fill_map_handler(void) { fill_map(); g_esp += 4; }

/* ---- the record sets: lists and finds ---- */

static void list_set(const RecordSet* s) {
    List* l = &g_lists[s->list];
    int n = 0;
    memset(wd_host_range(l->table, (uint32_t)(l->cap + 1) * (uint32_t)l->stride, 1), 0, (size_t)(l->cap + 1) * (size_t)l->stride);
    if (l->second)
        memset(wd_host_range(l->second, (uint32_t)(l->cap + 1) * (uint32_t)l->stride, 1), 0, (size_t)(l->cap + 1) * (size_t)l->stride);
    for (int i = 0; i < s->slots; i++) {
        uint32_t r = s->base + (uint32_t)i * s->stride;
        if (!(WD_HOST_READ8(r + s->used) & 1)) continue;
        char name[NAME_FIELD + 1];
        guest_str(r, name, sizeof name);
        guest_strcpy_out(entry(l, l->table, n), (uint32_t)l->stride, name);
        if (s->mesh >= 0) {
            char mesh[16];
            guest_str(r + (uint32_t)s->mesh, mesh, sizeof mesh);
            guest_strcpy_out(entry(l, l->second, n), (uint32_t)l->stride, mesh);
        }
        n++;
    }
    WD_HOST_WRITE32(l->nb) = (uint32_t)n;
    g_eax = (uint32_t)(s->slots - 1);   /* the loop counter retail left in EAX (callers ignore it) */
}

static uint32_t find_set(const RecordSet* s, uint32_t name_va) {
    char want[NAME_FIELD + 1];
    guest_str(name_va, want, sizeof want);
    for (int i = 0; i < s->slots; i++) {
        uint32_t r = s->base + (uint32_t)i * s->stride;
        char have[NAME_FIELD + 1];
        guest_str(r, have, sizeof have);
        if (!strncmp(have, want, NAME_FIELD)) return r;
    }
    return 0;
}

static void list_objet(void) { list_set(&g_sets[0]); g_esp += 4; }
static void list_link(void) { list_set(&g_sets[1]); g_esp += 4; }
static void list_ladv(void) { list_set(&g_sets[2]); g_esp += 4; }
static void list_box(void) { list_set(&g_sets[3]); g_esp += 4; }
static void find_objet(void) { g_eax = find_set(&g_sets[0], g_eax); g_esp += 4; }
static void find_link(void) { g_eax = find_set(&g_sets[1], g_eax); g_esp += 4; }
static void find_ladv(void) { g_eax = find_set(&g_sets[2], g_eax); g_esp += 4; }
static void find_box(void) { g_eax = find_set(&g_sets[3], g_eax); g_esp += 4; }

/* ---- the pages ---- */

static void confirm(const Page* p, const List* l, uint32_t table, uint32_t target) {
    int sel = (int)WD_HOST_READ32(p->sel), count = (int)WD_HOST_READ32(l->nb);
    if (table && sel >= 0 && sel < count && sel < entries(p->list)) {
        char name[32];
        guest_str(entry(l, table, sel), name, sizeof name);
        guest_strcpy_out(target, (uint32_t)sizeof name, name);   /* a strcpy: the rest of the field stays */
    }
    WD_HOST_WRITE32(p->sema) = 0;
}

static void page_run(Page* p) {
    uint32_t target = g_eax;
    List* l = &g_lists[p->list];
    int opened = p->last_frame + 1 != g_frame && p->last_frame != g_frame;
    p->last_frame = g_frame;
    if (p->list == L_PROJECT) {
        editor_bank_list();
        l->table = editor_bank_list_names();
        l->second = l->table ? l->table + 150u * 16u : 0;
    } else if (opened) {
        for (int i = 0; i < NFILLERS; i++)
            if (g_fillers[i].list == p->list) fill_files(&g_fillers[i]);
    }
    uint32_t table = l->table;
    int count = (int)WD_HOST_READ32(l->nb);

    draw_text(p->title, 0x14, 0x14, 0);
    draw_text(STR_UP, 0x14, 0x28, 0);
    draw_text(STR_DOWN, 0x14, 0x82, 0);
    draw_text(STR_EXIT, 0x14, 0x96, 0);
    int sel = (int)WD_HOST_READ32(p->sel);
    if (sel != -1 && table && sel >= 0 && sel < entries(p->list)) {
        draw_text(entry(l, table, sel), 0x96, 0x14, p->sel_font);
        if (l->second) draw_text(entry(l, l->second, sel), 300, 0x14, 1);
    }
    if (p->current) draw_text(target, 0x96, 0x1E, 1);
    int start = (int)WD_HOST_READ32(p->start);
    if (start < 0) start = 0;
    if (count <= start) start = count - 1;
    WD_HOST_WRITE32(p->start) = (uint32_t)start;
    for (int i = start; table && i < count && i < start + 8 && i < entries(p->list); i++) {
        if (i < 0) continue;
        draw_text(entry(l, table, i), 0x50, (uint32_t)((i - start) * 10 + 0x32), 1);
        if (l->second) draw_text(entry(l, l->second, i), 0xA0, (uint32_t)((i - start) * 10 + 0x32), 1);
    }

    if (WD_HOST_READ8(MOUSE_BTN) & 1) {
        int y = (int)WD_HOST_READ32(MOUSE_Y);
        int bound = p->bound < 0 ? count : p->bound;
        if (0x32 < y && y < 0x82) {
            sel = (y - 0x28) / 10 + (int)WD_HOST_READ32(p->start) - 1;
            if (sel < 0 || bound < sel) sel = -1;
            WD_HOST_WRITE32(p->sel) = (uint32_t)sel;
        }
        if (0x28 < y && y < 0x32) {
            guest_call_regs(DELAY, 0x32, 0, 0, 0);
            WD_HOST_WRITE32(p->start) = WD_HOST_READ32(p->start) - (uint32_t)p->step;
        }
        if (0x82 < y && y < 0x8C) {
            guest_call_regs(DELAY, 0x32, 0, 0, 0);
            WD_HOST_WRITE32(p->start) = WD_HOST_READ32(p->start) + (uint32_t)p->step;
        }
        if (y < 0x1F) confirm(p, l, table, target);
        if (0x95 < y && y < 0xA1) WD_HOST_WRITE32(p->sema) = 0;
    }
    uint16_t key = WD_HOST_READ16(KEY_WORD);
    if (key == 0x1B) {
        WD_HOST_WRITE32(p->sema) = 0;
        WD_HOST_WRITE16(KEY_WORD) = 0;
    } else if (key == 0x20) {
        confirm(p, l, table, target);
        WD_HOST_WRITE16(KEY_WORD) = 0;
    }
}

static void page_by_va(uint32_t va) {
    for (int i = 0; i < NPAGES; i++)
        if (g_pages[i].va == va) { page_run(&g_pages[i]); break; }
    g_esp += 4;
}
static void page_mesh(void) { page_by_va(0x0044A6E4u); }
static void page_sym(void) { page_by_va(0x0044A990u); }
static void page_hnm(void) { page_by_va(0x00449577u); }
static void page_anim(void) { page_by_va(0x0044902Bu); }
static void page_map(void) { page_by_va(0x004492D1u); }
static void page_project(void) { page_by_va(0x0044991Eu); }
static void page_objet(void) { page_by_va(0x0044A1D3u); }
static void page_link(void) { page_by_va(0x0044B452u); }
static void page_ladv(void) { page_by_va(0x0044ADEDu); }
static void page_box(void) { page_by_va(0x0044BBD0u); }

uint32_t editor_pickers_table(int which) { return which >= 0 && which < NLISTS ? g_lists[which].table : 0; }

void editor_pickers_install(void) {
    static const struct { uint32_t va; wd_guest_replacement fn; } k[] = {
        {0x004486BDu, fill_3dc}, {0x004487EDu, fill_hnm}, {0x004488BFu, fill_map_handler},
        {0x00448937u, fill_anim}, {0x004489ABu, fill_sym},
        {0x0044A6E4u, page_mesh}, {0x0044A990u, page_sym}, {0x00449577u, page_hnm},
        {0x0044902Bu, page_anim}, {0x004492D1u, page_map}, {0x0044991Eu, page_project},
        {0x0044A14Au, list_objet}, {0x0044A1D3u, page_objet}, {0x0044A4A9u, find_objet},
        {0x0044B3DFu, list_link}, {0x0044B452u, page_link}, {0x0044B6DFu, find_link},
        {0x0044AD7Du, list_ladv}, {0x0044ADEDu, page_ladv}, {0x0044B07Au, find_ladv},
        {0x0044BB5Au, list_box}, {0x0044BBD0u, page_box}, {0x0044BE5Du, find_box},
    };
    for (int i = 0; i < NLISTS; i++) {
        List* l = &g_lists[i];
        if (i == L_PROJECT) continue;   /* editor_bank.c's list */
        l->table = shim_alloc((uint32_t)(l->cap + 1) * (uint32_t)l->stride, 16);
        if (i == L_OBJET) l->second = shim_alloc((uint32_t)(l->cap + 1) * (uint32_t)l->stride, 16);
    }
    g_find_t = shim_alloc(FIND_T_SIZE, 16);
    int ok = 0, n = (int)(sizeof k / sizeof k[0]);
    for (int i = 0; i < n; i++) {
        if (wd_install_replacement(k[i].va, k[i].fn)) ok++;
        else fprintf(stderr, "[pickers] WARNING: cannot replace 0x%06x\n", k[i].va);
    }
    fprintf(stderr, "[pickers] %d of %d picker functions replaced (fillers, lists, pages, finds); tables:", ok, n);
    for (int i = 0; i < NLISTS; i++)
        if (i != L_PROJECT) fprintf(stderr, " %s 0x%08X/%d", g_lists[i].what, g_lists[i].table, g_lists[i].stride);
    fprintf(stderr, "; objet meshes 0x%08X\n", g_lists[L_OBJET].second);
}

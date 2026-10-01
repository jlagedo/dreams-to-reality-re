/* CPU-only renderer-boundary experiment. Generated game-derived functions
 * are included from DREAMS_OUT, never stored alongside this hand-written file.
 * The arena setup is Windows-only, matching the current recomp toolchain. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"

RECOMP_TLS uint32_t g_eax, g_ebx, g_ecx, g_edx, g_esi, g_edi, g_ebp, g_esp;
RECOMP_TLS double g_st[8];
RECOMP_TLS int g_fp_top;
RECOMP_TLS uint16_t g_fpu_cw = 0x027f;
RECOMP_TLS uint32_t g_flag_k, g_flag_a, g_flag_b, g_flag_cf, g_cur_func;
RECOMP_TLS uint16_t g_seg_cs, g_seg_ds, g_seg_es, g_seg_fs, g_seg_gs, g_seg_ss;
RECOMP_TLS uint32_t g_fs_base, g_gs_base;
RECOMP_TLS uint64_t g_mm[8];
ptrdiff_t g_mem_base;
uint32_t g_icall_trace[ICALL_TRACE_SIZE], g_icall_from[ICALL_TRACE_SIZE];
uint32_t g_icall_trace_idx, g_icall_count;

void sub_0047E634(void);

/* Experimental replacement entry. The lifted transform helper consumes the
 * caller's guest return address, so this wrapper must not pop it again.
 * Submission would consume the saved node and its freshly composed state. */
static void replace_draw_object(void) {
    uint32_t node = g_eax;
    sub_0047E634();
    if (!node) abort();
}
/* Omitted pixel backend and unreachable traversal-error diagnostic. */
void sub_004731B8(void) { g_esp += 4; }
void sub_00460D5F(void) { fputs("unexpected retail printf\n", stderr); exit(3); }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_import(uint32_t va) { (void)va; return NULL; }
/* This isolated transform oracle has no GPU-owned ranges. */
int wd_render_copy(uint32_t pc,uint32_t src,uint32_t dst,uint32_t count,uint32_t width,int direction) {
    (void)pc;(void)src;(void)dst;(void)count;(void)width;(void)direction;return 0;
}
int wd_render_fill(uint32_t pc,uint32_t dst,uint32_t value,uint32_t count,uint32_t width,int direction) {
    (void)pc;(void)dst;(void)value;(void)count;(void)width;(void)direction;return 0;
}

#include "render_smoke_lifted.inc"

static uint32_t word(FILE* f) {
    uint32_t v;
    if (fread(&v, 4, 1, f) != 1) exit(4);
    return v;
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    FILE* f = fopen(argv[1], "rb");
    if (!f || word(f) != 0x314d5352) return 4; /* RSM1 */
    uint32_t entry = word(f);
    uint32_t* regs[] = {&g_eax, &g_ebx, &g_ecx, &g_edx, &g_esi, &g_edi, &g_ebp, &g_esp};
    for (unsigned i = 0; i < 8; ++i) *regs[i] = word(f);
    uint32_t count = word(f), pages = word(f);
    if (count > 10000 || pages > 100000) return 4;
    uint32_t* nodes = malloc(count * 4);
    if (fread(nodes, 4, count, f) != count) return 4;
    g_mem_base = (ptrdiff_t)VirtualAlloc(NULL, (SIZE_T)1 << 32, MEM_RESERVE, PAGE_READWRITE);
    if (!g_mem_base) return 5;
    for (uint32_t i = 0; i < pages; ++i) {
        uint32_t va = word(f);
        void* p = VirtualAlloc((void*)ADDR(va), 4096, MEM_COMMIT, PAGE_READWRITE);
        if (!p || fread(p, 1, 4096, f) != 4096) return 5;
    }
    fclose(f);
    if (!VirtualAlloc((void*)ADDR(0x10000000), 0x10000, MEM_COMMIT, PAGE_READWRITE)) return 5;
    MEM32(g_esp) = 0x1000ff00;
    recomp_func_t fn = recomp_lookup(entry);
    if (!fn) return 6;
    if (!wd_install_replacement(0x0047E498u, replace_draw_object)) return 6;
    fn();
    f = fopen(argv[2], "wb");
    if (!f) return 4;
    for (unsigned i = 0; i < 8; ++i) fwrite(regs[i], 4, 1, f);
    uint32_t cw = g_fpu_cw, top = g_fp_top;
    fwrite(&cw, 4, 1, f);
    fwrite(&top, 4, 1, f);
    for (uint32_t i = 0; i < count; ++i) fwrite((void*)ADDR(nodes[i] + 0x4c), 1, 48, f);
    fclose(f);
    free(nodes);
    VirtualFree((void*)g_mem_base, 0, MEM_RELEASE);
    return 0;
}

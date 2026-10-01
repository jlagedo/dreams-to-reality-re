/* Isolated production-hook ABI oracle. Lifted retail closure lives in DREAMS_OUT. */
#define WIN32_LEAN_AND_MEAN
#define RECOMP_GENERATED_CODE
#include <windows.h>
#include "imports.h"
#include "render_live.h"

RECOMP_TLS uint32_t g_eax,g_ebx,g_ecx,g_edx,g_esi,g_edi,g_ebp,g_esp;
RECOMP_TLS double g_st[8];
RECOMP_TLS int g_fp_top;
RECOMP_TLS uint16_t g_fpu_cw;
RECOMP_TLS uint32_t g_flag_k,g_flag_a,g_flag_b,g_flag_cf,g_cur_func;
RECOMP_TLS uint16_t g_seg_cs,g_seg_ds,g_seg_es,g_seg_fs,g_seg_gs,g_seg_ss;
RECOMP_TLS uint32_t g_fs_base,g_gs_base;
RECOMP_TLS uint64_t g_mm[8];
ptrdiff_t g_mem_base;
uint32_t g_icall_trace[ICALL_TRACE_SIZE],g_icall_from[ICALL_TRACE_SIZE];
uint32_t g_icall_trace_idx,g_icall_count;
static uint32_t submission[7];
static int owned;
static uint32_t events[16][16], event_count;
static void event(uint32_t kind,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    uint32_t *e=events[event_count++];
    if (event_count>16) abort();
    e[0]=kind; e[1]=a; e[2]=b; e[3]=c; e[4]=d;
    e[5]=MEM32(0x4ac8cc); e[6]=MEM32(0x4ac8d0); e[7]=MEM32(g_esp);
    e[8]=g_esp; e[9]=g_ebx; e[10]=g_esi;
    for (uint32_t i=1;i<=5;++i) e[10+i]=MEM32(g_esp+i*4);
}
static void frame_init(void) { event(1,g_eax,g_edx,0,0); g_eax=0x18001000; g_esp+=4; }
static void frame_compose(void) {
    event(2,g_eax,g_edx,0,0);
    g_esp+=4;
}
static void frame_callback(void) { event(4,g_eax,g_edx,0,0); g_esp+=4; }
static void frame_boxes(void) { event(5,g_eax,g_edx,0,0); g_esp+=4; }

#include "render_line_lifted.inc"
recomp_func_t recomp_lookup_reference(uint32_t va) { return recomp_lookup(va); }
int wd_install_replacement(uint32_t va, wd_guest_replacement fn) { abort(); }
int wd_render_requested(void) { return 1; }
int wd_render_surface_owned(uint32_t address) { return owned; }
void wd_render_line(uint32_t d,int x0,int y0,int x1,int y1,uint32_t c) {
    submission[0]++;
    submission[1]=d; submission[2]=x0; submission[3]=y0;
    submission[4]=x1; submission[5]=y1; submission[6]=c;
}
void wd_render_ui(const uint32_t r[8],uint32_t e) { abort(); }
void wd_render_scene(uint32_t r,uint32_t d,int m,uint32_t c,uint32_t f) { event(6,r,d,m,c); }
void wd_render_prepare_callback(uint32_t r,uint32_t d,int m,uint32_t c) { event(3,r,d,m,c); }
void wd_render_collect_scene(uint32_t r) { event(7,r,0,0,0); }
void wd_render_dim_background(uint32_t v) { abort(); }
void wd_render_text_band(int v) { abort(); }
void wd_render_caption_band(void) { abort(); }
void wd_render_forget_surface(uint32_t v) { abort(); }
void wd_render_reset_scene(void) { abort(); }
void wd_render_caption_scope_begin(void) { abort(); }
void wd_render_caption_scope_end(void) { abort(); }
void wd_render_fog_update(void) { abort(); }
void wd_render_hnm5(void) { abort(); }
int wd_render_copy(uint32_t p,uint32_t s,uint32_t d,uint32_t c,uint32_t w,int dir) { abort(); }
int wd_render_fill(uint32_t p,uint32_t d,uint32_t v,uint32_t c,uint32_t w,int dir) { abort(); }

/* Include the exact production function rather than a test copy. */
#include "render_hooks.c"

__declspec(dllexport) int line_hook_init(const void *constant) {
    g_mem_base=(ptrdiff_t)VirtualAlloc(NULL,(SIZE_T)1<<32,MEM_RESERVE,PAGE_READWRITE);
    if (!g_mem_base) return 0;
    if (!VirtualAlloc((void*)ADDR(0x400000),0x300000,MEM_COMMIT,PAGE_READWRITE) ||
        !VirtualAlloc((void*)ADDR(0x10000000),0x10000,MEM_COMMIT,PAGE_READWRITE) ||
        !VirtualAlloc((void*)ADDR(0x18000000),0x20000,MEM_COMMIT,PAGE_READWRITE)) return 0;
    memcpy((void*)ADDR(0x4c6168),constant,8);
    return 1;
}
__declspec(dllexport) void line_hook_close(void) {
    if (g_mem_base) VirtualFree((void*)g_mem_base,0,MEM_RELEASE);
    g_mem_base=0;
}
__declspec(dllexport) void line_hook_run(const uint32_t *input,uint32_t *output,void *pixels) {
    const uint32_t stack=0x1000f000;
    memset(submission,0,sizeof submission);
    memset((void*)ADDR(0x18000040),0,input[0]*input[1]*2);
    MEM32(0x661ebc)=input[0]; MEM32(0x661ec8)=input[1];
    g_eax=0x18000040; g_edx=input[2]; g_ebx=input[3]; g_ecx=input[4];
    g_esi=0x51515151; g_edi=0xd1d1d1d1; g_ebp=0xbebebebe; g_esp=stack;
    g_fp_top=0; g_fpu_cw=0x027f; g_cur_func=0x465c80;
    memset(g_st,0,sizeof g_st);
    MEM32(stack)=0x1000ff00; MEM32(stack+4)=input[5]; MEM32(stack+8)=input[6];
    owned=input[7];
    line();
    memcpy(output,submission,sizeof submission);
    output[7]=g_esi; output[8]=g_edi; output[9]=g_ebp; output[10]=g_esp;
    output[11]=g_fpu_cw; output[12]=g_fp_top; output[13]=MEM32(stack+4);
    output[14]=MEM32(0x661ebc); output[15]=MEM32(0x661ec8);
    memcpy(pixels,(void*)ADDR(0x18000040),input[0]*input[1]*2);
}

__declspec(dllexport) void frame_hook_run(const uint32_t *input,uint32_t *output,uint32_t *trace) {
    const uint32_t stack=0x1000f000;
    memset(events,0,sizeof events); event_count=0;
    MEM32(0x661ee8)=0x18001000; MEM32(0x661ebc)=input[3];
    MEM32(0x4ac8c8)=input[1]; MEM32(0x4aa704)=input[2] ? 0x18002000 : 0;
    MEM32(0x4ac8cc)=0x473014; MEM32(0x4ac8d0)=0x4731b8;
    g_eax=input[0] ? 0x18003000 : 0x18000040;
    g_edx=input[0] ? 0x18000040 : 0xd0d0d0d0;
    g_ebx=0xb0b0b0b0; g_esi=0x51515151; g_edi=0xd1d1d1d1; g_ebp=0xbebebebe;
    g_esp=stack; g_cur_func=input[0] ? 0x4593a4 : 0x459320;
    MEM32(stack)=0x1000ff00;
    frame(input[0]);
    output[0]=g_eax; output[1]=g_edx; output[2]=g_ebx; output[3]=g_esi;
    output[4]=g_edi; output[5]=g_ebp; output[6]=g_esp;
    output[7]=MEM32(0x4ac8cc); output[8]=MEM32(0x4ac8d0); output[9]=event_count;
    memcpy(trace,events,event_count*sizeof events[0]);
}

/* Production bridge + lifted palette converter, isolated from SDL/GPU. */
#define WIN32_LEAN_AND_MEAN
#define RECOMP_GENERATED_CODE
#include <windows.h>
#include <assert.h>
#include "imports.h"
#include "render_live.h"
#include "render_movie.h"

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
static int owned;
static unsigned allocations, releases, uploads;
static unsigned char* result_pixels;
static uint32_t uploaded[7];
enum { SOURCE=0x18000000, TABLE=0x18100000, DEST=0x18200040, SCRATCH=0x18300040 };
static void sub_00454FEB(void) { g_esp += 8; } // only compiler stack guard is stubbed
#include "render_movie_lifted.inc"
recomp_func_t recomp_lookup_reference(uint32_t address) { return recomp_lookup(address); }
int wd_render_surface_owned(uint32_t address) { assert(address==DEST); return owned; }
uint32_t vm_alloc(uint32_t address,uint32_t bytes,uint32_t type,uint32_t protect) {
    assert(!address && bytes==960000 && type==0x3000 && protect==4);
    ++allocations; return SCRATCH;
}
int vm_free(uint32_t address,uint32_t bytes,uint32_t type) {
    assert(address==SCRATCH && !bytes && type==0x8000); ++releases; return 1;
}
int wd_render_copy(uint32_t ip,uint32_t s,uint32_t d,uint32_t c,uint32_t w,int dir) { return 0; }
int wd_render_fill(uint32_t ip,uint32_t d,uint32_t v,uint32_t c,uint32_t w,int dir) {
    assert(d>=SCRATCH || !owned); return 0;
}
void wd_render_movie_upload(uint32_t source,uint32_t destination,int x,int y,int w,int h,int pitch) {
    assert(destination==DEST && MEM32(0x5e549c)==DEST && source>=SCRATCH);
    ++uploads;
    uploaded[0]=source; uploaded[1]=destination; uploaded[2]=x; uploaded[3]=y;
    uploaded[4]=w; uploaded[5]=h; uploaded[6]=pitch;
    for (int row=0;row<h;++row)
        memcpy(result_pixels+(y+row)*pitch+x*2,PTR(source+row*pitch),(size_t)w*2);
}
#include "render_movie.c"

__declspec(dllexport) int movie_hook_init(void) {
    g_mem_base=(ptrdiff_t)VirtualAlloc(NULL,(SIZE_T)1<<32,MEM_RESERVE,PAGE_READWRITE);
    if (!g_mem_base) return 0;
    return VirtualAlloc(PTR(0x400000),0x300000,MEM_COMMIT,PAGE_READWRITE)!=NULL &&
           VirtualAlloc(PTR(0x10000000),0x10000,MEM_COMMIT,PAGE_READWRITE)!=NULL &&
           VirtualAlloc(PTR(0x18000000),0x400000,MEM_COMMIT,PAGE_READWRITE)!=NULL;
}
__declspec(dllexport) void movie_hook_close(uint32_t* counts) {
    wd_render_movie_shutdown();
    counts[0]=allocations; counts[1]=releases;
    VirtualFree((void*)g_mem_base,0,MEM_RELEASE);g_mem_base=0;
}
__declspec(dllexport) void movie_hook_run(const uint32_t* input,const void* source,const void* table,
                                         uint32_t* output,void* pixels) {
    uint32_t bytes=input[0]*input[1]*2, stack=0x1000f000;
    memcpy(PTR(SOURCE),source,640*480+2);memcpy(PTR(TABLE),table,65536*4);
    memset(PTR(DEST-64),0xa5,64);memset(PTR(DEST+bytes),0x5a,64);
    for(unsigned i=0;i<bytes/2;++i) MEM16(DEST+i*2)=(uint16_t)input[3];
    memset(PTR(SCRATCH-64),0xa5,64);memset(PTR(SCRATCH+960000),0x5a,64);
    owned=input[2];uploads=0;memset(uploaded,0,sizeof uploaded);
    result_pixels=pixels;
    for(unsigned i=0;i<bytes/2;++i) ((uint16_t*)pixels)[i]=(uint16_t)input[3];
    MEM32(0x49d9fc)=input[0];MEM32(0x49da00)=input[1];MEM32(0x5e549c)=DEST;
    MEM32(0x60ce14)=SOURCE;MEM32(0x60ce1c)=TABLE;MEM32(0x4a4b70)=0xdeadbeef;
    g_eax=0;g_edx=0;g_ebx=0xb0b0b0b0;g_ecx=0xc0c0c0c0;
    g_esi=0x51515151;g_edi=0xd1d1d1d1;g_ebp=0xbebebebe;g_esp=stack;
    g_fp_top=0;g_fpu_cw=0x027f;g_cur_func=0x42665a;MEM32(stack)=0x1000ff00;
    wd_render_hnm5();
    output[0]=g_eax;output[1]=g_ebx;output[2]=g_ecx;output[3]=g_edx;
    output[4]=g_esi;output[5]=g_edi;output[6]=g_ebp;output[7]=g_esp;
    output[8]=g_fpu_cw;output[9]=g_fp_top;output[10]=MEM32(0x4a4b70);
    output[11]=MEM32(0x5e549c);output[12]=uploads;
    output[13]=recomp_eflags(g_flag_k,g_flag_a,g_flag_b,g_flag_cf,1);
    memcpy(output+14,uploaded,sizeof uploaded);
    for(unsigned i=0;i<64;++i) {
        assert(MEM8(DEST-64+i)==0xa5 && MEM8(DEST+bytes+i)==0x5a);
        assert(MEM8(SCRATCH-64+i)==0xa5 && MEM8(SCRATCH+960000+i)==0x5a);
    }
    if (!owned) memcpy(pixels,PTR(DEST),bytes);
    else for(unsigned i=0;i<bytes/2;++i) assert(MEM16(DEST+i*2)==(uint16_t)input[3]);
}

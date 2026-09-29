/*
 * WINDREAM recompilation - runtime core.
 *
 * The whole guest address space is one host arena: guest VA v lives at host
 * g_mem_base + v. The arena is reserved up front and committed where the guest
 * has memory (image, stacks, TIBs, VirtualAlloc'd heap), so a wild guest pointer
 * faults instead of reading zeros. The original WINDREAM.EXE is mapped at its
 * real VAs from the file at startup; the lifted C is the code.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "imports.h"
#include "crash_report.h"
#include "recomp_trace.h"

/* ---- register file (per host thread) ---- */
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp;
RECOMP_TLS double   g_st[8];
RECOMP_TLS int      g_fp_top;
RECOMP_TLS uint16_t g_fpu_cw = 0x027F;   /* Win32 process default: 53-bit, round-nearest */
RECOMP_TLS uint32_t g_flag_k, g_flag_a, g_flag_b, g_flag_cf;
RECOMP_TLS uint16_t g_seg_cs, g_seg_ds, g_seg_es, g_seg_fs, g_seg_gs, g_seg_ss;
RECOMP_TLS uint32_t g_fs_base, g_gs_base;
RECOMP_TLS uint64_t g_mm[8];
RECOMP_TLS uint32_t g_cur_func;

ptrdiff_t g_mem_base;
uint32_t g_icall_trace[ICALL_TRACE_SIZE];
uint32_t g_icall_from[ICALL_TRACE_SIZE];
uint32_t g_icall_trace_idx, g_icall_count;


static uint32_t g_image_span;
void wd_scene_probe_init(void);
void wd_render_install(void);
void wd_render_close(void);
const char* g_wd_exe;
uint32_t wd_image_span(void) { return g_image_span; }

/* ---- small guest-memory helpers ---- */
static CRITICAL_SECTION g_alloc_cs;
static uint32_t g_shim_next;   /* shim allocations: top of the heap region, growing down */

uint32_t shim_alloc(uint32_t n, uint32_t align) {
    EnterCriticalSection(&g_alloc_cs);
    if (align < 16) align = 16;
    uint32_t va = (g_shim_next - n) & ~(align - 1u);
    g_shim_next = va;
    LeaveCriticalSection(&g_alloc_cs);
    VirtualAlloc(PTR(va), n ? n : 1, MEM_COMMIT, PAGE_READWRITE);
    memset(PTR(va), 0, n);
    return va;
}
uint32_t shim_strdup(const char* s) {
    uint32_t n = (uint32_t)strlen(s) + 1, va = shim_alloc(n, 16);
    memcpy(PTR(va), s, n);
    return va;
}
void guest_strcpy_out(uint32_t va, uint32_t cap, const char* s) {
    if (!va || !cap) return;
    uint32_t n = (uint32_t)strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(PTR(va), s, n);
    MEM8(va + n) = 0;
}
int guest_str(uint32_t va, char* out, int cap) {
    int i = 0;
    if (!va) { out[0] = 0; return 0; }
    for (; i < cap - 1; i++) { char c = (char)MEM8(va + i); out[i] = c; if (!c) break; }
    out[cap - 1] = 0;
    return i;
}

/* ---- dispatch ---- */
recomp_func_t recomp_lookup(uint32_t va) {
    int lo = 0, hi = (int)recomp_dispatch_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t m = recomp_dispatch_table[mid].address;
        if (m == va) return recomp_dispatch_table[mid].func;
        if (m < va) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_import(uint32_t va) {
    if (va >= WD_COM_BASE && va < WD_COM_BASE + 0x1000u) return wd_com_lookup(va);
    for (uint32_t i = 0; i < wd_import_bridge_count; i++)
        if (wd_import_bridges[i].address == va) return wd_import_bridges[i].func;
    return NULL;
}

/* ---- synthetic COM methods ----
 * A fake COM object lives in guest memory as { vtbl_va, ... }; its vtable holds
 * made-up method VAs from WD_COM_BASE up. `call [vtbl+off]` in lifted code is a
 * RECOMP_ICALL of that VA, which recomp_lookup_import resolves here. */
static recomp_func_t g_com_fn[1024];
static uint32_t g_com_n;
uint32_t wd_com_vtable(const recomp_func_t* fns, int n) {
    EnterCriticalSection(&g_alloc_cs);
    uint32_t base = g_com_n;
    for (int i = 0; i < n; i++) g_com_fn[g_com_n++] = fns[i];
    LeaveCriticalSection(&g_alloc_cs);
    uint32_t vt = shim_alloc((uint32_t)n * 4u, 16);
    for (int i = 0; i < n; i++) MEM32(vt + 4u * (uint32_t)i) = WD_COM_BASE + base + (uint32_t)i;
    return vt;
}
recomp_func_t wd_com_lookup(uint32_t va) {
    uint32_t i = va - WD_COM_BASE;
    return i < g_com_n ? g_com_fn[i] : NULL;
}

/* ---- callbacks: host code calling a lifted stdcall function ----
 * Pushes the arguments and the dummy return address on the calling thread's
 * guest stack and runs the lifted body. Only valid on a thread with guest
 * state, i.e. from inside an import bridge. The caller's registers are put back
 * (the callee would preserve ebx/esi/edi/ebp; ecx/edx are restored too). */
uint32_t guest_call(uint32_t va, int argc, const uint32_t* args) {
    recomp_func_t fn = recomp_lookup(va);
    if (!fn) { fprintf(stderr, "[callback] no lifted function at 0x%08X\n", va); return 0; }
    uint32_t esp = g_esp, ebx = g_ebx, ecx = g_ecx, edx = g_edx, esi = g_esi, edi = g_edi, ebp = g_ebp;
    for (int i = argc - 1; i >= 0; i--) PUSH32(g_esp, args[i]);
    PUSH32(g_esp, RECOMP_RETADDR);
    fn();
    uint32_t r = g_eax;
    g_esp = esp; g_ebx = ebx; g_ecx = ecx; g_edx = edx; g_esi = esi; g_edi = edi; g_ebp = ebp;
    return r;
}

/* The same for a Watcom register call (EAX, EDX, EBX, ECX): every guest
 * register is put back, EAX included. */
uint32_t guest_call_regs(uint32_t va, uint32_t eax, uint32_t edx, uint32_t ebx, uint32_t ecx) {
    recomp_func_t fn = recomp_lookup(va);
    if (!fn) { fprintf(stderr, "[callback] no lifted function at 0x%08X\n", va); return 0; }
    uint32_t a = g_eax, esp = g_esp, ebx0 = g_ebx, ecx0 = g_ecx, edx0 = g_edx, esi = g_esi, edi = g_edi,
             ebp = g_ebp;
    g_eax = eax; g_edx = edx; g_ebx = ebx; g_ecx = ecx;
    PUSH32(g_esp, RECOMP_RETADDR);
    fn();
    uint32_t r = g_eax;
    g_eax = a; g_esp = esp; g_ebx = ebx0; g_ecx = ecx0; g_edx = edx0; g_esi = esi; g_edi = edi; g_ebp = ebp;
    return r;
}

/* ---- image loader ---- */
#pragma pack(push, 1)
typedef struct { char name[8]; uint32_t vsize, vaddr, rsize, roff, a, b; uint16_t c, d; uint32_t chr; } SecHdr;
#pragma pack(pop)

static int load_image(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "FATAL: cannot open %s\n", path); return 0; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(buf); return 0; }
    fclose(f);
    uint8_t* nt = buf + *(uint32_t*)(buf + 0x3C);
    uint16_t nsec = *(uint16_t*)(nt + 6), optsz = *(uint16_t*)(nt + 20);
    uint32_t hdrsz = *(uint32_t*)(nt + 24 + 60);
    SecHdr* sec = (SecHdr*)(nt + 24 + optsz);
    uint32_t end = 0;
    for (int i = 0; i < nsec; i++) {
        uint32_t e = sec[i].vaddr + (sec[i].vsize ? sec[i].vsize : sec[i].rsize);
        if (e > end) end = e;
    }
    g_image_span = (end + 0xFFFu) & ~0xFFFu;
    VirtualAlloc(PTR(WD_IMAGE_BASE), g_image_span, MEM_COMMIT, PAGE_READWRITE);
    memcpy(PTR(WD_IMAGE_BASE), buf, hdrsz < (uint32_t)sz ? hdrsz : (uint32_t)sz);
    for (int i = 0; i < nsec; i++) {
        /* Watcom .bss: PointerToRawData 0 with the memory size in SizeOfRawData. */
        if (!sec[i].roff || !sec[i].rsize) continue;
        uint32_t n = sec[i].vsize && sec[i].vsize < sec[i].rsize ? sec[i].vsize : sec[i].rsize;
        if (sec[i].roff + n > (uint32_t)sz) n = (uint32_t)sz - sec[i].roff;
        memcpy(PTR(WD_IMAGE_BASE + sec[i].vaddr), buf + sec[i].roff, n);
    }
    free(buf);
    fprintf(stderr, "[*] image: %u sections, span 0x%X at guest 0x%08X\n", nsec, g_image_span, WD_IMAGE_BASE);
    return 1;
}

static const char* region(uint32_t va) {
    if (va >= WD_IMAGE_BASE && va < WD_IMAGE_BASE + g_image_span) return "(image)";
    if (va >= WD_STACK_BASE && va < WD_STACK_BASE + WD_STACK_SIZE) return "(main stack)";
    if (va >= WD_HEAP_BASE && va < WD_HEAP_BASE + WD_HEAP_SIZE) return vm_describe(va);
    if (va == RECOMP_RETADDR) return "(dummy return address)";
    return "";
}

static int setup(const char* exe) {
    InitializeCriticalSection(&g_alloc_cs);
    void* arena = VirtualAlloc(NULL, WD_ARENA_SIZE, MEM_RESERVE, PAGE_READWRITE);
    if (!arena) { fprintf(stderr, "FATAL: cannot reserve %u MB arena\n", WD_ARENA_SIZE >> 20); return 0; }
    g_mem_base = (ptrdiff_t)(uintptr_t)arena;
    g_shim_next = WD_HEAP_BASE + WD_HEAP_SIZE;
    fprintf(stderr, "[*] arena: %u MB reserved at host %p\n", WD_ARENA_SIZE >> 20, arena);
    if (!load_image(exe)) return 0;

    /* IAT: each slot holds its own VA, so `call [slot]` and the `jmp [slot]`
     * thunks both dispatch to recomp_lookup_import(slot) -> the bridge. */
    for (uint32_t i = 0; i < wd_import_bridge_count; i++)
        MEM32(wd_import_bridges[i].address) = wd_import_bridges[i].address;

    wd_thread_init_main();
    fprintf(stderr, "[*] %u lifted functions, %u import bridges, esp=%08X\n",
            recomp_dispatch_count, wd_import_bridge_count, g_esp);
    return 1;
}

/* WD_POKE="va=value,...": dword writes into the loaded image before the entry
 * point runs, e.g. the debug flags nothing in the retail code sets (run.py
 * --overlays). Values are strtoul base 0 (0x.. hex, else decimal). */
static int apply_pokes(void) {
    const char* s = getenv("WD_POKE");
    while (s && *s) {
        char* end;
        uint32_t va = (uint32_t)strtoul(s, &end, 0);
        if (*end != '=') break;
        uint32_t v = (uint32_t)strtoul(end + 1, &end, 0);
        if (va < WD_IMAGE_BASE || va + 4u > WD_IMAGE_BASE + g_image_span) {
            fprintf(stderr, "FATAL: WD_POKE 0x%08X is outside the image\n", va);
            return 0;
        }
        MEM32(va) = v;
        fprintf(stderr, "[*] poke [0x%08X] = 0x%X\n", va, v);
        if (*end != ',') { s = end; break; }
        s = end + 1;
    }
    if (s && *s) { fprintf(stderr, "FATAL: WD_POKE: expected va=value at \"%s\"\n", s); return 0; }
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <GDIDREAM.EXE> [--run] [trace options]\n", argv[0]);
        recomp_trace_help();
        return 2;
    }
    int run = 0;
    for (int i = 2; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--run")) run = 1;
    }
    recomp_install_crash_handler();
    recomp_set_region_describer(region);
    if (!setup(argv[1]) || !apply_pokes()) return 1;
    /* The PE entry point (WINDREAM: 0x465538), from the mapped headers. */
    uint32_t entry = WD_IMAGE_BASE + MEM32(WD_IMAGE_BASE + MEM32(WD_IMAGE_BASE + 0x3C) + 0x28);
    recomp_func_t entry_fn = recomp_lookup(entry);
    if (!entry_fn) { fprintf(stderr, "FATAL: entry point 0x%08X is not a lifted function\n", entry); return 1; }
    if (!run) { fprintf(stderr, "[*] ready; pass --run to enter the program at 0x%08X\n", entry); return 0; }

    g_wd_exe = argv[1];
    files_init(argv[1]);
    host_init();
    wd_scene_probe_init();
    wd_render_install();
    fprintf(stderr, "[*] entering the program at 0x%08X\n", entry);
    PUSH32(g_esp, RECOMP_RETADDR);
    entry_fn();
    fprintf(stderr, "[*] entry returned, eax=%08X\n", g_eax);
    recomp_trace_flush();
    wd_render_close();
    return (int)g_eax;
}

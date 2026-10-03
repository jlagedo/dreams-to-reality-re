/*
 * WINDREAM recompilation - import bridge helpers (after pod-recomp).
 *
 * A bridge runs inside the recomp stack model. RECOMP_ICALL has pushed a dummy
 * return address, so from ESP the stack is [ret][arg0][arg1]... Every Win32
 * import WINDREAM uses is stdcall: the bridge pops its own arguments.
 *
 *   ARG(n)     argument n (0-based), a 32-bit slot
 *   PTR(va)    guest VA -> host pointer (arena)
 *   RET(v)     set EAX
 *   STDRET(k)  pop the dummy return address and k argument slots
 */
#ifndef WD_IMPORTS_H
#define WD_IMPORTS_H

#include "recomp_types.h"
#include "render_boundary.h"

#define PTR(va)     ((void*)(uintptr_t)ADDR(va))
/* Host shims bypass the generated instruction probes. Check each actual
 * transfer here; PTR alone intentionally carries no guessed access size. */
static inline void* wd_host_range(uint32_t va, size_t bytes, int write) {
#ifdef WD_RENDER_AUDIT
    if (bytes > UINT32_MAX || (uint64_t)va + bytes > 0x100000000ull) abort();
    WD_AUDIT_MEMORY(g_cur_func, va, (uint32_t)bytes, write);
#endif
    return PTR(va);
}
#define WD_HOST_READ8(va)  (*(const uint8_t*)wd_host_range((va), 1, 0))
#define WD_HOST_READ16(va) (*(const uint16_t*)wd_host_range((va), 2, 0))
#define WD_HOST_READ32(va) (*(const uint32_t*)wd_host_range((va), 4, 0))
#define WD_HOST_WRITE8(va)  (*(uint8_t*)wd_host_range((va), 1, 1))
#define WD_HOST_WRITE16(va) (*(uint16_t*)wd_host_range((va), 2, 1))
#define WD_HOST_WRITE32(va) (*(uint32_t*)wd_host_range((va), 4, 1))
#define ARG(n)      WD_HOST_READ32(g_esp + 4 + (n) * 4)
#define RET(v)      do { g_eax = (uint32_t)(v); } while (0)
#define STDRET(k)   do { g_esp += 4 + (uint32_t)(k) * 4; } while (0)

#define IMPORT_STUB(nm) do { \
    static int _w = 0; \
    if (!_w) { fprintf(stderr, "[import-stub] %s\n", (nm)); _w = 1; } \
} while (0)

/* Guest memory layout (guest VAs; host = arena + VA). */
#define WD_TIB_BASE     0x00010000u   /* main thread TIB (one page)            */
#define WD_STACK_BASE   0x00100000u   /* main thread stack, 2 MB               */
#define WD_STACK_SIZE   0x00200000u
#define WD_IMAGE_BASE   0x00400000u   /* WINDREAM.EXE                          */
#define WD_HEAP_BASE    0x01000000u   /* VirtualAlloc / shim allocations       */
#ifdef __EMSCRIPTEN__
#define WD_HEAP_SIZE    0x18000000u   /* 384 MB: wasm32 memory is one buffer, the guest uses about 32 MB at the menu */
#else
#define WD_HEAP_SIZE    0x30000000u   /* 768 MB                                */
#endif
#define WD_ARENA_SIZE   (WD_HEAP_BASE + WD_HEAP_SIZE)

/* runtime.c */
uint32_t shim_alloc(uint32_t n, uint32_t align);      /* zeroed, never freed */
uint32_t shim_strdup(const char* s);
void     guest_strcpy_out(uint32_t va, uint32_t cap, const char* s);
int      guest_str(uint32_t va, char* out, int cap);  /* copy a guest C string */
uint32_t wd_image_span(void);

/* files.c */
struct Disc;                                          /* recomp/disc/disc.h */
int      files_open_discs(void);                      /* 1 disc mode (WD_DISC1, WD_DISC2), 0 legacy, -1 failed (message printed) */
void*    files_disc_read(int disc, const char* path, size_t* size);   /* a whole file of disc 1 or 2, malloc'd; NULL if absent */
struct Disc* files_active_disc(int* number, void (*on_switch)(void)); /* NULL in legacy mode; on_switch: called after each change */
void     files_init(const char* exe);                 /* exe: the legacy read root's file; unused in disc mode */
void     files_release(int kind, void* host);         /* drop a reference to an HK_FILE or HK_FIND object */

/* kernel.c */
extern RECOMP_TLS uint32_t g_last_error;              /* the guest's GetLastError value */
void     mapping_release(void* host);                 /* a closed HK_MAP object */

/* vm/vm_front.c: the host memory behind the arena, and VirtualAlloc/VirtualFree/
 * VirtualQuery over the guest heap region. The implementation behind it is
 * chosen at build time (vm/vm_impl.h: win32, ledger or shadow). States and
 * flags are the guest's W32_MEM_* values. Everything in the host that needs to
 * know what is at a guest address asks here, never the host OS: the answer is
 * the same on every host, and a VA past the arena is MEM_FREE instead of
 * whatever the host happens to map there. */
void*    vm_reserve(size_t bytes);                    /* address space only; NULL on failure */
void     vm_commit(uint32_t va, uint32_t bytes);      /* zeroed, readable and writable */
uint32_t vm_alloc(uint32_t addr, uint32_t size, uint32_t type, uint32_t prot);
int      vm_free(uint32_t addr, uint32_t size, uint32_t type);
uint32_t vm_query(uint32_t addr, uint32_t mbi_va);    /* writes the guest's 28-byte MBI; returns 28 or 0 */
const char* vm_describe(uint32_t va);                 /* for crash reports */
/* W32_MEM_COMMIT, W32_MEM_RESERVE or W32_MEM_FREE for the page holding va, and
 * (when the pointers are given) the run of pages in that state starting at
 * va's page and going forward, clipped to the arena: base is va's page, as
 * VirtualQuery's BaseAddress is, so base + bytes is where the state changes.
 * A snapshot: another thread may change it afterwards. */
uint32_t vm_state(uint32_t va, uint32_t* run_base, uint32_t* run_bytes);
void     vm_dump(FILE* out);                          /* reservations and page runs, as text */
/* The committed guest memory as a memory image: "WDM2", root (the render
 * root, for the readers in verify/), then runs of { u32 va, u32 bytes, data }.
 * Returns 0 on a write error; runs and bytes may be NULL. */
int      vm_write_image(const char* path, uint32_t root, uint32_t* runs, uint32_t* bytes);

/* threads.c: the main thread's stack and TIB, and guest handles (small
 * numbers; each owns a host object of one kind) */
void     wd_thread_init_main(void);
uint32_t handle_new(void* host, int kind);
void*    handle_get(uint32_t gh, int kind);            /* kind 0: any */
void*    handle_acquire(uint32_t gh, int kind);        /* HK_FILE, HK_FIND: referenced, files_release it */
int      handle_kind(uint32_t gh);                     /* 0: not a handle */
void     handle_close(uint32_t gh);
enum { HK_FILE = 1, HK_FIND = 2, HK_STD = 3, HK_MAP = 6 };

/* runtime.c: calling back into lifted code, and synthetic COM methods */
extern const char* g_wd_exe;                           /* host path of the guest EXE; its bare name when it came from disc 1 */
uint32_t guest_call(uint32_t va, int argc, const uint32_t* args);  /* stdcall into lifted code */
uint32_t guest_call_regs(uint32_t va, uint32_t eax, uint32_t edx, uint32_t ebx, uint32_t ecx);  /* __watcall */
#define WD_COM_BASE 0xCD000000u                        /* synthetic COM method VAs */
uint32_t wd_com_vtable(const recomp_func_t* fns, int n);
recomp_func_t wd_com_lookup(uint32_t va);

/* web/web_glue.c (the browser build only; a no-op elsewhere): tell the page.
 * kind is "boot", "running" or "fatal" (Module.onDreamsStatus(kind, text)). */
#ifdef __EMSCRIPTEN__
void     wd_web_status(const char* kind, const char* text);
void     wd_web_boot_note(const char* rel, int ok);   /* every guest open: the unattended-boot state machine */
int      wd_web_boot_key(int vk);                    /* 1 while the host presses this key itself */
#else
#define  wd_web_status(kind, text) ((void)0)
#define  wd_web_boot_note(rel, ok) ((void)0)
#define  wd_web_boot_key(vk) 0
#endif

/* user.c (host.h has the SDL side) */
void     host_init(void);
uint32_t host_elapsed_ms(void);
extern int g_wd_quiet;                                 /* WD_QUIET: log message boxes, don't show */

/* dsound.c */
/* Play raw 44.1 kHz 16-bit stereo from a host file (UTF-8 path): length bytes
 * from offset, or to the end of the file when length is 0. NULL path stops.
 * track and disc (0: none) are for the log. */
void     mixer_cd_play(const char* path, uint64_t offset, uint64_t length, int track, int disc);
void     mixer_cd_pause(int paused);
int      mixer_cd_playing(void);

/* imports_gen.c (generated by gen_imports.py) */
extern const recomp_dispatch_entry_t wd_import_bridges[];
extern const uint32_t wd_import_bridge_count;
extern const char* const wd_import_bridge_names[];     /* "imp_CreateFileA", same order */

#endif /* WD_IMPORTS_H */

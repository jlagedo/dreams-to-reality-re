/*
 * WINDREAM recompilation - the virtual-memory front door.
 *
 * The vm_* API that the runtime, the bridges and the renderer call, and the
 * guest's VirtualAlloc/VirtualFree/VirtualQuery. Forwards to the
 * implementation chosen at build time (vm_impl.h: vm_impl_*).
 *
 * WD_VM_LOG=<file> writes one line per call with its arguments and result,
 * and the implementation's state (vm_dump) at exit. Two builds driven by the
 * same script (run.py --keys) can then be compared line by line
 * (verify/vm_compare.py). Calls from several guest threads interleave in the
 * order they happened, which the script cannot fix.
 */
#define RECOMP_GENERATED_CODE
#include "vm_impl.h"

static FILE* g_log;

static void log_close(void) {
    if (!g_log) return;
    fprintf(g_log, "--- state at exit\n");
    vm_impl_dump(g_log);
    fclose(g_log);
    g_log = NULL;
}

static void log_open(void) {
    static int tried;
    if (tried) return;
    tried = 1;
    const char* path = getenv("WD_VM_LOG");
    if (!path || !*path) return;
    g_log = fopen(path, "w");
    if (g_log) atexit(log_close);
    else fprintf(stderr, "[vm] cannot write WD_VM_LOG %s\n", path);
}

/* One fprintf per line, so lines from two threads do not split each other. */
#define LOG(...) do { if (g_log) { fprintf(g_log, __VA_ARGS__); fflush(g_log); } } while (0)

void* vm_reserve(size_t bytes) {
    log_open();
    void* p = vm_impl_reserve(bytes);
    LOG("reserve 0x%zX -> %s\n", bytes, p ? "ok" : "FAILED");
    return p;
}

void vm_commit(uint32_t va, uint32_t bytes) {
    log_open();
    vm_impl_commit(va, bytes);
    LOG("commit %08X 0x%X\n", va, bytes);
}

uint32_t vm_alloc(uint32_t addr, uint32_t size, uint32_t type, uint32_t prot) {
    uint32_t r = vm_impl_alloc(addr, size, type, prot);
    LOG("alloc %08X 0x%X type=0x%X prot=0x%X -> %08X\n", addr, size, type, prot, r);
    return r;
}

int vm_free(uint32_t addr, uint32_t size, uint32_t type) {
    int ok = vm_impl_free(addr, size, type);
    LOG("free %08X 0x%X type=0x%X -> %d err=%u\n", addr, size, type, ok, ok ? 0u : g_last_error);
    return ok;
}

uint32_t vm_query(uint32_t addr, uint32_t mbi) {
    uint32_t r = vm_impl_query(addr, mbi);
    if (g_log) {
        if (r)
            LOG("query %08X -> %u base=%08X alloc=%08X aprot=0x%X size=0x%X state=0x%X prot=0x%X type=0x%X\n",
                addr, r, WD_HOST_READ32(mbi + W32_MBI_BASE), WD_HOST_READ32(mbi + W32_MBI_ALLOCBASE),
                WD_HOST_READ32(mbi + W32_MBI_ALLOCPROTECT), WD_HOST_READ32(mbi + W32_MBI_REGIONSIZE),
                WD_HOST_READ32(mbi + W32_MBI_STATE), WD_HOST_READ32(mbi + W32_MBI_PROTECT),
                WD_HOST_READ32(mbi + W32_MBI_TYPE));
        else
            LOG("query %08X -> 0\n", addr);
    }
    return r;
}

const char* vm_describe(uint32_t va) { return vm_impl_describe(va); }

uint32_t vm_state(uint32_t va, uint32_t* run_base, uint32_t* run_bytes) {
    return vm_impl_state(va, run_base, run_bytes);
}

void vm_dump(FILE* out) { vm_impl_dump(out); }

int vm_write_image(const char* path, uint32_t root, uint32_t* runs_out, uint32_t* bytes_out) {
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    uint32_t runs = 0, total = 0, va = 0x10000u;
    int ok = fwrite("WDM2", 1, 4, f) == 4 && fwrite(&root, 4, 1, f) == 1;
    while (ok && va < WD_ARENA_SIZE) {
        uint32_t base = 0, bytes = 0;
        uint32_t state = vm_state(va, &base, &bytes);
        if (!bytes) break;
        uint32_t end = base + bytes;
        if (state == W32_MEM_COMMIT) {
            uint32_t head[2] = { va, end - va };
            ok = fwrite(head, sizeof head, 1, f) == 1 &&
                 fwrite(wd_host_range(va, end - va, 0), 1, end - va, f) == end - va;
            runs++;
            total += end - va;
        }
        if (end <= va) break;
        va = end;
    }
    ok = fclose(f) == 0 && ok;
    if (runs_out) *runs_out = runs;
    if (bytes_out) *bytes_out = total;
    return ok;
}

/* ---- the guest's imports ---- */
void imp_VirtualAlloc(void) { RET(vm_alloc(ARG(0), ARG(1), ARG(2), ARG(3))); STDRET(4); }
void imp_VirtualFree(void)  { RET(vm_free(ARG(0), ARG(1), ARG(2))); STDRET(3); }
void imp_VirtualQuery(void) { RET(vm_query(ARG(0), ARG(1))); STDRET(3); }

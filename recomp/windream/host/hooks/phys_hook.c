/*
 * WINDREAM recompilation - read-only diagnostic hook on PHYS_AddCandidate.
 *
 * lift.py inserts wd_hook_add_candidate(eax, edx, ebx, [esp]) at the entry of
 * PHYS_AddCandidate (0x45D10C): EAX collider, DL axis (0-2), EBX triangle,
 * [esp] the return address into PHYS_SweepAxis. Retail sets the axis bit
 * without checking it and links a new wall node (+0x30) whenever the record
 * then has all three bits (collider flag 4), a new floor node (+0x34) when it
 * has bits 0 and 2 (flag 8, axis != 1). A call on a bit that is already set
 * therefore leaks a node that PHYS_RemoveCandidate never unlinks.
 *
 * The hook looks the triangle's record up in the collider's tree (+0x2C: key
 * +0, bits +4, right +8, left +0xC) and reports calls whose bit is already
 * set. It only reads guest memory.
 *
 * Output (stderr): the first PER_CALLER lines per return address, and a
 * per-caller count every SUMMARY_MS while anything new was seen.
 *   [phys] re-add #N col C axis A tri T bits B -> wall|floor dup ret R c=X r=Y lo=I hi=J
 */
#include <stdio.h>
#include <stdlib.h>
#include "recomp_types.h"
#include "imports.h"

#define PER_CALLER 20
#define SUMMARY_MS 10000
#define MAX_CALLERS 32

static struct { uint32_t ret, calls, readds, dups; } g_callers[MAX_CALLERS];
static uint32_t g_ncallers, g_total, g_readds, g_last_summary, g_dirty;

static int caller_slot(uint32_t ret) {
    for (uint32_t i = 0; i < g_ncallers; i++)
        if (g_callers[i].ret == ret) return (int)i;
    if (g_ncallers == MAX_CALLERS) return -1;
    g_callers[g_ncallers].ret = ret;
    return (int)g_ncallers++;
}

static void summary(void) {
    fprintf(stderr, "[phys] summary at %u ms: %u adds, %u re-adds; blitter sites 4029D8=%02X 4029EF=%08X"
                    " (placeholders 12 / 12345678 = never patched)\n",
            host_elapsed_ms(), g_total, g_readds, MEM8(0x4029D8), MEM32(0x4029EF));
    for (uint32_t i = 0; i < g_ncallers; i++)
        fprintf(stderr, "[phys]   ret %08X: %u adds, %u re-adds, %u leaked nodes\n", g_callers[i].ret,
                g_callers[i].calls, g_callers[i].readds, g_callers[i].dups);
}

/* ---- per-sweep record, for replaying one PHYS_SweepAxis call elsewhere ----
 * wd_hook_sweep_axis runs at PHYS_SweepAxis entry (EAX world, EDX collider,
 * EBX axis); the add/remove hooks append to the current call's event list.
 * The first call that re-adds is printed at the next sweep entry (its own
 * events are complete by then) as [sweep] lines. With WD_PHYS_CAPTURE=1 the
 * runtime then faults on purpose so the crash handler writes a full dump:
 * the endpoint arrays and triangles in it are the static inputs of the call. */
#define MAX_EVENTS 4096
static struct { char kind; uint32_t tri, ret; } g_ev[MAX_EVENTS];
static uint32_t g_nev, g_sweeps, g_cur_seq, g_cur_world, g_cur_col, g_cur_axis, g_cur_lo, g_cur_hi;
static int32_t g_cur_c, g_cur_r;
static int g_target, g_reported;

static void report_target(void) {
    fprintf(stderr, "[sweep] call #%u world %08X col %08X axis %u c=%d r=%d entry lo=%u hi=%u exit lo=%u hi=%u events %u\n",
            g_cur_seq, g_cur_world, g_cur_col, g_cur_axis, g_cur_c, g_cur_r, g_cur_lo, g_cur_hi,
            MEM32(g_cur_col + 0x14 + g_cur_axis * 8), MEM32(g_cur_col + 0x18 + g_cur_axis * 8), g_nev);
    for (uint32_t i = 0; i < g_nev && i < MAX_EVENTS; i++)
        fprintf(stderr, "[sweep]   %c %08X ret %08X\n", g_ev[i].kind, g_ev[i].tri, g_ev[i].ret);
    fflush(stderr);
    const char* cap = getenv("WD_PHYS_CAPTURE");
    if (cap && *cap == '1') {
        fprintf(stderr, "[sweep] WD_PHYS_CAPTURE: faulting on purpose for a full dump\n");
        fflush(stderr);
        *(volatile uint32_t*)PTR(0x29) = 0;
    }
}

static uint32_t find_record(uint32_t col, uint32_t tri) {
    uint32_t rec = MEM32(col + 0x2C);
    for (int depth = 0; rec && depth < 4096; depth++) {
        uint32_t key = MEM32(rec);
        if (tri == key) return rec;
        rec = MEM32(rec + (tri > key ? 0x8 : 0xC));
    }
    return 0;
}

/* After a sweep of (col, axis) with centre c and radius r, a triangle must
 * have the axis bit iff it strictly overlaps [c-r, c+r] on that axis (checked
 * against the retail dumps: zero exceptions apart from exact ties). Returns
 * the number of violations and prints the first few. */
static uint32_t check_invariant(uint32_t world, uint32_t col, uint32_t axis, int32_t c, int32_t r, int print) {
    uint32_t n = MEM32(world), arr = MEM32(world + 4), bad = 0;
    int32_t L = c - r, H = c + r;
    for (uint32_t i = 0; i < 2 * n; i++) {
        if (MEM8(arr + i * 8 + 4)) continue;          /* min endpoints only: each triangle once */
        uint32_t tri = MEM32(arr + i * 8);
        int32_t mn = (int32_t)MEM32(tri + 0x48 + axis * 4), mx = (int32_t)MEM32(tri + 0x54 + axis * 4);
        if (mn == H || mx == L) continue;             /* exact tie: either answer is retail-consistent */
        int want = mn < H && mx > L;
        uint32_t rec = find_record(col, tri);
        int has = rec && (MEM8(rec + 4) & (1u << axis));
        if (want != has) {
            if (print && bad < 8)
                fprintf(stderr, "[inv]   tri %08X [%d, %d] vs [%d, %d]: %s\n", tri, mn, mx, L, H,
                        want ? "overlaps, bit missing" : "bit set, no overlap");
            bad++;
        }
    }
    return bad;
}

void wd_hook_sweep_axis(uint32_t world, uint32_t col, uint32_t axis_reg, uint32_t ret) {
    (void)ret;
    if (g_target && !g_reported) {
        g_reported = 1;
        report_target();
    }
    /* The previous call is complete now: check it. */
    static int inv_on = -1, inv_reported;
    if (inv_on < 0) { const char* s = getenv("WD_PHYS_INVARIANT"); inv_on = s && *s == '1'; }
    if (inv_on && !inv_reported && g_cur_col) {
        uint32_t bad = check_invariant(g_cur_world, g_cur_col, g_cur_axis, g_cur_c, g_cur_r, 0);
        if (bad) {
            inv_reported = 1;
            fprintf(stderr, "[inv] first violation after sweep call #%u: %u triangles\n", g_cur_seq, bad);
            check_invariant(g_cur_world, g_cur_col, g_cur_axis, g_cur_c, g_cur_r, 1);
            g_target = 1; g_reported = 1;
            report_target();
        }
    }
    uint32_t axis = axis_reg & 0xFF;
    g_sweeps++;
    g_cur_seq = g_sweeps; g_cur_world = world; g_cur_col = col; g_cur_axis = axis;
    g_cur_c = (int32_t)MEM32(col + axis * 4); g_cur_r = (int32_t)MEM32(col + 0xC);
    g_cur_lo = MEM32(col + 0x14 + axis * 8); g_cur_hi = MEM32(col + 0x18 + axis * 8);
    g_nev = 0;
}

void wd_hook_remove_candidate(uint32_t col, uint32_t axis_reg, uint32_t tri, uint32_t ret) {
    (void)axis_reg;
    if (col == g_cur_col && g_nev < MAX_EVENTS) { g_ev[g_nev].kind = 'R'; g_ev[g_nev].tri = tri; g_ev[g_nev].ret = ret; g_nev++; }
}

void wd_hook_add_candidate(uint32_t col, uint32_t axis_reg, uint32_t tri, uint32_t ret) {
    uint32_t axis = axis_reg & 0xFF;
    if (col == g_cur_col && g_nev < MAX_EVENTS) { g_ev[g_nev].kind = 'A'; g_ev[g_nev].tri = tri; g_ev[g_nev].ret = ret; g_nev++; }
    int slot = caller_slot(ret);
    g_total++;
    if (slot >= 0) g_callers[slot].calls++;

    uint32_t rec = MEM32(col + 0x2C);
    for (int depth = 0; rec && depth < 4096; depth++) {
        uint32_t key = MEM32(rec);
        if (tri == key) break;
        rec = MEM32(rec + (tri > key ? 0x8 : 0xC));
    }
    uint32_t bit = axis < 8 ? 1u << axis : 0;
    if (rec && bit && (MEM8(rec + 4) & bit)) {
        uint8_t bits = MEM8(rec + 4), flags = MEM8(col + 0x10);
        int wall = (flags & 4) && (bits & 7) == 7;
        int floor = (flags & 8) && bit != 2 && (bits & 5) == 5;
        g_readds++;
        g_dirty = 1;
        g_target = 1;
        if (slot >= 0) {
            g_callers[slot].readds++;
            g_callers[slot].dups += (uint32_t)(wall + floor);
        }
        if (slot < 0 || g_callers[slot].readds <= PER_CALLER)
            fprintf(stderr, "[phys] re-add #%u col %08X axis %u tri %08X bits %X ->%s%s ret %08X"
                            " c=%d r=%d lo=%u hi=%u\n",
                    g_readds, col, axis, tri, bits, wall ? " wall dup" : "", floor ? " floor dup" : "",
                    ret, (int32_t)MEM32(col + axis * 4), (int32_t)MEM32(col + 0xC),
                    axis < 3 ? MEM32(col + 0x14 + axis * 8) : 0, axis < 3 ? MEM32(col + 0x18 + axis * 8) : 0);
    }
    uint32_t now = host_elapsed_ms();
    if (g_dirty && now - g_last_summary >= SUMMARY_MS) {
        g_last_summary = now;
        g_dirty = 0;
        summary();
    }
}

/* Read-only probe before a single instruction (lift.py PROBES): the first
 * WD_PROBE_N (default 300) hits are printed as [probe] lines. */
void wd_probe(uint32_t va, uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t ebx,
              uint32_t esp, uint32_t ebp, uint32_t esi, uint32_t edi) {
    static long left = -1;
    if (left < 0) { const char* s = getenv("WD_PROBE_N"); left = s ? atol(s) : 300; }
    if (!left) return;
    left--;
    fprintf(stderr, "[probe] %08X eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X"
                    " tag(eax)=%X\n", va, eax, ecx, edx, ebx, esp, ebp, esi, edi,
            eax >= 0x10000 ? MEM32(eax - 4) : 0);
}

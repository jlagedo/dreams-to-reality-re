#include "render_boundary.h"
#include "render_surface_scope.h"
#include <cstdio>
#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #e);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static unsigned hits, violations;
static wd_surface_access last;
static unsigned surface_sweeps;
static wd_surface_id deferred_id;
static bool deferred_retired;
static void deferred_sweep() {
    ++surface_sweeps;
    wd_surface_desc desc{};
    if (deferred_id && !wd_surface_describe(deferred_id, &desc))
        deferred_retired = true;
}
static void replacement() { ++hits; }
static void reference() {
    if (wd_try_replace(0x47e498))
        hits += 1000;
}
static void violation(const wd_surface_access *a, void *) {
    last = *a;
    ++violations;
}
int main() {
    CHECK(wd_install_replacement(0x47e498, replacement));
    CHECK(!wd_install_replacement(0x47e498, replacement));
    CHECK(wd_try_replace(0x47e498) && hits == 1);
    wd_call_reference(reference);
    CHECK(hits == 1);
    CHECK(!wd_try_replace(0x459320));
    wd_clear_replacements();
    CHECK(!wd_try_replace(0x47e498));
    wd_surface_desc d{0x18000000, 62 * 48 * 2, 62, 48, 124, WD_SURFACE_565, 1};
    auto id = wd_surface_register(&d);
    CHECK(id);
    CHECK(wd_surface_register(&d) == id);
    CHECK(wd_surface_find(d.base + 126, 2) == id);
    CHECK(!wd_surface_find(d.base + d.bytes - 1, 2));
    auto alias = d;
    alias.base += 2;
    CHECK(!wd_surface_register(&alias));
    alias = d;
    alias.pitch = 123;
    CHECK(!wd_surface_register(&alias));
    alias = d;
    alias.base = 0xfffffff0;
    CHECK(!wd_surface_register(&alias));
    CHECK(wd_surface_check(0x1234, d.base, 4, 1));
    CHECK(wd_surface_set_authority(id, WD_SURFACE_GPU));
    wd_surface_set_violation_handler(violation, nullptr);
    CHECK(!wd_surface_check(0x5678, d.base - 2, 4, 0));
    CHECK(violations == 1 && last.instruction == 0x5678 && !last.write && last.surface == id);
    wd_surface_check_string(0x9876, 0, d.base + 124, 3, 2, -1, 0, 1);
    CHECK(violations == 2 && last.address == d.base + 120 && last.bytes == 6 && last.write);
    wd_surface_check_string(0x9876, 0, d.base, 0, 4, 1, 0, 1);
    CHECK(violations == 2);
    // Compare/scan iteration probes are reads, including backward walks and
    // a final element crossing into the surface. Unvisited later elements
    // must not be audited simply because REP's initial ECX allowed them.
    CHECK(wd_surface_check(0x1111, d.base - 4, 4, 0));
    CHECK(violations == 2);
    CHECK(!wd_surface_check(0x1111, d.base, 4, 0));
    CHECK(violations == 3 && !last.write && last.bytes == 4);
    CHECK(!wd_surface_check(0x1112, d.base + d.bytes - 1, 2, 0));
    CHECK(violations == 4 && last.address == d.base + d.bytes - 1);
    // PUSH and POP ranges use the same checker; neither requires CPU pixels.
    CHECK(!wd_surface_check(0x1113, d.base, 32, 1));
    CHECK(violations == 5 && last.write && last.bytes == 32);
    CHECK(wd_surface_unregister(id));
    CHECK(!wd_surface_find(d.base, 2));
    d.allocation_generation = 2;
    auto reused = wd_surface_register(&d);
    CHECK(reused && reused != id);
    CHECK(!wd_surface_set_authority(id, WD_SURFACE_GPU));
    wd_surface_reset();
    CHECK(!wd_surface_find(d.base, 2));
    auto after_reset = wd_surface_register(&d);
    CHECK(after_reset && after_reset != reused);
    wd_surface_reset();
    d.base = 0x18000000;
    auto first = wd_surface_register(&d);
    auto other_desc = d;
    other_desc.base += 0x10000;
    auto other = wd_surface_register(&other_desc);
    CHECK(first && other);
    // An interior page decommit invalidates the complete overlapping surface,
    // including aliases, while a distinct allocation survives.
    CHECK(wd_surface_invalidate_range(d.base + 4096, 4096) == 1);
    wd_surface_desc described{};
    CHECK(!wd_surface_describe(first, &described));
    CHECK(wd_surface_describe(other, &described));
    CHECK(!wd_surface_find(d.base, 2));
    ++d.allocation_generation;
    auto recommitted = wd_surface_register(&d);
    CHECK(recommitted && recommitted != first);
    CHECK(!wd_surface_set_authority(first, WD_SURFACE_GPU));
    CHECK(!wd_surface_invalidate_range(d.base, 0));
    CHECK(!wd_surface_invalidate_range(0xfffffff0, 32));
    CHECK(!wd_surface_invalidate_range(d.base + d.bytes, 2));
    CHECK(wd_surface_describe(recommitted, &described));
    // One release range can retire multiple suballocations atomically.
    CHECK(wd_surface_invalidate_range(d.base, 0x20000) == 2);
    CHECK(!wd_surface_describe(recommitted, &described));
    CHECK(!wd_surface_describe(other, &described));
    auto reused_after_free = wd_surface_register(&d);
    CHECK(reused_after_free && reused_after_free != recommitted);
    unsigned surface_depth = 0;
    deferred_id = reused_after_free;
    {
        wd::SurfaceScope outer(surface_depth, deferred_sweep);
        CHECK(surface_sweeps == 1 && !deferred_retired);
        // A worker invalidates the registry after an outer copy has borrowed
        // its source. A nested bind must not retire that source mid-operation.
        CHECK(wd_surface_invalidate_range(d.base, d.bytes) == 1);
        {
            wd::SurfaceScope nested(surface_depth, deferred_sweep);
            CHECK(surface_depth == 2 && surface_sweeps == 1 && !deferred_retired);
        }
        CHECK(surface_depth == 1 && !deferred_retired);
    }
    CHECK(surface_depth == 0 && !deferred_retired);
    {
        wd::SurfaceScope next_operation(surface_depth, deferred_sweep);
        CHECK(surface_sweeps == 2 && deferred_retired);
    }
    CHECK(surface_depth == 0);
    wd_surface_reset();
    std::puts("render boundary: replacement/reference scopes, aliases, pitch, generations, "
              "directional access checks passed");
}

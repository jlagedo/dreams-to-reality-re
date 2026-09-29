#include "render_boundary.h"
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
    std::puts("render boundary: replacement/reference scopes, aliases, pitch, generations, "
              "directional access checks passed");
}

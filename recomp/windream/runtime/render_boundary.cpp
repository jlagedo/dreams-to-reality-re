#include "render_boundary.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

namespace {
struct Replacement {
    uint32_t address;
    wd_guest_replacement function;
};
std::vector<Replacement> replacements;
thread_local unsigned reference_depth = 0;
struct Surface {
    wd_surface_id id;
    wd_surface_desc desc;
    wd_surface_authority authority;
};
std::vector<Surface> surfaces;
std::mutex surface_mutex;
uint64_t next_surface = 1;
wd_surface_violation violation_handler = nullptr;
void *violation_data = nullptr;
bool overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t m) { return a < b + m && b < a + n; }
bool same(const wd_surface_desc &a, const wd_surface_desc &b) {
    return a.base == b.base && a.bytes == b.bytes && a.width == b.width && a.height == b.height &&
           a.pitch == b.pitch && a.format == b.format &&
           a.allocation_generation == b.allocation_generation;
}
} // namespace
int wd_install_replacement(uint32_t address, wd_guest_replacement function) {
    if (!address || !function || reference_depth)
        return 0;
    auto it = std::lower_bound(replacements.begin(), replacements.end(), address,
                               [](const Replacement &r, uint32_t a) { return r.address < a; });
    if (it != replacements.end() && it->address == address)
        return 0;
    replacements.insert(it, {address, function});
    return 1;
}
int wd_try_replace(uint32_t address) {
    if (reference_depth || replacements.empty())
        return 0;
    auto it = std::lower_bound(replacements.begin(), replacements.end(), address,
                               [](const Replacement &r, uint32_t a) { return r.address < a; });
    if (it == replacements.end() || it->address != address)
        return 0;
    // Copy before invocation: the callback must not invalidate an iterator.
    const auto function = it->function;
    function();
    return 1;
}
void wd_call_reference(wd_guest_replacement original) {
    if (!original)
        std::abort();
    ++reference_depth;
    original();
    --reference_depth;
}
void wd_clear_replacements() {
    if (reference_depth)
        std::abort();
    replacements.clear();
}
wd_surface_id wd_surface_register(const wd_surface_desc *d) {
    if (!d || !d->base || !d->allocation_generation || !d->width || !d->height ||
        d->format < WD_SURFACE_565 || d->format > WD_SURFACE_P8)
        return 0;
    const uint64_t row = uint64_t(d->width) * (d->format == WD_SURFACE_P8 ? 1 : 2);
    if (d->pitch < row || uint64_t(d->pitch) * (d->height - 1) + row > d->bytes ||
        uint64_t(d->base) + d->bytes > 0x100000000ull)
        return 0;
    std::lock_guard<std::mutex> lock(surface_mutex);
    for (const auto &s : surfaces) {
        if (same(s.desc, *d))
            return s.id;
        if (overlap(d->base, d->bytes, s.desc.base, s.desc.bytes))
            return 0;
    }
    if (!next_surface)
        return 0;
    const auto id = next_surface++;
    surfaces.push_back({id, *d, WD_SURFACE_CPU});
    return id;
}
int wd_surface_unregister(wd_surface_id id) {
    std::lock_guard<std::mutex> lock(surface_mutex);
    auto it = std::find_if(surfaces.begin(), surfaces.end(),
                           [&](const Surface &s) { return s.id == id; });
    if (it == surfaces.end())
        return 0;
    surfaces.erase(it);
    return 1;
}
uint32_t wd_surface_invalidate_range(uint32_t base, uint32_t bytes) {
    if (!bytes || uint64_t(base) + bytes > 0x100000000ull)
        return 0;
    std::lock_guard<std::mutex> lock(surface_mutex);
    const auto before = surfaces.size();
    surfaces.erase(std::remove_if(surfaces.begin(), surfaces.end(),
                                   [&](const Surface &s) {
                                       return overlap(base, bytes, s.desc.base, s.desc.bytes);
                                   }),
                   surfaces.end());
    return uint32_t(before - surfaces.size());
}
wd_surface_id wd_surface_find(uint32_t address, uint32_t bytes) {
    if (!bytes || uint64_t(address) + bytes > 0x100000000ull)
        return 0;
    std::lock_guard<std::mutex> lock(surface_mutex);
    for (const auto &s : surfaces)
        if (address >= s.desc.base &&
            uint64_t(address) + bytes <= uint64_t(s.desc.base) + s.desc.bytes)
            return s.id;
    return 0;
}
int wd_surface_describe(wd_surface_id id, wd_surface_desc *out) {
    if (!out)
        return 0;
    std::lock_guard<std::mutex> lock(surface_mutex);
    for (const auto &s : surfaces)
        if (s.id == id) {
            *out = s.desc;
            return 1;
        }
    return 0;
}
int wd_surface_set_authority(wd_surface_id id, wd_surface_authority authority) {
    if (authority != WD_SURFACE_CPU && authority != WD_SURFACE_GPU)
        return 0;
    std::lock_guard<std::mutex> lock(surface_mutex);
    for (auto &s : surfaces)
        if (s.id == id) {
            s.authority = authority;
            return 1;
        }
    return 0;
}
int wd_surface_check(uint32_t instruction, uint32_t address, uint32_t bytes, int write) {
    if (!bytes)
        return 1;
    wd_surface_access access{};
    wd_surface_violation callback = nullptr;
    void *data = nullptr;
    {
        std::lock_guard<std::mutex> lock(surface_mutex);
        for (const auto &s : surfaces)
            if (s.authority == WD_SURFACE_GPU &&
                overlap(address, bytes, s.desc.base, s.desc.bytes)) {
                access = {instruction, address, bytes, write, s.id, s.authority};
                callback = violation_handler;
                data = violation_data;
                break;
            }
    }
    if (!access.surface)
        return 1;
    if (callback)
        callback(&access, data);
    else {
        std::fprintf(stderr, "[render] unclassified CPU %s at %08x: %08x + %u, surface %llu\n",
                     write ? "write" : "read", instruction, address, bytes,
                     (unsigned long long)access.surface);
        std::abort();
    }
    return 0;
}
void wd_surface_set_violation_handler(wd_surface_violation callback, void *data) {
    std::lock_guard<std::mutex> lock(surface_mutex);
    violation_handler = callback;
    violation_data = data;
}
void wd_surface_reset() {
    std::lock_guard<std::mutex> lock(surface_mutex);
    surfaces.clear();
    violation_handler = nullptr;
    violation_data = nullptr;
    // Never recycle IDs across a reset; stale queued handles remain invalid.
}
void wd_surface_check_string(uint32_t instruction, uint32_t source, uint32_t destination,
                             uint32_t count, uint32_t width, int direction, int read, int write) {
    if (!count)
        return;
    const uint64_t bytes = uint64_t(count) * width;
    if (!width || bytes > UINT32_MAX)
        std::abort();
    const auto probe = [&](uint32_t address, int is_write) {
        if (direction < 0) {
            if (bytes - width > address)
                std::abort();
            address -= uint32_t(bytes - width);
        }
        if (uint64_t(address) + bytes > 0x100000000ull)
            std::abort();
        wd_surface_check(instruction, address, uint32_t(bytes), is_write);
    };
    if (read)
        probe(source, 0);
    if (write)
        probe(destination, 1);
}

#ifndef WD_RENDER_BOUNDARY_H
#define WD_RENDER_BOUNDARY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wd_guest_replacement)(void);
/* Configure handlers before guest execution on the main thread. Handlers
 * must implement their own documented guest return, registers and flags. */
int wd_install_replacement(uint32_t address, wd_guest_replacement);
int wd_try_replace(uint32_t address);
void wd_call_reference(wd_guest_replacement original);
void wd_clear_replacements(void);

typedef uint64_t wd_surface_id;
typedef enum wd_surface_format { WD_SURFACE_565, WD_SURFACE_555, WD_SURFACE_P8 } wd_surface_format;
typedef enum wd_surface_authority { WD_SURFACE_CPU, WD_SURFACE_GPU } wd_surface_authority;
typedef struct wd_surface_desc {
    uint32_t base, bytes, width, height, pitch;
    wd_surface_format format;
    uint64_t allocation_generation;
} wd_surface_desc;
typedef struct wd_surface_access {
    uint32_t instruction, address, bytes;
    int write;
    wd_surface_id surface;
    wd_surface_authority authority;
} wd_surface_access;
typedef void (*wd_surface_violation)(const wd_surface_access *, void *);
/* Exact aliases return the existing ID. Partial/ambiguous overlaps fail;
 * the adapter must register the owning allocation rather than guess. */
wd_surface_id wd_surface_register(const wd_surface_desc *);
int wd_surface_unregister(wd_surface_id);
wd_surface_id wd_surface_find(uint32_t address, uint32_t bytes);
int wd_surface_describe(wd_surface_id, wd_surface_desc *);
int wd_surface_set_authority(wd_surface_id, wd_surface_authority);
/* Returns 0 for an unclassified CPU access to a GPU-owned range. This is an
 * ownership check, not a readback request. The strict audit callback aborts. */
int wd_surface_check(uint32_t instruction, uint32_t address, uint32_t bytes, int write);
void wd_surface_set_violation_handler(wd_surface_violation, void *);
void wd_surface_reset(void);
void wd_surface_check_string(uint32_t instruction, uint32_t source, uint32_t destination,
                             uint32_t count, uint32_t width, int direction, int read, int write);
#ifdef WD_RENDER_AUDIT
#define WD_AUDIT_MEMORY(va, address, bytes, write)                                                 \
    ((void)wd_surface_check((va), (address), (bytes), (write)))
#define WD_AUDIT_STRING(va, src, dst, count, width, dir, read, write)                              \
    wd_surface_check_string((va), (src), (dst), (count), (width), (dir), (read), (write))
#else
#define WD_AUDIT_MEMORY(va, address, bytes, write) ((void)0)
#define WD_AUDIT_STRING(va, src, dst, count, width, dir, read, write) ((void)0)
#endif
#ifdef __cplusplus
}
#endif
#endif

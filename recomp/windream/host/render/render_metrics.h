#ifndef WD_RENDER_METRICS_H
#define WD_RENDER_METRICS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum wd_render_metric_category {
    WD_METRIC_SCENE,
    WD_METRIC_UPLOAD,
    WD_METRIC_COPY,
    WD_METRIC_UI,
    WD_METRIC_OUTPUT,
    WD_METRIC_PREPARE, /* CPU-only source capture and preparation. */
    WD_METRIC_COUNT
} wd_render_metric_category;

/* Optional renderer-thread-only instrumentation. The host still owns commit,
 * present and frame pacing. D3D pointers are borrowed with retained COM refs.
 * Disabled unless WD_RENDER_PROFILE=1; a missing backend permits CPU samples.
 * Nested scopes are skipped. End(0) is a no-op; tokens must end in their frame. */
void wd_render_metrics_initialize(void *device, void *context);
uint64_t wd_render_metrics_begin(wd_render_metric_category category);
void wd_render_metrics_end(uint64_t token);
void wd_render_metrics_frame_completed(void);
void wd_render_metrics_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif

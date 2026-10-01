// Standalone bookkeeping regression; include the production implementation so
// internal ring/percentile checks do not add a testing API to the runtime.
#include "../../host/render/render_metrics.cpp"
#define check(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "metrics check failed: %s at line %d\n", #condition, __LINE__); \
    std::exit(1); } } while (false)

int main() {
    Samples samples;
    for (unsigned i = 1; i <= 2000; ++i)
        samples.add(double(i));
    check(samples.size == 1024 && samples.total == 2000);
    const auto quantiles = samples.percentiles();
    check(quantiles[0] == 1488 && quantiles[1] == 1949 && quantiles[2] == 1990);
#ifdef _WIN32
    _putenv_s("WD_RENDER_PROFILE", "0");
#else
    setenv("WD_RENDER_PROFILE", "0", 1);
#endif
    wd_render_metrics_initialize(nullptr, nullptr);
    check(wd_render_metrics_begin(WD_METRIC_SCENE) == 0);
    wd_render_metrics_end(0);
    wd_render_metrics_frame_completed();
    check(!metrics.enabled && metrics.frames == 0);
#ifdef _WIN32
    _putenv_s("WD_RENDER_PROFILE", "1");
#else
    setenv("WD_RENDER_PROFILE", "1", 1);
#endif
    wd_render_metrics_initialize(nullptr, nullptr);
    const auto token = wd_render_metrics_begin(WD_METRIC_PREPARE);
    check(token != 0);
    check(wd_render_metrics_begin(WD_METRIC_UPLOAD) == 0);
    wd_render_metrics_end(0);
    check(metrics.active == token);
    wd_render_metrics_end(token + 1);
    check(metrics.active == token && metrics.invalid_ends == 1);
    wd_render_metrics_end(token);
    check(metrics.categories[WD_METRIC_PREPARE].cpu.total == 1);
    check(metrics.categories[WD_METRIC_UPLOAD].skipped == 1);
    check(metrics.categories[WD_METRIC_UPLOAD].cpu.total == 0);
    for (unsigned i = 0; i < 1100; ++i)
        wd_render_metrics_frame_completed();
    check(metrics.frames == 1100 && metrics.frame_intervals.size == sample_capacity);
    check(metrics.frame_intervals.total == 1099);
    for (const auto &query : metrics.queries)
        check(!query.pending);
    wd_render_metrics_shutdown();
    check(!metrics.enabled && metrics.frames == 0);
    std::puts("renderer metrics: bounded rings, percentiles, disabled/nested scopes and reset passed");
}

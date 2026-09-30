#include "render_metrics.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t sample_capacity = 1024, query_capacity = 64;
constexpr std::array<const char *, WD_METRIC_COUNT> names{
    "scene", "upload", "copy", "ui", "output", "prepare"};
struct Samples {
    std::array<double, sample_capacity> values{};
    size_t next = 0, size = 0;
    uint64_t total = 0;
    void add(double milliseconds) {
        values[next] = milliseconds;
        next = (next + 1) % values.size();
        size = std::min(size + 1, values.size());
        ++total;
    }
    std::array<double, 3> percentiles() const {
        if (!size)
            return {};
        std::vector<double> sorted(values.begin(), values.begin() + size);
        std::sort(sorted.begin(), sorted.end());
        const auto at = [&](size_t percent) {
            const size_t rank = (percent * size + 99) / 100;
            return sorted[rank - 1];
        };
        return {at(50), at(95), at(99)};
    }
};
struct Category {
    Samples cpu, gpu;
    uint64_t skipped = 0, disjoint = 0, failed = 0;
};
struct Query {
#ifdef _WIN32
    ID3D11Query *disjoint = nullptr, *begin = nullptr, *end = nullptr;
#endif
    bool pending = false;
    wd_render_metric_category category = WD_METRIC_SCENE;
};
struct Metrics {
    bool enabled = false;
    std::array<Category, WD_METRIC_COUNT> categories;
    std::array<Query, query_capacity> queries;
    size_t next_query = 0;
    int active_query = -1;
    uint64_t active = 0, next_token = 0, frames = 0, invalid_ends = 0;
    wd_render_metric_category active_category = WD_METRIC_SCENE;
    Clock::time_point begin_time{}, previous_frame{};
    Samples frame_intervals;
#ifdef _WIN32
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
#endif
} metrics;

double elapsed(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}
void poll() {
#ifdef _WIN32
    if (!metrics.context)
        return;
    for (auto &query : metrics.queries) {
        if (!query.pending)
            continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
        UINT64 begin = 0, end = 0;
        const HRESULT a = metrics.context->GetData(query.disjoint, &disjoint, sizeof disjoint,
                                                    D3D11_ASYNC_GETDATA_DONOTFLUSH);
        const HRESULT b = metrics.context->GetData(query.begin, &begin, sizeof begin,
                                                    D3D11_ASYNC_GETDATA_DONOTFLUSH);
        const HRESULT c = metrics.context->GetData(query.end, &end, sizeof end,
                                                    D3D11_ASYNC_GETDATA_DONOTFLUSH);
        auto &category = metrics.categories[query.category];
        if (FAILED(a) || FAILED(b) || FAILED(c)) {
            query.pending = false;
            ++category.failed;
            continue;
        }
        if (a == S_FALSE || b == S_FALSE || c == S_FALSE)
            continue;
        query.pending = false;
        if (disjoint.Disjoint || !disjoint.Frequency || end < begin)
            ++category.disjoint;
        else
            category.gpu.add(double(end - begin) * 1000 / double(disjoint.Frequency));
    }
#endif
}
void report(const char *reason) {
    const auto frame = metrics.frame_intervals.percentiles();
    std::fprintf(stderr,
                 "[render-profile] {\"reason\":\"%s\",\"frames\":%llu,\"window_capacity\":%zu,"
                 "\"query_capacity\":%zu,\"invalid_ends\":%llu,"
                 "\"frame_ms\":{\"samples\":%llu,\"p50\":%.6f,\"p95\":%.6f,\"p99\":%.6f},"
                 "\"regions\":{",
                 reason, (unsigned long long)metrics.frames, sample_capacity, query_capacity,
                 (unsigned long long)metrics.invalid_ends,
                 (unsigned long long)metrics.frame_intervals.total, frame[0], frame[1], frame[2]);
    for (size_t i = 0; i < names.size(); ++i) {
        const auto &category = metrics.categories[i];
        const auto cpu = category.cpu.percentiles(), gpu = category.gpu.percentiles();
        size_t pending = 0;
        for (const auto &query : metrics.queries)
            pending += query.pending && size_t(query.category) == i;
        std::fprintf(stderr,
                     "%s\"%s\":{\"cpu_samples\":%llu,\"cpu_ms\":{\"p50\":%.6f,\"p95\":%.6f,\"p99\":%.6f},"
                     "\"gpu_completed\":%llu,\"gpu_pending\":%zu,\"gpu_ms\":{\"p50\":%.6f,\"p95\":%.6f,\"p99\":%.6f},"
                     "\"skipped\":%llu,\"disjoint\":%llu,\"failed\":%llu}",
                     i ? "," : "", names[i], (unsigned long long)category.cpu.total,
                     cpu[0], cpu[1], cpu[2], (unsigned long long)category.gpu.total, pending,
                     gpu[0], gpu[1], gpu[2], (unsigned long long)category.skipped,
                     (unsigned long long)category.disjoint, (unsigned long long)category.failed);
    }
    std::fputs("}}\n", stderr);
}
} // namespace

void wd_render_metrics_initialize(void *device, void *context) {
    wd_render_metrics_shutdown();
    const char *enabled = std::getenv("WD_RENDER_PROFILE");
    if (!enabled || std::strcmp(enabled, "1") != 0)
        return;
    metrics.enabled = true;
#ifdef _WIN32
    metrics.device = static_cast<ID3D11Device *>(device);
    metrics.context = static_cast<ID3D11DeviceContext *>(context);
    if (metrics.device)
        metrics.device->AddRef();
    if (metrics.context)
        metrics.context->AddRef();
#else
    (void)device;
    (void)context;
#endif
}

uint64_t wd_render_metrics_begin(wd_render_metric_category category) {
    if (!metrics.enabled || category < 0 || category >= WD_METRIC_COUNT)
        return 0;
    auto &region = metrics.categories[category];
    if (metrics.active) {
        ++region.skipped;
        return 0;
    }
    metrics.active = ++metrics.next_token;
    metrics.active_category = category;
    metrics.active_query = -1;
    metrics.begin_time = Clock::now();
#ifdef _WIN32
    if (category != WD_METRIC_PREPARE && metrics.device && metrics.context) {
        for (size_t i = 0; i < metrics.queries.size(); ++i) {
            const size_t index = (metrics.next_query + i) % metrics.queries.size();
            auto &query = metrics.queries[index];
            if (query.pending)
                continue;
            if (!query.disjoint) {
                D3D11_QUERY_DESC description{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
                HRESULT status = metrics.device->CreateQuery(&description, &query.disjoint);
                description.Query = D3D11_QUERY_TIMESTAMP;
                if (SUCCEEDED(status))
                    status = metrics.device->CreateQuery(&description, &query.begin);
                if (SUCCEEDED(status))
                    status = metrics.device->CreateQuery(&description, &query.end);
                if (FAILED(status)) {
                    if (query.disjoint) query.disjoint->Release();
                    if (query.begin) query.begin->Release();
                    if (query.end) query.end->Release();
                    query = {};
                    ++region.failed;
                    break;
                }
            }
            query.category = category;
            metrics.active_query = int(index);
            metrics.next_query = (index + 1) % metrics.queries.size();
            metrics.context->Begin(query.disjoint);
            metrics.context->End(query.begin);
            break;
        }
        if (metrics.active_query < 0)
            ++region.skipped;
    }
#endif
    return metrics.active;
}

void wd_render_metrics_end(uint64_t token) {
    if (!metrics.enabled || !token)
        return;
    if (metrics.active != token) {
        ++metrics.invalid_ends;
        return;
    }
    metrics.categories[metrics.active_category].cpu.add(elapsed(metrics.begin_time, Clock::now()));
#ifdef _WIN32
    if (metrics.active_query >= 0) {
        auto &query = metrics.queries[size_t(metrics.active_query)];
        metrics.context->End(query.end);
        metrics.context->End(query.disjoint);
        query.pending = true;
    }
#endif
    metrics.active = 0;
    metrics.active_query = -1;
}

void wd_render_metrics_frame_completed(void) {
    if (!metrics.enabled)
        return;
    const auto now = Clock::now();
    if (metrics.frames)
        metrics.frame_intervals.add(elapsed(metrics.previous_frame, now));
    metrics.previous_frame = now;
    ++metrics.frames;
    poll();
    if (metrics.frames % 250 == 0)
        report("periodic");
}

void wd_render_metrics_shutdown(void) {
    if (!metrics.enabled)
        return;
    if (metrics.active)
        wd_render_metrics_end(metrics.active);
    poll();
    report("shutdown");
#ifdef _WIN32
    for (auto &query : metrics.queries) {
        if (query.disjoint) query.disjoint->Release();
        if (query.begin) query.begin->Release();
        if (query.end) query.end->Release();
    }
    if (metrics.context) metrics.context->Release();
    if (metrics.device) metrics.device->Release();
#endif
    metrics = {};
}

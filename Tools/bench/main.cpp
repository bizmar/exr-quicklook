// Times the three paths against the plan's §6.7 budget:
//   thumbnail      exr_render at Finder's largest thumbnail (1024 px)   < 300 ms
//   preview        exr_open at the preview cap (2048 px) + first render  < 500 ms
//   exposure drag  exr_source_render on the open source (no re-decode)
// Each is run several times; the first (cold-ish: file just read) and the
// median are reported. Run on the target hardware, Release build.
//
// usage: bench [--runs N] <file.exr>...
#include "EXRCore/exr_api.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

static void report(const char* what, std::vector<double> v, double budget) {
    const double first = v.front();
    std::sort(v.begin(), v.end());
    const double median = v[v.size() / 2];
    std::printf("  %-14s first %7.1f ms   median %7.1f ms   %s\n", what, first, median,
                budget <= 0 ? "" : (median <= budget ? "within budget" : "OVER BUDGET"));
}

int main(int argc, char** argv) {
    int runs = 7;
    std::vector<const char*> files;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--runs") == 0 && i + 1 < argc) runs = std::max(1, std::atoi(argv[++i]));
        else files.push_back(argv[i]);
    }
    if (files.empty()) { std::fprintf(stderr, "usage: bench [--runs N] <file.exr>...\n"); return 2; }

    for (const char* f : files) {
        std::printf("%s\n", f);
        std::vector<double> thumb, preview, drag;
        for (int r = 0; r < runs; ++r) {
            EXRRenderOptions o{};
            o.max_edge = 1024;
            EXRRenderResult res{};
            auto t0 = Clock::now();
            if (!exr_render(f, &o, &res)) { std::printf("  render failed\n"); break; }
            thumb.push_back(ms_since(t0));
            exr_render_free(&res);

            t0 = Clock::now();
            EXRSource* src = exr_open(f, 2048, 0, nullptr);
            if (!src) { std::printf("  open failed\n"); break; }
            EXRRenderOptions p{};
            p.max_edge = 2048;
            exr_source_render(src, &p, &res);
            preview.push_back(ms_since(t0));
            exr_render_free(&res);

            p.exposure_stops = 1.0f + 0.1f * r;
            t0 = Clock::now();
            exr_source_render(src, &p, &res);
            drag.push_back(ms_since(t0));
            exr_render_free(&res);
            exr_close(src);
        }
        if (thumb.empty() || preview.empty()) continue;
        report("thumbnail", thumb, 300);
        report("preview", preview, 500);
        report("exposure drag", drag, 0);
    }
    return 0;
}

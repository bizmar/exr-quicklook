// Mutational fuzzer over the whole public API: open, render in several views,
// layer switch, describe. The Command Line Tools ship no libFuzzer, so this is
// a plain loop. Run through Tools/fuzz.sh, which builds it with AddressSanitizer
// and UndefinedBehaviorSanitizer on EXRCore and keeps every crasher.
// Usage: fuzz <workfile> <seconds> <rng-seed> <seed files...>
// The current input is always at <workfile>, so after a sanitizer abort the
// crasher is on disk.
#include "EXRCore/exr_api.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>
static std::vector<uint8_t> load(const char* p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}
int main(int argc, char** argv) {
    const char* work = argv[1];
    const double secs = atof(argv[2]);
    std::mt19937 rng(strtoul(argv[3], nullptr, 10));
    std::vector<std::vector<uint8_t>> seeds;
    for (int i = 4; i < argc; ++i) { auto s = load(argv[i]); if (s.size() > 8 && s.size() < (8 << 20)) seeds.push_back(s); }
    const uint32_t interesting[] = {0, 1, 0x7f, 0x80, 0xff, 0x7fffffff, 0x80000000, 0xffffffff, 0xfffffffe, 65535, 65536, 0x40000000};
    if (seeds.empty()) { fprintf(stderr, "no seeds loaded from %d args\n", argc - 4); return 2; }
    auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(secs);
    long iters = 0, opened = 0;
    while (std::chrono::steady_clock::now() < end) {
        std::vector<uint8_t> d = seeds[rng() % seeds.size()];
        const int muts = 1 + rng() % 8;
        for (int m = 0; m < muts; ++m) {
            // Bias towards the header, where the parsing logic is.
            const size_t lim = (rng() % 4) ? std::min<size_t>(d.size(), 2048) : d.size();
            const size_t at = rng() % lim;
            switch (rng() % 5) {
                case 0: d[at] ^= uint8_t(1u << (rng() % 8)); break;
                case 1: d[at] = uint8_t(rng()); break;
                case 2: if (at + 4 <= d.size()) { uint32_t v = interesting[rng() % 12]; memcpy(&d[at], &v, 4); } break;
                case 3: if (at + 4 <= d.size()) { uint32_t v; memcpy(&v, &d[at], 4); v += uint32_t(rng() % 33) - 16u; memcpy(&d[at], &v, 4); } break;
                case 4: if (rng() % 8 == 0) d.resize(std::max<size_t>(9, at)); break;
            }
        }
        { std::ofstream f(work, std::ios::binary | std::ios::trunc); f.write((const char*)d.data(), d.size()); }
        EXRSource* s = exr_open(work, 64 + rng() % 512, rng() % 2, nullptr);
        if (s) {
            ++opened;
            for (int v = 0; v < 2; ++v) {
                EXRRenderOptions o{}; o.max_edge = 128; o.channel_view = rng() % 6; o.exposure_stops = float(int(rng() % 25) - 12);
                if (v) o.view = "raw";
                EXRRenderResult r{}; exr_source_render(s, &o, &r); exr_render_free(&r);
            }
            const int n = exr_source_layer_count(s);
            if (n > 1) { const char* id = exr_source_layer_id(s, rng() % n); if (id) { std::string keep = id; EXRSource* t = exr_open(work, 128, 0, keep.c_str()); exr_close(t); } }
            exr_close(s);
        }
        char buf[8192]; exr_describe(work, buf, sizeof buf);
        ++iters;
    }
    printf("iterations %ld, opened %ld\n", iters, opened);
}

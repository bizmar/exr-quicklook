#include "EXRCore/exr_decode.h"
#include "EXRCore/exr_reader.h"
#include <cmath>
#include <cstdio>
using namespace exrcore;
int main(int argc, char** argv) {
    FileInfo info; std::string err;
    if (!inspect_file(argv[1], info, err)) { std::printf("inspect failed: %s\n", err.c_str()); return 1; }
    Image img;
    if (!decode_layer(argv[1], info, select_primary_layer(info.parts), img, err)) {
        std::printf("decode failed: %s\n", err.c_str()); return 1;
    }
    int nan = 0, inf = 0; float lo = 1e30f, hi = -1e30f;
    for (float v : img.rgba) {
        if (std::isnan(v)) ++nan;
        if (std::isinf(v)) ++inf;
        lo = std::fmin(lo, v); hi = std::fmax(hi, v);
    }
    std::printf("  %dx%d  NaN=%d  Inf=%d  range=[%.1f, %.1f]\n", img.width, img.height, nan, inf, lo, hi);
    return (nan || inf) ? 1 : 0;
}

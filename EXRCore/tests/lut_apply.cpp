// Reads "r g b" scene-linear AP1 triples on stdin, writes transformed display
// code values on stdout. Used by Tools/verify-lut.sh to compare our runtime
// against OCIO's direct evaluation (the Option D golden check).
#include "EXRCore/exr_lut.h"
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
    const char* want = argc > 1 ? argv[1] : exrcore::kDefaultLutName;
    const exrcore::BakedLut* lut = exrcore::find_lut(want);
    if (!lut) { std::fprintf(stderr, "no such LUT: %s\n", want); return 2; }
    float r, g, b;
    while (std::scanf("%f %f %f", &r, &g, &b) == 3) {
        exrcore::apply_lut(*lut, r, g, b);
        std::printf("%.6f %.6f %.6f\n", r, g, b);
    }
    return 0;
}

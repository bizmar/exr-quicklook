#include "EXRCore/exr_lut.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace exrcore {
namespace {

// Minimal IEEE half -> float. Avoids depending on Imath here so exr_lut stays
// linkable into tests without OpenEXR.
inline float half_to_float(std::uint16_t h) {
    const std::uint32_t sign = std::uint32_t(h & 0x8000u) << 16;
    std::uint32_t exp = (h >> 10) & 0x1Fu;
    std::uint32_t man = h & 0x3FFu;
    std::uint32_t bits;
    if (exp == 0) {
        if (man == 0) {
            bits = sign;                       // +/- zero
        } else {
            // Subnormal: normalise it.
            exp = 127 - 15 + 1;
            while ((man & 0x400u) == 0) { man <<= 1; --exp; }
            man &= 0x3FFu;
            bits = sign | (exp << 23) | (man << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (man << 13);   // inf / NaN
    } else {
        bits = sign | ((exp - 15 + 127) << 23) | (man << 13);
    }
    float out;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

struct RGB { float r, g, b; };

inline RGB fetch(const BakedLut& lut, int ir, int ig, int ib) {
    const int n = lut.size;
    ir = std::clamp(ir, 0, n - 1);
    ig = std::clamp(ig, 0, n - 1);
    ib = std::clamp(ib, 0, n - 1);
    const std::size_t idx = (std::size_t(ib) * n * n + std::size_t(ig) * n + ir) * 3;
    return {half_to_float(lut.data[idx]),
            half_to_float(lut.data[idx + 1]),
            half_to_float(lut.data[idx + 2])};
}

inline RGB lerp4(const RGB& c000, const RGB& cA, const RGB& cB, const RGB& c111,
                 float wA, float wB, float wC) {
    return {c000.r * (1 - wA) + cA.r * (wA - wB) + cB.r * (wB - wC) + c111.r * wC,
            c000.g * (1 - wA) + cA.g * (wA - wB) + cB.g * (wB - wC) + c111.g * wC,
            c000.b * (1 - wA) + cA.b * (wA - wB) + cB.b * (wB - wC) + c111.b * wC};
}

}  // namespace

float acescct_encode(float linear) {
    if (!(linear > 0.0f)) return 0.0729055341958355f;   // also catches NaN
    if (linear <= 0.0078125f) return 10.5402377416545f * linear + 0.0729055341958355f;
    return (std::log2(linear) + 9.72f) / 17.52f;
}

const BakedLut* find_lut(const std::string& name) {
    for (int i = 0; i < kBakedLutCount; ++i) {
        if (name == kBakedLuts[i].name) return &kBakedLuts[i];
    }
    return nullptr;
}

void apply_lut(const BakedLut& lut, float& r, float& g, float& b) {
    const int n = lut.size;
    const float scale = float(n - 1);

    // Shaper. Values outside [0,1] after encoding are clamped: the LUT domain
    // is the whole representable ACEScct range, so this only catches extremes.
    const float xr = std::clamp(acescct_encode(r), 0.0f, 1.0f) * scale;
    const float xg = std::clamp(acescct_encode(g), 0.0f, 1.0f) * scale;
    const float xb = std::clamp(acescct_encode(b), 0.0f, 1.0f) * scale;

    const int i0r = std::min(int(xr), n - 2);
    const int i0g = std::min(int(xg), n - 2);
    const int i0b = std::min(int(xb), n - 2);
    const float fr = xr - float(i0r), fg = xg - float(i0g), fb = xb - float(i0b);

    const RGB c000 = fetch(lut, i0r, i0g, i0b);
    const RGB c111 = fetch(lut, i0r + 1, i0g + 1, i0b + 1);

    // Tetrahedral interpolation: pick one of six tetrahedra by the ordering of
    // the fractional coordinates. Better than trilinear on the strongly
    // non-linear gamut-compression regions -- see aces-transform-options.md §4.
    RGB out;
    if (fr > fg) {
        if (fg > fb)       out = lerp4(c000, fetch(lut,i0r+1,i0g,i0b), fetch(lut,i0r+1,i0g+1,i0b), c111, fr, fg, fb);
        else if (fr > fb)  out = lerp4(c000, fetch(lut,i0r+1,i0g,i0b), fetch(lut,i0r+1,i0g,i0b+1), c111, fr, fb, fg);
        else               out = lerp4(c000, fetch(lut,i0r,i0g,i0b+1), fetch(lut,i0r+1,i0g,i0b+1), c111, fb, fr, fg);
    } else {
        if (fb > fg)       out = lerp4(c000, fetch(lut,i0r,i0g,i0b+1), fetch(lut,i0r,i0g+1,i0b+1), c111, fb, fg, fr);
        else if (fb > fr)  out = lerp4(c000, fetch(lut,i0r,i0g+1,i0b), fetch(lut,i0r,i0g+1,i0b+1), c111, fg, fb, fr);
        else               out = lerp4(c000, fetch(lut,i0r,i0g+1,i0b), fetch(lut,i0r+1,i0g+1,i0b), c111, fg, fr, fb);
    }
    r = out.r; g = out.g; b = out.b;
}

}  // namespace exrcore

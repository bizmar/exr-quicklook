#include "EXRCore/exr_color.h"

#include <cmath>

namespace exrcore {

const float kAP1Chromaticities[8] = {
    0.713f, 0.293f,      // red
    0.165f, 0.830f,      // green
    0.128f, 0.044f,      // blue
    0.32168f, 0.33767f,  // white (ACES ~D60)
};

namespace {

// Bradford cone response, the standard CAT used by ACES and ICC alike.
constexpr Mat3 kBradford = {
     0.8951f,  0.2664f, -0.1614f,
    -0.7502f,  1.7135f,  0.0367f,
     0.0389f, -0.0685f,  1.0296f,
};

// xyY (Y = 1) to XYZ.
void xy_to_xyz(float x, float y, float& X, float& Y, float& Z) {
    Y = 1.0f;
    X = (y != 0.0f) ? x / y : 0.0f;
    Z = (y != 0.0f) ? (1.0f - x - y) / y : 0.0f;
}

}  // namespace

Mat3 mat3_multiply(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < 3; ++k) sum += a[i * 3 + k] * b[k * 3 + j];
            r[i * 3 + j] = sum;
        }
    }
    return r;
}

bool mat3_invert(const Mat3& m, Mat3& out) {
    const float a = m[0], b = m[1], c = m[2];
    const float d = m[3], e = m[4], f = m[5];
    const float g = m[6], h = m[7], i = m[8];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-12f) return false;
    const float inv = 1.0f / det;
    out = {
        (e * i - f * h) * inv, (c * h - b * i) * inv, (b * f - c * e) * inv,
        (f * g - d * i) * inv, (a * i - c * g) * inv, (c * d - a * f) * inv,
        (d * h - e * g) * inv, (b * g - a * h) * inv, (a * e - b * d) * inv,
    };
    return true;
}

void mat3_apply(const Mat3& m, float& r, float& g, float& b) {
    const float x = m[0] * r + m[1] * g + m[2] * b;
    const float y = m[3] * r + m[4] * g + m[5] * b;
    const float z = m[6] * r + m[7] * g + m[8] * b;
    r = x; g = y; b = z;
}

bool rgb_to_xyz_matrix(const float c[8], Mat3& out) {
    float Xr, Yr, Zr, Xg, Yg, Zg, Xb, Yb, Zb, Xw, Yw, Zw;
    xy_to_xyz(c[0], c[1], Xr, Yr, Zr);
    xy_to_xyz(c[2], c[3], Xg, Yg, Zg);
    xy_to_xyz(c[4], c[5], Xb, Yb, Zb);
    xy_to_xyz(c[6], c[7], Xw, Yw, Zw);

    // Solve for the per-primary scale factors that map (1,1,1) to the white point.
    const Mat3 primaries = {Xr, Xg, Xb, Yr, Yg, Yb, Zr, Zg, Zb};
    Mat3 inv{};
    if (!mat3_invert(primaries, inv)) return false;

    float sr = Xw, sg = Yw, sb = Zw;
    mat3_apply(inv, sr, sg, sb);

    out = {
        Xr * sr, Xg * sg, Xb * sb,
        Yr * sr, Yg * sg, Yb * sb,
        Zr * sr, Zg * sg, Zb * sb,
    };
    return true;
}

Mat3 bradford_adaptation(float src_x, float src_y, float dst_x, float dst_y) {
    float sX, sY, sZ, dX, dY, dZ;
    xy_to_xyz(src_x, src_y, sX, sY, sZ);
    xy_to_xyz(dst_x, dst_y, dX, dY, dZ);

    float sr = sX, sg = sY, sb = sZ;
    mat3_apply(kBradford, sr, sg, sb);
    float dr = dX, dg = dY, db = dZ;
    mat3_apply(kBradford, dr, dg, db);

    const Mat3 scale = {
        (sr != 0.0f) ? dr / sr : 1.0f, 0.0f, 0.0f,
        0.0f, (sg != 0.0f) ? dg / sg : 1.0f, 0.0f,
        0.0f, 0.0f, (sb != 0.0f) ? db / sb : 1.0f,
    };
    Mat3 inv_bradford{};
    if (!mat3_invert(kBradford, inv_bradford)) return {1, 0, 0, 0, 1, 0, 0, 0, 1};
    return mat3_multiply(inv_bradford, mat3_multiply(scale, kBradford));
}

bool rgb_to_ap1_matrix(const float c[8], Mat3& out) {
    Mat3 src_to_xyz{}, ap1_to_xyz{}, xyz_to_ap1{};
    if (!rgb_to_xyz_matrix(c, src_to_xyz)) return false;
    if (!rgb_to_xyz_matrix(kAP1Chromaticities, ap1_to_xyz)) return false;
    if (!mat3_invert(ap1_to_xyz, xyz_to_ap1)) return false;

    // Adapt the source white to the ACES white before converting into AP1.
    const Mat3 adapt = bradford_adaptation(c[6], c[7],
                                           kAP1Chromaticities[6], kAP1Chromaticities[7]);
    out = mat3_multiply(xyz_to_ap1, mat3_multiply(adapt, src_to_xyz));
    return true;
}

}  // namespace exrcore

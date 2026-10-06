// Colour maths. Plan §6.5 steps 1 and 2 only.
//
// Step 3 -- the ACES 2.0 output transform -- is Phase 2 and is deliberately
// absent. CLAUDE.md forbids silently substituting an approximation, so there is
// no stand-in here. What lives in this file is exact, closed-form maths:
// primaries to XYZ, Bradford chromatic adaptation, and the concatenation into
// a single 3x3 source-to-AP1 matrix.
#pragma once

#include <array>

namespace exrcore {

using Mat3 = std::array<float, 9>;  // row-major

// ACEScg (AP1) primaries and the ACES white point.
extern const float kAP1Chromaticities[8];

// Builds the 3x3 that converts scene-linear RGB in the given primaries to
// scene-linear AP1, applying Bradford adaptation when the white points differ
// (D60 vs D65 matters here). Returns false if the primaries are degenerate.
[[nodiscard]] bool rgb_to_ap1_matrix(const float chromaticities[8], Mat3& out);

// RGB -> XYZ for a set of primaries. Exposed for testing.
[[nodiscard]] bool rgb_to_xyz_matrix(const float chromaticities[8], Mat3& out);

// Bradford adaptation from one white point (x, y) to another.
[[nodiscard]] Mat3 bradford_adaptation(float src_x, float src_y, float dst_x, float dst_y);

Mat3 mat3_multiply(const Mat3& a, const Mat3& b);
[[nodiscard]] bool mat3_invert(const Mat3& m, Mat3& out);
void mat3_apply(const Mat3& m, float& r, float& g, float& b);

}  // namespace exrcore

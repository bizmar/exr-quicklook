// Colour matrix tests. These are exact closed-form transforms, so they are
// checked against published reference values, not against our own output.
#include "EXRCore/exr_color.h"

#include <cmath>
#include <cstdio>

using namespace exrcore;

static int g_failures = 0;
static int g_checks = 0;

static void check_close(float a, float b, float tol, const char* what, int line) {
    ++g_checks;
    if (std::fabs(a - b) > tol) {
        std::printf("  FAIL  line %d  %s: got %.5f expected %.5f (tol %.5f)\n",
                    line, what, a, b, tol);
        ++g_failures;
    }
}
#define CLOSE(a, b, tol, what) check_close((a), (b), (tol), (what), __LINE__)

static const float kRec709[8] = {0.64f, 0.33f, 0.30f, 0.60f,
                                 0.15f, 0.06f, 0.3127f, 0.3290f};

static void test_identity() {
    // AP1 -> AP1 must be the identity, white point included.
    Mat3 m{};
    if (!rgb_to_ap1_matrix(kAP1Chromaticities, m)) { std::printf("  FAIL build\n"); ++g_failures; return; }
    const float expect[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < 9; ++i) CLOSE(m[i], expect[i], 0.0005f, "AP1 identity");
}

static void test_rec709_to_ap1() {
    // Published ACES Rec.709(D65) -> AP1(D60, Bradford) matrix.
    Mat3 m{};
    if (!rgb_to_ap1_matrix(kRec709, m)) { std::printf("  FAIL build\n"); ++g_failures; return; }
    const float expect[9] = {
        0.6131f, 0.3395f, 0.0474f,
        0.0701f, 0.9164f, 0.0135f,
        0.0206f, 0.1096f, 0.8698f,
    };
    for (int i = 0; i < 9; ++i) CLOSE(m[i], expect[i], 0.002f, "Rec709->AP1");
}

static void test_white_maps_to_white() {
    // Whatever the source primaries, RGB(1,1,1) must land on AP1 (1,1,1):
    // that is the entire point of adapting the white point.
    const float spaces[][8] = {
        {0.64f, 0.33f, 0.30f, 0.60f, 0.15f, 0.06f, 0.3127f, 0.3290f},        // Rec.709
        {0.7347f, 0.2653f, 0.0f, 1.0f, 0.0001f, -0.077f, 0.32168f, 0.33767f},// AP0
        {0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f, 0.3127f, 0.3290f},  // P3-D65
        {0.708f, 0.292f, 0.170f, 0.797f, 0.131f, 0.046f, 0.3127f, 0.3290f},  // Rec.2020
    };
    for (const auto& c : spaces) {
        Mat3 m{};
        if (!rgb_to_ap1_matrix(c, m)) { std::printf("  FAIL build\n"); ++g_failures; continue; }
        float r = 1.0f, g = 1.0f, b = 1.0f;
        mat3_apply(m, r, g, b);
        CLOSE(r, 1.0f, 0.002f, "white->AP1 R");
        CLOSE(g, 1.0f, 0.002f, "white->AP1 G");
        CLOSE(b, 1.0f, 0.002f, "white->AP1 B");
    }
}

static void test_invert_roundtrip() {
    Mat3 m{}, inv{}, product{};
    if (!rgb_to_xyz_matrix(kRec709, m)) { ++g_failures; return; }
    if (!mat3_invert(m, inv)) { ++g_failures; return; }
    product = mat3_multiply(m, inv);
    const float expect[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < 9; ++i) CLOSE(product[i], expect[i], 0.0005f, "M * inv(M)");
}

static void test_degenerate_rejected() {
    // Collinear primaries have no inverse and must be refused, not fudged.
    const float bad[8] = {0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3127f, 0.3290f};
    Mat3 m{};
    ++g_checks;
    if (rgb_to_ap1_matrix(bad, m)) {
        std::printf("  FAIL  degenerate primaries accepted\n");
        ++g_failures;
    }
}

int main() {
    test_identity();
    test_rec709_to_ap1();
    test_white_maps_to_white();
    test_invert_roundtrip();
    test_degenerate_rejected();
    std::printf("  colour: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

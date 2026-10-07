// PQ (SMPTE ST 2084) HDR masters, chosen in the input-colourspace picker.
//
// A PQ EXR is display-referred: tone-mapped already, brightness in absolute
// nits. The input entries undo the ACES 2.0 HDR output transform (baked from
// OCIO's inverse) to recover scene-linear ACEScg, which then goes through the
// normal SDR view. Expected values below were computed by OCIO 2.5.1 doing the
// whole chain exactly: PQ -> inverse "ACES 2.0 - HDR 1000 nits (P3 D65)" on
// "ST2084-P3-D65 - Display" -> forward SDR 100 nits P3. The baked path must
// land within 2/255 of them. Every assertion compares rendered pixels.
#include "EXRCore/exr_api.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
static void expect(bool ok, const char* what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

struct Px { float r = -1, g = -1, b = -1; };
// Centre of band `band` (0..3) of a 4-band image, as display code values.
static std::vector<Px> bands(const std::string& path, const char* input, float stops = 0,
                             const char* view = nullptr) {
    std::vector<Px> out(4);
    EXRRenderOptions o{};
    o.input_colorspace = input;
    o.exposure_stops = stops;
    o.view = view;
    EXRRenderResult r{};
    if (!exr_render(path.c_str(), &o, &r)) return {};
    for (int b = 0; b < 4; ++b) {
        const int x = r.width * (2 * b + 1) / 8, y = r.height / 2;
        const uint16_t* p = r.pixels + (size_t(y) * r.width + x) * 4;
        out[b] = {p[0] / 65535.0f, p[1] / 65535.0f, p[2] / 65535.0f};
    }
    exr_render_free(&r);
    return out;
}

static bool near(float a, float b, float tol = 2.0f / 255.0f) { return std::fabs(a - b) <= tol; }

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "Tests/Fixtures/corpus";
    const std::string pq = dir + "/pq-bands.exr";

    bool listed[3] = {};
    const char* ids[3] = {"pq_p3d65_1000", "pq_p3d65_4000", "pq_rec2100_1000"};
    for (int i = 0; i < exr_colorspace_count(); ++i) {
        for (int k = 0; k < 3; ++k) {
            if (std::strcmp(exr_colorspace_id(i), ids[k]) == 0) listed[k] = true;
        }
    }
    expect(listed[0] && listed[1] && listed[2], "the three PQ entries are in the picker");

    const auto as_scene = bands(pq, nullptr);
    const auto p1000 = bands(pq, "pq_p3d65_1000");
    expect(!as_scene.empty() && !p1000.empty(), "renders with and without the PQ input");
    if (p1000.empty()) { std::printf("\n  pq: %d failures\n", failures); return 1; }

    expect(as_scene[2].r != p1000[2].r, "choosing PQ changes the picture");
    expect(p1000[0].r < 1.0f / 255.0f, "PQ black stays black");
    expect(near(p1000[1].r, 0.2150f), "PQ 0.25 matches OCIO's full chain (0.2150)");
    expect(near(p1000[2].r, 0.6949f), "PQ 100 nits matches OCIO's full chain (0.6949)");
    expect(near(p1000[3].r, 1.0f), "PQ 0.75 (~1000 nits) reaches SDR white, as in OCIO");
    expect(p1000[2].r == p1000[2].g && p1000[2].g == p1000[2].b, "grey stays grey");

    const auto p4000 = bands(pq, "pq_p3d65_4000"), r2100 = bands(pq, "pq_rec2100_1000");
    expect(!p4000.empty() && p4000[2].r != p1000[2].r, "the 4000-nit entry differs from 1000");
    expect(!r2100.empty() && near(r2100[2].r, p1000[2].r, 6.0f / 255.0f),
           "Rec.2100 PQ grey lands near P3 PQ grey (same curve, same white)");

    // Exposure is applied in scene-linear, after undoing PQ.
    const auto up = bands(pq, "pq_p3d65_1000", 1.0f);
    expect(!up.empty() && up[1].r > p1000[1].r + 0.02f, "exposure brightens after the inverse");

    // Raw shows file values: the PQ input does not apply to it.
    const auto raw = bands(pq, "pq_p3d65_1000", 0, "raw");
    expect(!raw.empty() && near(raw[2].r, 0.5081f, 1.0f / 255.0f), "Raw shows the PQ code values themselves");

    std::printf("\n  pq: %d failures\n", failures);
    return failures ? 1 : 0;
}

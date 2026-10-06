// Data passes -- depth, position, motion -- must reach the screen untransformed.
//
// The ACES curve is built for scene-referred colour. Applied to a position or
// depth pass it compresses the values into something unreadable, so a data
// pass renders raw unless the user explicitly picks a view (plan §8: "Raw is
// the only sane way to inspect a data pass"). Every assertion compares bytes.
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

struct Rendered {
    std::vector<uint16_t> px;
    int w = 0, h = 0;
    bool ok() const { return !px.empty(); }
    uint16_t at(int x, int y, int c) const { return px[(size_t(y) * w + x) * 4 + c]; }
};

static Rendered render(const char* path, const char* layer, const char* view,
                       float exposure = 0.0f) {
    Rendered out;
    EXRSource* src = exr_open(path, 0, 0, layer);
    if (!src) return out;
    EXRRenderOptions o{};
    o.view = view;
    o.exposure_stops = exposure;
    EXRRenderResult r{};
    if (exr_source_render(src, &o, &r)) {
        out.w = r.width;
        out.h = r.height;
        out.px.assign(r.pixels, r.pixels + size_t(r.width) * r.height * 4);
        exr_render_free(&r);
    }
    exr_close(src);
    return out;
}

static uint16_t code(float v) {
    const float c = std::fmin(std::fmax(v, 0.0f), 1.0f);
    return static_cast<uint16_t>(c * 65535.0f + 0.5f);
}

static bool near(uint16_t a, uint16_t b) { return (a > b ? a - b : b - a) <= 40; }

// Finds a layer id by its label, so the test does not hard-code id formats.
static std::string layer_id(const char* path, const char* label, bool* is_data = nullptr) {
    std::string id;
    if (EXRSource* src = exr_open(path, 64, 0, nullptr)) {
        for (int i = 0; i < exr_source_layer_count(src); ++i) {
            if (std::strcmp(exr_source_layer_label(src, i), label) == 0) {
                id = exr_source_layer_id(src, i);
                if (is_data) *is_data = exr_source_layer_is_data(src, i) == 1;
            }
        }
        exr_close(src);
    }
    return id;
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "Tests/Fixtures/corpus";
    const std::string passes = dir + "/data-passes.exr";
    const std::string depth_only = dir + "/depth-only.exr";
    const std::string p_only = dir + "/position-only.exr";

    // The raw view is offered in the picker alongside the baked transforms.
    bool listed = false;
    for (int i = 0; i < exr_view_count(); ++i) {
        if (std::strcmp(exr_view_id(i), "raw") == 0) listed = true;
    }
    expect(listed, "a raw view is listed");

    bool p_is_data = false, depth_is_data = false, mv_is_data = false;
    const std::string p_id = layer_id(passes.c_str(), "P", &p_is_data);
    const std::string depth_id = layer_id(passes.c_str(), "depth", &depth_is_data);
    const std::string mv_id = layer_id(passes.c_str(), "motion", &mv_is_data);
    expect(!p_id.empty() && !depth_id.empty() && !mv_id.empty(),
           "position, depth and motion are all listed");
    expect(p_is_data && depth_is_data && mv_is_data, "and all three are flagged as data");

    // The beauty still goes through ACES by default.
    const Rendered beauty = render(passes.c_str(), nullptr, nullptr);
    const Rendered beauty_raw = render(passes.c_str(), nullptr, "raw");
    expect(beauty.ok() && beauty_raw.ok() && beauty.px != beauty_raw.px,
           "imagery defaults to the ACES view, not raw");

    // Position: P = (x/(w-1), y/(h-1), 0.5), written by make-fixtures.
    const Rendered p = render(passes.c_str(), p_id.c_str(), nullptr);
    expect(p.ok(), "position renders");
    if (p.ok()) {
        const int x = p.w * 3 / 4, y = p.h / 4;
        const bool exact = near(p.at(x, y, 0), code(float(x) / float(p.w - 1))) &&
                           near(p.at(x, y, 1), code(float(y) / float(p.h - 1))) &&
                           near(p.at(x, y, 2), code(0.5f));
        expect(exact, "position defaults to raw: x->red, y->green, z->blue, untouched");
    }
    const Rendered p_aces = render(passes.c_str(), p_id.c_str(), "aces2_p3d65_sdr100");
    expect(p_aces.ok() && p_aces.px != p.px, "an explicit view still applies to a data pass");

    // Depth: constant 0.25 -> one grey code value, untransformed.
    const Rendered z = render(passes.c_str(), depth_id.c_str(), nullptr);
    expect(z.ok() && near(z.at(5, 5, 0), code(0.25f)) && z.at(5, 5, 0) == z.at(5, 5, 2),
           "depth defaults to raw greyscale");
    // Exposure still works on raw, which is how depth beyond 1.0 is read.
    const Rendered z_down = render(passes.c_str(), depth_id.c_str(), nullptr, -1.0f);
    expect(z_down.ok() && near(z_down.at(5, 5, 0), code(0.125f)), "exposure applies to raw");

    // Motion: two components, (u, v) = (0.75, 0.1); blue must stay zero.
    const Rendered mv = render(passes.c_str(), mv_id.c_str(), nullptr);
    expect(mv.ok() && near(mv.at(5, 5, 0), code(0.75f)) && near(mv.at(5, 5, 1), code(0.1f)) &&
               mv.at(5, 5, 2) == 0,
           "motion u/v map to red/green, blue zero");

    // Files holding nothing but a data pass show it, raw, by default -- and the
    // one-shot path (thumbnails) agrees with the preview path, per D7.
    const Rendered d = render(depth_only.c_str(), nullptr, nullptr);
    expect(d.ok() && near(d.at(5, 5, 0), code(0.25f)), "a depth-only file renders raw");
    const Rendered po = render(p_only.c_str(), nullptr, nullptr);
    expect(po.ok(), "a position-only file renders");

    // A greyscale layer names one channel three times. It must come out grey
    // through the default view too -- it once came out blue, because only the
    // last of the three identically named slices was ever filled.
    const Rendered y = render((dir + "/single-channel.exr").c_str(), nullptr, nullptr);
    expect(y.ok() && y.at(200, 5, 0) == y.at(200, 5, 1) && y.at(200, 5, 1) == y.at(200, 5, 2) &&
               y.at(200, 5, 0) > 0,
           "a single-channel file renders grey, not blue");

    EXRRenderOptions o{};
    EXRRenderResult r{};
    const bool thumb = exr_render(depth_only.c_str(), &o, &r) == 1;
    expect(thumb && d.ok() && r.width == d.w &&
               std::memcmp(r.pixels, d.px.data(), d.px.size() * sizeof(uint16_t)) == 0,
           "thumbnail path matches preview path for a data-only file (D7)");
    exr_render_free(&r);

    std::printf("\n  raw view: %d failures\n", failures);
    return failures ? 1 : 0;
}

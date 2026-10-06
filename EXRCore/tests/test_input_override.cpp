// The input-colourspace override must actually change the render.
//
// This test exists because it did not: the transform applied the override only
// to files *without* chromaticities, so on a tagged file the picker moved, the
// badge lit, the setting carried to the next frame -- and the pixels stayed the
// same. Every assertion here compares rendered bytes, not settings.
#include "EXRCore/exr_api.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;

static std::vector<uint16_t> render(const char* path, const char* override_id,
                                    const char* assumed_id) {
    EXRRenderOptions o{};
    o.max_edge = 128;
    o.input_colorspace = override_id;
    o.assumed_colorspace = assumed_id;
    EXRRenderResult r{};
    if (!exr_render(path, &o, &r)) return {};
    std::vector<uint16_t> px(r.pixels, r.pixels + size_t(r.width) * r.height * 4);
    exr_render_free(&r);
    return px;
}

static void expect(bool ok, const char* what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "Tests/Fixtures/corpus";
    char tagged[512], untagged[512];
    std::snprintf(tagged, sizeof tagged, "%s/aces2065-1.exr", dir);          // states AP0
    std::snprintf(untagged, sizeof untagged, "%s/no-chromaticities.exr", dir);

    const auto t_none = render(tagged, nullptr, nullptr);
    const auto u_none = render(untagged, nullptr, nullptr);
    expect(!t_none.empty() && !u_none.empty(), "both fixtures render");

    expect(render(tagged, "acescg", nullptr) != t_none,
           "override changes a file that states its own primaries");
    expect(render(tagged, "aces2065_1", nullptr) == t_none,
           "overriding with the file's own space is a no-op");
    expect(render(untagged, "linear_rec_709_srgb", nullptr) != u_none,
           "override changes an untagged file");
    expect(render(untagged, nullptr, "acescg") == u_none,
           "untagged file defaults to ACEScg");
    expect(render(untagged, nullptr, "aces2065_1") != u_none,
           "assumed colourspace applies to an untagged file");
    expect(render(tagged, nullptr, "linear_rec_709_srgb") == t_none,
           "assumed colourspace never touches a tagged file");
    expect(render(untagged, "linear_rec_709_srgb", "aces2065_1") ==
           render(untagged, "linear_rec_709_srgb", nullptr),
           "an explicit override beats the assumed default");

    std::printf("\n  input override: %d failures\n", failures);
    return failures ? 1 : 0;
}

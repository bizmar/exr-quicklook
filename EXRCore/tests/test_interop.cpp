// colorInteropID: the OpenEXR 3.4 standard attribute naming a file's colour
// space with a Color Interop Forum ID ("lin_ap0_scene").
//
// Precedence, D9 as amended 2026-10-06: an explicit override, then the file's
// chromaticities, then a recognised scene-linear colorInteropID, then the
// assumed default. Every assertion compares rendered bytes against a file that
// states the same thing the other way, not against settings.
#include "EXRCore/exr_api.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;

static void expect(bool ok, const char* what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

static std::vector<uint16_t> render(const std::string& path, const char* override_id = nullptr,
                                    const char* assumed_id = nullptr, const char* view = nullptr) {
    EXRRenderOptions o{};
    o.max_edge = 128;
    o.input_colorspace = override_id;
    o.assumed_colorspace = assumed_id;
    o.view = view;
    EXRRenderResult r{};
    if (!exr_render(path.c_str(), &o, &r)) return {};
    std::vector<uint16_t> px(r.pixels, r.pixels + size_t(r.width) * r.height * 4);
    exr_render_free(&r);
    return px;
}

static std::string stated_name(const std::string& path) {
    std::string name;
    if (EXRSource* s = exr_open(path.c_str(), 64, 0, nullptr)) {
        name = exr_source_has_chromaticities(s) ? exr_source_chromaticities_name(s) : "";
        exr_close(s);
    }
    return name;
}

static std::string describe(const std::string& path) {
    char buf[8192] = {};
    exr_describe(path.c_str(), buf, sizeof buf);
    return buf;
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "Tests/Fixtures/corpus";
    const std::string ap0_chroma = dir + "/aces2065-1.exr";         // chromaticities AP0
    const std::string untagged = dir + "/no-chromaticities.exr";
    const std::string ap0_id = dir + "/interop-ap0.exr";             // ID only
    const std::string conflict = dir + "/interop-conflict.exr";      // AP0 chroma, AP1 ID
    const std::string awg3_id = dir + "/interop-awg3.exr";           // namespaced "ocio:" ID
    const std::string log_id = dir + "/interop-log.exr";             // ACEScct: not linear
    const std::string data_id = dir + "/interop-data.exr";           // "data"

    const auto ref_ap0 = render(ap0_chroma);
    const auto ref_untagged = render(untagged);
    expect(!ref_ap0.empty() && !ref_untagged.empty() && ref_ap0 != ref_untagged,
           "reference renders differ (AP0 vs assumed ACEScg)");

    expect(render(ap0_id) == ref_ap0,
           "an ID alone is honoured: lin_ap0_scene renders as AP0 chromaticities do");
    expect(stated_name(ap0_id) == "ACES2065-1 (AP0)",
           "and it counts as the file stating its space, named for the picker");
    expect(render(ap0_id, nullptr, "linear_rec_709_srgb") == ref_ap0,
           "the assumed default never touches a file with a recognised ID");
    expect(render(ap0_id, "acescg") == ref_untagged,
           "an explicit override still beats the ID");

    expect(render(conflict) == ref_ap0, "chromaticities beat a disagreeing ID");

    expect(render(awg3_id) == render(untagged, "linear_arri_wide_gamut_3"),
           "an OCIO-namespaced ID maps to its space (ocio:lin_awg3_scene)");

    expect(render(log_id) == ref_untagged,
           "a non-linear ID is not applied: falls back to the assumed default");
    expect(stated_name(log_id).empty(), "and the file is not treated as stating a space");

    expect(render(data_id) == render(untagged, nullptr, nullptr, "raw"),
           "an ID of \"data\" renders raw by default");
    expect(render(data_id, nullptr, nullptr, "aces2_p3d65_sdr100") == ref_untagged,
           "and an explicit view still applies to it");

    // arnold/color_space: an OCIO colour-space name, matched against each
    // space's name and OCIO aliases. Ranks after colorInteropID.
    const std::string a_ap0 = dir + "/arnold-ap0.exr", a_alias = dir + "/arnold-alias.exr";
    const std::string a_disp = dir + "/arnold-display.exr", a_vs = dir + "/arnold-vs-interop.exr";
    expect(render(a_ap0) == ref_ap0, "arnold/color_space \"ACES2065-1\" renders as AP0");
    expect(stated_name(a_ap0) == "ACES2065-1 (AP0)", "and counts as the file stating its space");
    expect(render(a_alias) == render(untagged, "linear_rec_709_srgb"),
           "an OCIO alias (\"Utility - Linear - sRGB\") maps to its space");
    expect(render(a_disp) == ref_untagged && stated_name(a_disp).empty(),
           "a display space name is ignored: falls back to the assumed default");
    expect(render(a_vs) == ref_untagged, "colorInteropID outranks arnold/color_space");
    expect(render(dir + "/arnold-linear.exr") == render(untagged, "linear_rec_709_srgb"),
           "Arnold's built-in \"linear\" is linear sRGB / Rec.709");
    expect(render(a_ap0, "acescg") == ref_untagged, "an explicit override still wins");
    const std::string d_arnold = describe(a_ap0), d_disp = describe(a_disp);
    expect(d_arnold.find("arnold/color_space") != std::string::npos,
           "the info panel names the attribute the space came from");
    expect(d_disp.find("sRGB - Display") != std::string::npos && d_disp.find("ignored") != std::string::npos,
           "and says when it was ignored");

    const std::string d_ap0 = describe(ap0_id), d_log = describe(log_id);
    expect(d_ap0.find("lin_ap0_scene") != std::string::npos,
           "the info panel shows the ID");
    expect(d_log.find("ocio:acescct_ap1_scene") != std::string::npos &&
               d_log.find("ignored") != std::string::npos,
           "and says when it was ignored");

    std::printf("\n  colour tags: %d failures\n", failures);
    return failures ? 1 : 0;
}

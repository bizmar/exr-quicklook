// Debug harness. Plan §5: "EXRCore and EXRCLI mean almost everything is
// testable without touching Quick Look at all."
//
// Today it inspects headers and reports the primary layer selection. Pixel
// decode and the colour pipeline land here before they go near an appex.
#include "EXRCore/exr_color.h"
#include "EXRCore/exr_lut.h"
#include "EXRCore/exr_decode.h"
#include "EXRCore/exr_layers.h"
#include "EXRCore/exr_reader.h"
#include "png_writer.h"

#include <cmath>

#include <cstdio>
#include <string>
#include <vector>

using namespace exrcore;

static const char* kind_name(LayerKind k) {
    switch (k) {
        case LayerKind::kRGB:        return "RGB";
        case LayerKind::kLumaChroma: return "luminance-chroma";
        case LayerKind::kGrey:       return "greyscale";
        case LayerKind::kNone:       return "none";
    }
    return "?";
}

static const char* type_name(PixelType t) {
    switch (t) {
        case PixelType::kUInt:  return "uint";
        case PixelType::kHalf:  return "half";
        case PixelType::kFloat: return "float";
    }
    return "?";
}

static void print_box(const char* label, const Box2i& b) {
    int64_t w = 0, h = 0;
    const bool ok = b.extent(w, h);
    std::printf("    %-14s (%d, %d) .. (%d, %d)   %lldx%lld%s\n", label,
                b.min_x, b.min_y, b.max_x, b.max_y,
                static_cast<long long>(ok ? w : 0), static_cast<long long>(ok ? h : 0),
                ok ? "" : "  [INVALID]");
}

// Fallback transfer, used only by --raw. The shipping path is the baked ACES
// 2.0 output transform in EXRCore/exr_lut.h.
static float srgb_encode(float v) {
    if (v <= 0.0f) return 0.0f;
    if (v >= 1.0f) return 1.0f;
    return v <= 0.0031308f ? v * 12.92f
                           : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

struct RenderOptions {
    std::string png_path;
    bool to_ap1 = true;      // apply the source-primaries -> AP1 matrix
    bool srgb = true;        // --raw fallback transfer when no LUT is used
    std::string view = kDefaultLutName;   // baked ACES 2.0 output transform
    float exposure = 0.0f;   // stops, applied linearly before anything else
    int max_edge = 0;        // longest-edge cap; 0 = full resolution
};

static int render_png(const std::string& path, const FileInfo& info,
                      const LayerSelection& sel, const RenderOptions& opt) {
    Image img;
    std::string error;
    DecodeOptions dopt;
    dopt.max_edge = opt.max_edge;
    if (!decode_layer(path, info, sel, img, error, dopt)) {
        std::printf("    DECODE FAILED: %s\n", error.c_str());
        return 1;
    }
    std::printf("    decoded       %dx%d (cropped to displayWindow%s)\n",
                img.width, img.height, opt.max_edge ? ", downsampled" : "");

    Mat3 m{1, 0, 0, 0, 1, 0, 0, 0, 1};
    const PartDetail& d = info.details[sel.part_index];
    if (opt.to_ap1) {
        const float* chroma = d.has_chromaticities ? d.chromaticities : kAP1Chromaticities;
        if (!rgb_to_ap1_matrix(chroma, m)) {
            std::printf("    colour matrix could not be built; leaving primaries alone\n");
            m = Mat3{1, 0, 0, 0, 1, 0, 0, 0, 1};
        } else {
            std::printf("    primaries     %s -> AP1\n",
                        d.has_chromaticities
                            ? (d.chromaticities_name.empty() ? "(custom)"
                                                             : d.chromaticities_name.c_str())
                            : "assumed ACEScg (D9 fallback)");
        }
    }

    const BakedLut* lut = opt.to_ap1 ? find_lut(opt.view) : nullptr;
    if (opt.to_ap1 && !lut) {
        std::printf("    NO SUCH VIEW: %s\n", opt.view.c_str());
        return 1;
    }
    if (lut) std::printf("    view          %s\n", lut->view);

    const float gain = std::pow(2.0f, opt.exposure);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(img.width) * img.height * 4);
    for (std::size_t i = 0, n = static_cast<std::size_t>(img.width) * img.height; i < n; ++i) {
        float r = img.rgba[i * 4 + 0] * gain;
        float g = img.rgba[i * 4 + 1] * gain;
        float b = img.rgba[i * 4 + 2] * gain;
        if (opt.to_ap1) mat3_apply(m, r, g, b);
        if (lut) {
            apply_lut(*lut, r, g, b);
        } else if (opt.srgb) {
            r = srgb_encode(r); g = srgb_encode(g); b = srgb_encode(b);
        } else {
            r = std::fmin(std::fmax(r, 0.0f), 1.0f);
            g = std::fmin(std::fmax(g, 0.0f), 1.0f);
            b = std::fmin(std::fmax(b, 0.0f), 1.0f);
        }
        out[i * 4 + 0] = static_cast<std::uint8_t>(r * 255.0f + 0.5f);
        out[i * 4 + 1] = static_cast<std::uint8_t>(g * 255.0f + 0.5f);
        out[i * 4 + 2] = static_cast<std::uint8_t>(b * 255.0f + 0.5f);
        out[i * 4 + 3] = 255;  // alpha ignored by the default display path (§6.3)
    }

    if (!exrcli::write_png(opt.png_path, out.data(), img.width, img.height, error)) {
        std::printf("    PNG FAILED: %s\n", error.c_str());
        return 1;
    }
    std::printf("    wrote         %s\n", opt.png_path.c_str());
    if (!lut) {
        std::printf("    NOTE: --raw, no output transform applied.\n");
    }
    return 0;
}

static int inspect_one(const std::string& path, bool verbose,
                       const RenderOptions* render) {
    FileInfo info;
    std::string error;
    std::printf("\n\033[1m%s\033[0m\n", path.c_str());
    if (!inspect_file(path, info, error)) {
        std::printf("    REJECTED: %s\n", error.c_str());
        return 1;
    }

    std::printf("    parts          %zu\n", info.parts.size());
    for (std::size_t i = 0; i < info.parts.size(); ++i) {
        const PartInfo& p = info.parts[i];
        const PartDetail& d = info.details[i];
        std::printf("\n    [part %zu] %s%s\n", i,
                    p.name.empty() ? "(unnamed)" : p.name.c_str(),
                    p.is_deep() ? "   *** DEEP ***" : "");
        std::printf("    %-14s %s\n", "type", d.type_name.c_str());
        std::printf("    %-14s %s\n", "compression", d.compression.c_str());
        print_box("dataWindow", d.data_window);
        print_box("displayWindow", d.display_window);
        if (d.data_window.min_x != d.display_window.min_x ||
            d.data_window.max_x != d.display_window.max_x ||
            d.data_window.min_y != d.display_window.min_y ||
            d.data_window.max_y != d.display_window.max_y) {
            std::printf("    %-14s yes  (crop to displayWindow -- decision D6)\n", "overscan");
        }
        std::printf("    %-14s %.4f\n", "pixelAspect", d.pixel_aspect_ratio);
        if (d.has_preview) std::printf("    %-14s yes\n", "preview attr");

        if (d.has_chromaticities) {
            std::printf("    %-14s %s\n", "chromaticities",
                        d.chromaticities_name.empty() ? "(custom)"
                                                      : d.chromaticities_name.c_str());
            if (verbose) {
                std::printf("    %-14s R(%.4f %.4f) G(%.4f %.4f) B(%.4f %.4f) W(%.5f %.5f)\n", "",
                            d.chromaticities[0], d.chromaticities[1], d.chromaticities[2],
                            d.chromaticities[3], d.chromaticities[4], d.chromaticities[5],
                            d.chromaticities[6], d.chromaticities[7]);
            }
        } else {
            std::printf("    %-14s ABSENT  (falls back to the ACEScg preference -- D9)\n",
                        "chromaticities");
        }

        if (!p.color_interop_id.empty()) {
            std::printf("    %-14s %s\n", "colorInteropID", p.color_interop_id.c_str());
        }
        std::printf("    %-14s %zu\n", "channels", p.channels.size());
        if (verbose) {
            for (const ChannelInfo& c : p.channels) {
                std::printf("        %-28s %-6s", c.name.c_str(), type_name(c.type));
                if (c.x_sampling != 1 || c.y_sampling != 1) {
                    std::printf("  sampling %d/%d", c.x_sampling, c.y_sampling);
                }
                std::printf("\n");
            }
        }
    }

    const LayerSelection sel = select_primary_layer(info.parts);
    std::printf("\n    \033[1mprimary layer\033[0m\n");
    if (!sel.valid()) {
        std::printf("        none selectable (nothing but data/deep layers)\n");
        return 0;
    }
    std::printf("        part       %d\n", sel.part_index);
    std::printf("        layer      %s\n",
                sel.layer_name.empty() ? "(default, unprefixed)" : sel.layer_name.c_str());
    std::printf("        kind       %s\n", kind_name(sel.kind));
    std::printf("        channels   R=%s G=%s B=%s\n", sel.r.c_str(), sel.g.c_str(),
                sel.b.c_str());
    std::printf("        alpha      %s\n",
                sel.has_alpha() ? sel.a.c_str() : "none (ignored by default -- §6.3)");

    // What the preview's layer switcher will offer. Data passes render raw.
    std::printf("\n    \033[1mlayer switcher\033[0m\n");
    for (const LayerOption& o : enumerate_layers(info.parts)) {
        std::printf("        %-28s %s  R=%s G=%s B=%s\n", o.label.c_str(),
                    o.data_pass ? "data " : "image", o.selection.r.c_str(),
                    o.selection.g.c_str(), o.selection.b.empty() ? "0" : o.selection.b.c_str());
    }

    if (render) {
        std::printf("\n    \033[1mrender\033[0m\n");
        return render_png(path, info, sel, *render);
    }
    return 0;
}

int main(int argc, char** argv) {
    bool verbose = false;
    bool want_png = false;
    RenderOptions render;
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-v" || a == "--verbose") verbose = true;
        else if (a == "--png" && i + 1 < argc) { want_png = true; render.png_path = argv[++i]; }
        else if (a == "--raw") { render.to_ap1 = false; render.srgb = false; }
        else if (a == "--exposure" && i + 1 < argc) { render.exposure = std::stof(argv[++i]); }
        else if (a == "--max-edge" && i + 1 < argc) { render.max_edge = std::stoi(argv[++i]); }
        else if (a == "--view" && i + 1 < argc) { render.view = argv[++i]; }
        else if (a == "--list-views") {
            for (int k = 0; k < kBakedLutCount; ++k)
                std::printf("  %-24s %d^3  %s\n", kBakedLuts[k].name,
                            kBakedLuts[k].size, kBakedLuts[k].view);
            return 0;
        }
        else if (a == "-h" || a == "--help") {
            std::printf("usage: exrcli [-v] [--png OUT.png [--raw] [--exposure STOPS]] <file.exr>...\n\n"
                        "  --png       write a debug PNG of the primary layer\n"
                        "  --raw       no primaries conversion, no transfer function\n"
                        "  --exposure  stops, applied linearly (manual only -- D8 forbids auto)\n"
                        "  --max-edge  longest-edge cap; downsamples during decode\n"
                        "  --view      baked output transform (see --list-views)\n");
            return 0;
        } else paths.push_back(a);
    }
    if (paths.empty()) {
        std::fprintf(stderr, "usage: exrcli [-v] <file.exr>...\n");
        return 2;
    }
    int bad = 0;
    for (const std::string& p : paths) bad += inspect_one(p, verbose, want_png ? &render : nullptr);
    std::printf("\n");
    return bad == 0 ? 0 : 1;
}

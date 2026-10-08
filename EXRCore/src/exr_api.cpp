#include "EXRCore/exr_api.h"

#include "EXRCore/exr_color.h"
#include "EXRCore/exr_decode.h"
#include "EXRCore/exr_layers.h"
#include "EXRCore/exr_lut.h"
#include "EXRCore/exr_reader.h"
#include "EXRCore/exr_spaces.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <memory>
#include <thread>
#include <string>
#include <vector>

using namespace exrcore;

// A decoded file plus everything needed to re-transform it without touching
// disk again. Holding scene-linear pixels (not display values) is what makes an
// exposure drag cheap: only the matrix, exposure and LUT are re-applied.
struct EXRSource {
    Image image;                 // scene-linear, file primaries
    FileInfo info;
    LayerSelection selection;
    bool has_chromaticities = false;
    float chromaticities[8] = {};
    std::string chromaticities_name;
    std::vector<LayerOption> layers;
    std::string active_layer;
    std::string auto_layer;
    bool active_is_data = false;  // renders raw unless a view is chosen
};

namespace {

// No output transform: file values straight to display code values. The only
// sane way to look at a data pass (plan §8). Not a LUT, so it has no table.
constexpr const char* kRawViewId = "raw";

std::string printable(const std::string& in);   // file text, made safe to display

// Named input primaries for the D9 override.

const NamedSpace* find_space(const char* id) {
    if (!id) return nullptr;
    // Indexed rather than range-for: kSpaces is declared with unknown bound.
    for (int i = 0; i < kSpaceCount; ++i) {
        if (std::strcmp(kSpaces[i].id, id) == 0) return &kSpaces[i];
    }
    return nullptr;
}

// Rec.709 luma weights, used only for the luminance isolation view.
inline float luminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// Mid-grey checkerboard, in display code values. Only ever used behind alpha in
// the preview; thumbnails never composite (§6.3).
inline float checker(int x, int y) {
    constexpr int kSquare = 8;
    return (((x / kSquare) + (y / kSquare)) & 1) ? 0.60f : 0.40f;
}

inline uint16_t quantise(float v) {
    const float c = std::fmin(std::fmax(v, 0.0f), 1.0f);
    return static_cast<uint16_t>(c * 65535.0f + 0.5f);
}

// The single transform path. Everything -- thumbnail, preview, every override
// -- goes through here, which is how D7 is enforced rather than intended.
bool transform(const Image& img, const float* file_chroma, bool has_chroma, bool is_data,
               const EXRRenderOptions& opt, EXRRenderResult& out) {
    if (img.empty()) return false;

    // An explicit view wins. Otherwise a data pass is shown raw -- the ACES
    // curve would compress position or depth into something unreadable -- and
    // imagery gets the committed default view.
    const char* view = opt.view ? opt.view
                     : is_data  ? kRawViewId
                     : (opt.default_view ? opt.default_view : kDefaultLutName);
    const bool raw = std::strcmp(view, kRawViewId) == 0;
    const BakedLut* lut = raw ? nullptr : find_lut(view);
    if (!raw && !lut) return false;   // never render untransformed by accident

    // Precedence, D9 as amended (see CLAUDE.md):
    //   1. an explicit override, whether or not the file states its space --
    //      a misapplied profile is baked into every frame of a sequence, and
    //      the user has to be able to correct it. It is either named primaries
    //      or, for a PQ HDR master, an input transform (inverse HDR view);
    //   2. what the file states (`has_chroma`): chromaticities, colorInteropID
    //      or arnold/color_space, resolved by stated_space();
    //   3. the assumed default for an untagged file (D9a), ACEScg if unset.
    // An unknown override id falls through rather than failing, so a stale id
    // carried from an older build degrades to the file's own primaries.
    const float* chroma = nullptr;
    const InputLut* input = nullptr;   // PQ code values -> scene-linear ACEScg
    if (const NamedSpace* forced = find_space(opt.input_colorspace)) {
        chroma = forced->chroma;
    } else if ((input = find_input_lut(opt.input_colorspace))) {
        chroma = kAP1Chromaticities;   // the input transform already yields AP1
    } else if (has_chroma) {
        chroma = file_chroma;
    } else if (const NamedSpace* assumed = find_space(opt.assumed_colorspace)) {
        chroma = assumed->chroma;
    } else if ((input = find_input_lut(opt.assumed_colorspace))) {
        chroma = kAP1Chromaticities;
    } else {
        chroma = kAP1Chromaticities;
    }

    Mat3 m{1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (!rgb_to_ap1_matrix(chroma, m)) return false;

    const std::size_t n = static_cast<std::size_t>(img.width) * img.height;
    uint16_t* px = static_cast<uint16_t*>(std::malloc(n * 4 * sizeof(uint16_t)));
    if (!px) return false;

    const float gain = std::pow(2.0f, opt.exposure_stops);
    const auto mode = static_cast<EXRChannelView>(opt.channel_view);
    const bool over_checker = opt.alpha_over_checker && mode != EXR_VIEW_ALPHA;
    const int width = img.width;

    // The transform is per-pixel independent, so it parallelises cleanly. This
    // is what makes dragging the exposure slider feel immediate: the decode is
    // already cached, and this is the only remaining per-event cost.
    // Kept modest -- the appex is sandboxed and memory-capped (§6.4).
    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned workers =
        std::max(1u, std::min<unsigned>(Limits::kDecodeThreads + 1u, hw ? hw : 1u));

    auto run_band = [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            float r = img.rgba[i * 4 + 0];
            float g = img.rgba[i * 4 + 1];
            float b = img.rgba[i * 4 + 2];
            const float a = img.rgba[i * 4 + 3];

            // A PQ master is display-referred: undo the HDR output transform
            // first, so exposure and isolation act in scene-linear like for
            // any other file. Raw shows the file's own code values instead.
            if (input && !raw) apply_input_lut(input->lut, r, g, b);
            r *= gain; g *= gain; b *= gain;

            // Channel isolation happens in scene-linear, before the transform,
            // so an isolated channel is tone-mapped as it would be in situ.
            switch (mode) {
                case EXR_VIEW_RED:       g = r; b = r; break;
                case EXR_VIEW_GREEN:     r = g; b = g; break;
                case EXR_VIEW_BLUE:      r = b; g = b; break;
                case EXR_VIEW_ALPHA:     r = g = b = a; break;
                case EXR_VIEW_LUMINANCE: r = g = b = luminance(r, g, b); break;
                case EXR_VIEW_RGB:
                default: break;
            }

            // Raw skips the primaries matrix too: a data pass has no primaries.
            if (!raw) {
                if (!input) mat3_apply(m, r, g, b);
                apply_lut(*lut, r, g, b);
            }

            if (over_checker) {
                // EXR alpha is associated, so colour is already premultiplied.
                const int x = static_cast<int>(i % static_cast<std::size_t>(width));
                const int y = static_cast<int>(i / static_cast<std::size_t>(width));
                const float bg = checker(x, y) * (1.0f - std::fmin(std::fmax(a, 0.0f), 1.0f));
                r += bg; g += bg; b += bg;
            }

            px[i * 4 + 0] = quantise(r);
            px[i * 4 + 1] = quantise(g);
            px[i * 4 + 2] = quantise(b);
            // Default display ignores alpha entirely (§6.3): show RGB as the
            // comp shows it. That is what structurally prevents the white/black
            // failure users hit with Apple's decoder.
            px[i * 4 + 3] = 65535;
        }
    };

    if (workers <= 1 || n < 65536) {
        run_band(0, n);
    } else {
        std::vector<std::thread> pool;
        pool.reserve(workers - 1);
        const std::size_t chunk = (n + workers - 1) / workers;
        for (unsigned w = 1; w < workers; ++w) {
            const std::size_t b0 = std::min(n, chunk * w);
            const std::size_t b1 = std::min(n, b0 + chunk);
            if (b0 < b1) pool.emplace_back(run_band, b0, b1);
        }
        run_band(0, std::min(n, chunk));
        for (std::thread& t : pool) t.join();
    }

    out.width = img.width;
    out.height = img.height;
    out.pixels = px;
    return true;
}

// What a part states about its primaries (D9, amended 2026-10-06/07): its
// chromaticities, else a recognised scene-linear colorInteropID, else a
// renderer's colour-space attribute naming a known scene-linear space. Shared
// by the renderer and the info panel so the two can never disagree.
// The space a renderer's colour-space attribute names: an OCIO name or alias,
// or Arnold's built-in-manager name "linear" -- its default rendering space
// since Arnold 5, linear sRGB (Rec.709 primaries). That last mapping applies
// to arnold/color_space only; a bare "linear" means nothing in OCIO.
const NamedSpace* writer_space(const PartDetail& d) {
    if (d.writer_colorspace_attr == "arnold/color_space" &&
        strcasecmp(d.writer_colorspace.c_str(), "linear") == 0) {
        return find_space("linear_rec_709_srgb");
    }
    return find_space_by_name(d.writer_colorspace.c_str());
}

struct StatedSpace {
    bool stated = false;
    const float* chroma = nullptr;
    std::string name;
    bool from_interop = false;
    std::string from_attr;   // e.g. "arnold/color_space" when that decided it
};

StatedSpace stated_space(const PartDetail& d, const PartInfo& p) {
    StatedSpace s;
    if (d.has_chromaticities) {
        s.stated = true;
        s.chroma = d.chromaticities;
        s.name = d.chromaticities_name.empty() ? "custom chromaticities" : d.chromaticities_name;
    } else if (const NamedSpace* n = find_space_by_interop(p.color_interop_id.c_str())) {
        s.stated = true;
        s.chroma = n->chroma;
        s.name = n->label;
        s.from_interop = true;
    } else if (const NamedSpace* w = writer_space(d)) {
        s.stated = true;
        s.chroma = w->chroma;
        s.name = w->label;
        s.from_attr = d.writer_colorspace_attr;
    }
    return s;
}

std::string id_for(const std::vector<LayerOption>& layers, const LayerSelection& sel) {
    for (const LayerOption& o : layers) {
        if (o.selection.part_index == sel.part_index &&
            o.selection.layer_name == sel.layer_name && o.selection.r == sel.r) {
            return o.id;
        }
    }
    return {};
}

// Why the last exr_render / exr_open on this thread failed, for the
// extensions' log. Empty after a success.
thread_local std::string t_last_error;

std::unique_ptr<EXRSource> open_impl(const char* path, int32_t max_edge,
                                     bool use_data_window,
                                     const std::string& wanted_layer = {}) {
    t_last_error.clear();
    if (!path) { t_last_error = "no path"; return nullptr; }
    auto src = std::make_unique<EXRSource>();
    std::string error;
    if (!inspect_file(path, src->info, error)) { t_last_error = error; return nullptr; }

    src->layers = enumerate_layers(src->info.parts);
    for (LayerOption& o : src->layers) o.label = printable(o.label);
    src->selection = select_primary_layer(src->info.parts);
    src->auto_layer = id_for(src->layers, src->selection);

    // An explicit choice wins, but only if this file actually has it. Falling
    // back to the automatic layer rather than failing is what lets a selection
    // stay sticky across a sequence whose frames do not all match.
    if (!wanted_layer.empty()) {
        for (const LayerOption& o : src->layers) {
            if (o.id == wanted_layer) { src->selection = o.selection; break; }
        }
    }
    if (!src->selection.valid()) { t_last_error = "no layer to show"; return nullptr; }

    src->active_layer = id_for(src->layers, src->selection);
    for (const LayerOption& o : src->layers) {
        if (o.id == src->active_layer) src->active_is_data = o.data_pass;
    }

    // Keep the menu to a sane length, always including what is shown and what
    // would be shown automatically.
    if (src->layers.size() > static_cast<std::size_t>(Limits::kMaxLayerOptions)) {
        std::vector<LayerOption> kept(src->layers.begin(),
                                      src->layers.begin() + Limits::kMaxLayerOptions);
        for (const std::string& id : {src->active_layer, src->auto_layer}) {
            const auto in = [&](const std::vector<LayerOption>& v) {
                return std::any_of(v.begin(), v.end(),
                                   [&](const LayerOption& o) { return o.id == id; });
            };
            if (in(kept)) continue;
            for (const LayerOption& o : src->layers) {
                if (o.id == id) { kept.push_back(o); break; }
            }
        }
        src->layers = std::move(kept);
    }

    DecodeOptions dopt;
    dopt.max_edge = max_edge;
    dopt.use_data_window = use_data_window;
    if (!decode_layer(path, src->info, src->selection, src->image, error, dopt)) {
        t_last_error = error;
        return nullptr;
    }

    const int pi = src->selection.part_index;
    const StatedSpace stated = stated_space(src->info.details[pi], src->info.parts[pi]);
    src->has_chromaticities = stated.stated;
    src->chromaticities_name = stated.name;
    std::memcpy(src->chromaticities, stated.stated ? stated.chroma : kAP1Chromaticities,
                sizeof(src->chromaticities));
    return src;
}

// File-supplied text made safe to display. The info panel is "label\tvalue"
// lines, so a tab or newline inside a layer name or tag would forge a row of
// its own (a second "Colour" line, say); bidirectional overrides can reorder
// what is shown. Control characters become spaces, direction controls go.
std::string printable(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    const auto* p = reinterpret_cast<const unsigned char*>(in.data());
    const std::size_t n = in.size();
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char c = p[i];
        if (c < 0x20 || c == 0x7F) { out += ' '; continue; }
        // C1 controls, U+0080..U+009F: C2 80..C2 9F.
        if (c == 0xC2 && i + 1 < n && p[i + 1] >= 0x80 && p[i + 1] <= 0x9F) {
            out += ' '; ++i; continue;
        }
        // U+200E/F, U+202A..U+202E, U+2066..U+2069: E2 80 8E/8F, E2 80 AA..AE, E2 81 A6..A9.
        if (c == 0xE2 && i + 2 < n) {
            const unsigned char b = p[i + 1], d = p[i + 2];
            if ((b == 0x80 && (d == 0x8E || d == 0x8F || (d >= 0xAA && d <= 0xAE))) ||
                (b == 0x81 && d >= 0xA6 && d <= 0xA9)) {
                i += 2; continue;
            }
        }
        out += static_cast<char>(c);
    }
    return out;
}

void append(std::string& s, const char* label, const std::string& value) {
    s += label; s += "\t"; s += printable(value); s += "\n";
}

const char* pixel_type_name(PixelType t) {
    switch (t) {
        case PixelType::kUInt:  return "uint32";
        case PixelType::kHalf:  return "half";
        case PixelType::kFloat: return "float32";
    }
    return "?";
}

}  // namespace

int exr_render(const char* path, const EXRRenderOptions* options, EXRRenderResult* out) {
    if (!out) return 0;
    out->width = out->height = 0;
    out->pixels = nullptr;
    EXRRenderOptions defaults{};
    const EXRRenderOptions& opt = options ? *options : defaults;

    auto src = open_impl(path, opt.max_edge, opt.use_data_window != 0);
    if (!src) return 0;
    if (!transform(src->image, src->chromaticities, src->has_chromaticities,
                   src->active_is_data, opt, *out)) {
        t_last_error = "colour transform failed";
        return 0;
    }
    return 1;
}

const char* exr_last_error(void) { return t_last_error.c_str(); }

void exr_render_free(EXRRenderResult* result) {
    if (!result || !result->pixels) return;
    std::free(result->pixels);
    result->pixels = nullptr;
    result->width = result->height = 0;
}

EXRSource* exr_open(const char* path, int32_t max_edge, int32_t use_data_window,
                    const char* layer) {
    return open_impl(path, max_edge, use_data_window != 0,
                     layer ? std::string(layer) : std::string()).release();
}

int exr_source_layer_count(const EXRSource* s) {
    return s ? static_cast<int>(s->layers.size()) : 0;
}
const char* exr_source_layer_id(const EXRSource* s, int i) {
    return (s && i >= 0 && i < static_cast<int>(s->layers.size()))
        ? s->layers[static_cast<std::size_t>(i)].id.c_str() : nullptr;
}
const char* exr_source_layer_label(const EXRSource* s, int i) {
    return (s && i >= 0 && i < static_cast<int>(s->layers.size()))
        ? s->layers[static_cast<std::size_t>(i)].label.c_str() : nullptr;
}
int exr_source_layer_is_data(const EXRSource* s, int i) {
    return (s && i >= 0 && i < static_cast<int>(s->layers.size()) &&
            s->layers[static_cast<std::size_t>(i)].data_pass) ? 1 : 0;
}
const char* exr_source_active_layer(const EXRSource* s) {
    return s ? s->active_layer.c_str() : "";
}
const char* exr_source_auto_layer(const EXRSource* s) {
    return s ? s->auto_layer.c_str() : "";
}

void exr_close(EXRSource* source) { delete source; }

int exr_source_render(EXRSource* source, const EXRRenderOptions* options,
                      EXRRenderResult* out) {
    if (!source || !out) return 0;
    out->width = out->height = 0;
    out->pixels = nullptr;
    EXRRenderOptions defaults{};
    return transform(source->image, source->chromaticities, source->has_chromaticities,
                     source->active_is_data, options ? *options : defaults, *out) ? 1 : 0;
}

const char* exr_source_chromaticities_name(const EXRSource* source) {
    return source ? source->chromaticities_name.c_str() : "";
}

int exr_source_has_chromaticities(const EXRSource* source) {
    return (source && source->has_chromaticities) ? 1 : 0;
}

int exr_describe(const char* path, char* buffer, size_t len) {
    if (!path || !buffer || len == 0) return 0;
    FileInfo info;
    std::string error;
    if (!inspect_file(path, info, error)) {
        std::snprintf(buffer, len, "Unreadable\t%s\n", error.c_str());
        return 0;
    }
    const LayerSelection sel = select_primary_layer(info.parts);
    const int pi = sel.valid() ? sel.part_index : 0;
    const PartDetail& d = info.details[pi];
    const PartInfo& part = info.parts[pi];

    std::string s;
    int64_t w = 0, h = 0, dw = 0, dh = 0;
    const bool have_disp = d.display_window.extent(w, h);
    const bool have_data = d.data_window.extent(dw, dh);

    if (have_disp) append(s, "Dimensions", std::to_string(w) + " x " + std::to_string(h));
    append(s, "Compression", d.compression);
    const StatedSpace stated = stated_space(d, part);
    const std::string& interop = part.color_interop_id;
    if (stated.stated) {
        append(s, "Colour", stated.from_interop ? stated.name + " (from colour interop ID)"
                          : !stated.from_attr.empty() ? stated.name + " (from " + stated.from_attr + ")"
                          : stated.name);
    } else if (interop == "data") {
        append(s, "Colour", "data \u2014 shown without a colour transform");
    } else {
        append(s, "Colour", "not stated (assuming ACEScg)");
    }
    if (!interop.empty()) {
        const NamedSpace* named = find_space_by_interop(interop.c_str());
        const bool agrees = named && stated.name == named->label;
        if (stated.stated && !stated.from_interop && !agrees) {
            append(s, "Colour interop ID", interop + " (chromaticities take precedence)");
        } else if (stated.stated || interop == "data") {
            append(s, "Colour interop ID", interop);
        } else {
            append(s, "Colour interop ID",
                   interop + " (ignored: not a scene-linear space this viewer knows)");
        }
    }
    if (!d.writer_colorspace.empty() && stated.from_attr.empty()) {
        const bool known = writer_space(d) != nullptr;
        append(s, "Renderer colour space",
               d.writer_colorspace + " (" + d.writer_colorspace_attr +
               (known ? "; outranked by the file's own tags)" : "; ignored: not a scene-linear space this viewer knows)"));
    }

    if (sel.valid()) {
        append(s, "Layer", sel.layer_name.empty() ? "default (unprefixed)" : sel.layer_name);
        std::string chans = sel.r + ", " + sel.g + ", " + sel.b;
        if (sel.has_alpha()) chans += ", " + sel.a;
        append(s, "Rendered channels", chans);
    } else {
        append(s, "Layer", "none renderable");
    }

    if (!part.channels.empty()) {
        append(s, "Pixel type", pixel_type_name(part.channels.front().type));
    }
    append(s, "Channels", std::to_string(part.channels.size()));
    if (info.parts.size() > 1) {
        append(s, "Parts", std::to_string(info.parts.size()) +
                           " (showing " + std::to_string(pi) + ")");
    }

    if (have_disp) {
        append(s, "Display window",
               std::to_string(d.display_window.min_x) + ", " +
               std::to_string(d.display_window.min_y) + " .. " +
               std::to_string(d.display_window.max_x) + ", " +
               std::to_string(d.display_window.max_y));
    }
    if (have_data && (dw != w || dh != h)) {
        append(s, "Data window",
               std::to_string(dw) + " x " + std::to_string(dh) + "  (overscan)");
    }
    if (d.pixel_aspect_ratio < 0.999f || d.pixel_aspect_ratio > 1.001f) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4f", d.pixel_aspect_ratio);
        append(s, "Pixel aspect", buf);
    }
    if (d.has_preview) append(s, "Preview attribute", "present");
    if (part.is_deep()) append(s, "Deep", "yes — not rendered");

    // Every layer in the file, so the panel shows what was not chosen too.
    std::vector<std::string> layers;
    for (const ChannelInfo& c : part.channels) {
        std::string layer, base;
        split_channel(c.name, layer, base);
        const std::string shown = layer.empty() ? "(default)" : layer;
        if (std::find(layers.begin(), layers.end(), shown) == layers.end()) {
            layers.push_back(shown);
        }
    }
    if (layers.size() > 1) {
        std::string all;
        for (std::size_t i = 0; i < layers.size(); ++i) {
            if (i) all += ", ";
            all += layers[i];
        }
        append(s, "Layers present", all);
    }

    std::snprintf(buffer, len, "%s", s.c_str());
    return 1;
}

// The baked LUTs, then raw as the last entry.
int exr_view_count(void) { return kBakedLutCount + 1; }
const char* exr_view_id(int i) {
    if (i == kBakedLutCount) return kRawViewId;
    return (i >= 0 && i < kBakedLutCount) ? kBakedLuts[i].name : nullptr;
}

// The ACES view name alone is ambiguous in a picker: the Rec.709 view is baked
// against several displays, so they would all read
// "ACES 2.0 - SDR 100 nits (Rec.709)". Label by target display instead. Where
// one display carries two different limiting gamuts, the gamut is appended so
// the two entries stay distinguishable.
const char* exr_view_display_name(int i) {
    if (i == kBakedLutCount) return "Raw \u2014 no transform";
    if (i < 0 || i >= kBakedLutCount) return nullptr;
    static std::vector<std::string> labels = [] {
        auto shortDisplay = [](const char* d) {
            std::string s = d ? d : "";
            const std::string suffix = " - Display";
            if (s.size() > suffix.size() &&
                s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
                s.erase(s.size() - suffix.size());
            }
            return s;
        };
        auto limitingGamut = [](const char* v) {
            std::string s = v ? v : "";
            const auto open = s.rfind('(');
            const auto close = s.rfind(')');
            if (open == std::string::npos || close == std::string::npos || close < open) return std::string();
            return s.substr(open + 1, close - open - 1);
        };

        std::vector<std::string> displays;
        for (int k = 0; k < kBakedLutCount; ++k) displays.push_back(shortDisplay(kBakedLuts[k].display));

        std::vector<std::string> out;
        out.reserve(static_cast<std::size_t>(kBakedLutCount));
        for (int k = 0; k < kBakedLutCount; ++k) {
            const bool ambiguous =
                std::count(displays.begin(), displays.end(), displays[k]) > 1;
            std::string label = "ACES 2.0 \u2014 " + displays[k];
            if (ambiguous) {
                const std::string g = limitingGamut(kBakedLuts[k].view);
                if (!g.empty()) label += " (" + g + " limited)";
            }
            out.push_back(label);
        }
        return out;
    }();
    return labels[static_cast<std::size_t>(i)].c_str();
}

const char* exr_default_view_id(void) { return kDefaultLutName; }

// The scene-linear spaces, then the PQ HDR-master input transforms.
int exr_colorspace_count(void) { return kSpaceCount + kInputLutCount; }
const char* exr_colorspace_id(int i) {
    if (i >= 0 && i < kSpaceCount) return kSpaces[i].id;
    if (i >= kSpaceCount && i < kSpaceCount + kInputLutCount) return kInputLuts[i - kSpaceCount].lut.name;
    return nullptr;
}
const char* exr_colorspace_display_name(int i) {
    if (i >= 0 && i < kSpaceCount) return kSpaces[i].label;
    if (i >= kSpaceCount && i < kSpaceCount + kInputLutCount) return kInputLuts[i - kSpaceCount].label;
    return nullptr;
}
const char* exr_default_colorspace_id(void) { return "acescg"; }

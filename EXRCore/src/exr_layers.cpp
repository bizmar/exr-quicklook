#include "EXRCore/exr_layers.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace exrcore {
namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

// One layer's channels, keyed by upper-cased base name -> full channel name.
using BaseMap = std::map<std::string, std::string>;

struct Layer {
    std::string name;
    BaseMap bases;
    int first_seen = 0;  // preserves file order for tie-breaking
};

const std::string* find(const BaseMap& m, const char* base) {
    auto it = m.find(base);
    return it == m.end() ? nullptr : &it->second;
}

bool has_rgb(const BaseMap& m) {
    return find(m, "R") && find(m, "G") && find(m, "B");
}

bool has_luma_chroma(const BaseMap& m) {
    return find(m, "Y") && find(m, "RY") && find(m, "BY");
}

// x/y(/z) or u/v components: position, normals, motion. Never colour.
bool has_vector(const BaseMap& m) {
    return (find(m, "X") && find(m, "Y")) || (find(m, "U") && find(m, "V"));
}

bool is_crypto(const std::string& name) {
    return to_lower(name).find("crypto") != std::string::npos;
}

// Alpha is only ever taken from the same layer. Plan §6.3: never borrowed.
std::string alpha_of(const BaseMap& m) {
    if (const std::string* a = find(m, "A")) return *a;
    return {};
}

std::vector<Layer> group_channels(const PartInfo& part) {
    std::map<std::string, Layer> by_name;
    int order = 0;
    for (const ChannelInfo& ch : part.channels) {
        std::string layer, base;
        split_channel(ch.name, layer, base);
        Layer& L = by_name[layer];
        if (L.bases.empty() && L.name.empty()) {
            L.name = layer;
            L.first_seen = order;
        }
        // First channel of a given base name wins; EXR should not have dupes.
        // Nuke writes layers other than rgba with long names ("mattes.red"),
        // so those are folded onto the short ones.
        std::string key = to_upper(base);
        if (key == "RED") key = "R";
        else if (key == "GREEN") key = "G";
        else if (key == "BLUE") key = "B";
        else if (key == "ALPHA") key = "A";
        L.bases.emplace(key, ch.name);
        ++order;
    }
    std::vector<Layer> out;
    out.reserve(by_name.size());
    for (auto& kv : by_name) out.push_back(kv.second);
    std::sort(out.begin(), out.end(),
              [](const Layer& a, const Layer& b) { return a.first_seen < b.first_seen; });
    return out;
}

LayerSelection make_rgb(int part, const Layer& L) {
    LayerSelection s;
    s.kind = LayerKind::kRGB;
    s.part_index = part;
    s.layer_name = L.name;
    s.r = *find(L.bases, "R");
    s.g = *find(L.bases, "G");
    s.b = *find(L.bases, "B");
    s.a = alpha_of(L.bases);
    return s;
}

LayerSelection make_luma(int part, const Layer& L) {
    LayerSelection s;
    s.kind = LayerKind::kLumaChroma;
    s.part_index = part;
    s.layer_name = L.name;
    s.r = *find(L.bases, "RY");
    s.g = *find(L.bases, "Y");
    s.b = *find(L.bases, "BY");
    s.a = alpha_of(L.bases);
    return s;
}

// Components onto red/green/blue, as Nuke's viewer shows them. A
// two-component pass leaves blue empty, which decodes as zero.
LayerSelection make_vector(int part, const Layer& L) {
    LayerSelection s;
    s.kind = LayerKind::kRGB;
    s.part_index = part;
    s.layer_name = L.name;
    const bool xy = find(L.bases, "X") && find(L.bases, "Y");
    s.r = *find(L.bases, xy ? "X" : "U");
    s.g = *find(L.bases, xy ? "Y" : "V");
    if (const std::string* third = find(L.bases, xy ? "Z" : "W")) s.b = *third;
    s.a = alpha_of(L.bases);
    return s;
}

LayerSelection make_grey(int part, const Layer& L, const std::string& channel) {
    LayerSelection s;
    s.kind = LayerKind::kGrey;
    s.part_index = part;
    s.layer_name = L.name;
    s.r = s.g = s.b = channel;
    s.a = alpha_of(L.bases);
    return s;
}

// A data pass: depth, position, motion, normals, ids, masks, cryptomatte.
// Multi-part renders often name the part after the pass and leave its channels
// unprefixed, so an unprefixed layer takes its part's name. Vector components
// are data whatever the layer is called.
bool is_data_layer(const PartInfo& part, const Layer& L) {
    if (part.color_interop_id == "data") return true;
    if (is_never_auto_layer(L.name.empty() ? part.name : L.name)) return true;
    return !has_rgb(L.bases) && !has_luma_chroma(L.bases) && has_vector(L.bases);
}

// Layer selection within one part, in the plan's documented preference order.
LayerSelection select_in_part(int part_index, const PartInfo& part) {
    const std::vector<Layer> layers = group_channels(part);
    auto data = [&](const Layer& L) { return is_data_layer(part, L); };

    // 1. Unprefixed R,G,B (+A).
    for (const Layer& L : layers) {
        if (L.name.empty() && !data(L) && has_rgb(L.bases)) return make_rgb(part_index, L);
    }
    // 2. A layer explicitly named beauty / rgba / main / composite.
    for (const Layer& L : layers) {
        if (!L.name.empty() && is_preferred_name(L.name) && has_rgb(L.bases)) {
            return make_rgb(part_index, L);
        }
    }
    // 3. Luminance-chroma, unprefixed first.
    for (const Layer& L : layers) {
        if (L.name.empty() && !data(L) && has_luma_chroma(L.bases)) return make_luma(part_index, L);
    }
    for (const Layer& L : layers) {
        if (!data(L) && has_luma_chroma(L.bases)) return make_luma(part_index, L);
    }
    // 4. First remaining layer resolving to three RGB channels.
    for (const Layer& L : layers) {
        if (!data(L) && has_rgb(L.bases)) return make_rgb(part_index, L);
    }
    // 5/6. Single channel, or Y alone, rendered as greyscale.
    for (const Layer& L : layers) {
        if (data(L)) continue;
        if (const std::string* y = find(L.bases, "Y")) return make_grey(part_index, L, *y);
        // A lone non-alpha channel. Skip layers that are only an alpha.
        for (const auto& kv : L.bases) {
            if (kv.first == "A") continue;
            if (is_never_auto_layer(kv.first)) continue;
            return make_grey(part_index, L, kv.second);
        }
    }
    return {};
}

bool part_has_rgb(const PartInfo& part) {
    for (const Layer& L : group_channels(part)) {
        if (!is_data_layer(part, L) && has_rgb(L.bases)) return true;
    }
    return false;
}

// The dot-separated components of a name, lower-cased: "ViewLayer.Depth" ->
// {"viewlayer", "depth"}.
std::vector<std::string> components(const std::string& lower) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= lower.size()) {
        const std::size_t dot = lower.find('.', start);
        out.push_back(lower.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return out;
}

// The words inside a name, split at '_', '-', '.', spaces, digits and camelCase
// humps, lower-cased: "s_fur_uv" -> {s, fur, uv}; "instanceID" -> {instance, id}.
std::vector<std::string> words(const std::string& name) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] { if (!cur.empty()) { out.push_back(to_lower(cur)); cur.clear(); } };
    for (std::size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (!std::isalpha(c)) { flush(); continue; }
        // A new word starts at lower->Upper ("instanceID") and at the last
        // capital of an acronym followed by lower case ("IDMask" -> ID, Mask).
        if (std::isupper(c) && !cur.empty()) {
            const unsigned char prev = static_cast<unsigned char>(cur.back());
            const bool next_lower = i + 1 < name.size() &&
                                    std::islower(static_cast<unsigned char>(name[i + 1]));
            if (std::islower(prev) || (std::isupper(prev) && next_lower)) flush();
        }
        cur += static_cast<char>(c);
    }
    flush();
    return out;
}

}  // namespace

void split_channel(const std::string& full, std::string& layer, std::string& base) {
    const std::size_t dot = full.rfind('.');
    if (dot == std::string::npos) {
        layer.clear();
        base = full;
    } else {
        layer = full.substr(0, dot);
        base = full.substr(dot + 1);
    }
}

// The beauty, by name. The last component counts, so "ViewLayer.Combined"
// (Blender) qualifies. See docs/LAYER-RULES.md.
bool is_preferred_name(const std::string& name) {
    const std::string n = to_lower(name);
    if (n.empty()) return true;
    const std::size_t dot = n.rfind('.');
    const std::string last = dot == std::string::npos ? n : n.substr(dot + 1);
    static const char* const kBeauty[] = {
        "rgba", "rgb", "beauty", "main", "composite",
        "combined",     // Blender
        "ci",           // RenderMan
        "c",            // Karma, Mantra
        "finalimage",   // Unreal Movie Render Queue
    };
    for (const char* k : kBeauty) {
        if (last == k) return true;
    }
    return false;
}

// Data passes, by name. The tiers are documented, with examples and the
// reasoning, in docs/LAYER-RULES.md -- keep the two in step.
bool is_never_auto_layer(const std::string& layer_name) {
    if (layer_name.empty()) return false;  // the default layer is always eligible
    const std::string n = to_lower(layer_name);

    // 1. Cryptomatte, anywhere in the name: "CryptoObject00", Arnold's
    //    "uCryptoObject", "crypto_material", "cryptomatte/...".
    if (n.find("crypto") != std::string::npos) return true;

    // 2. Distinctive words, anywhere: renderer prefixes and plurals are caught
    //    ("VRayZDepth", "MotionVectors", "WorldPosition", "PuzzleMatte").
    static const char* const kAnywhere[] = {
        "depth", "position", "normal", "motion", "velocity", "vector", "matte", "mask",
        "rendertime", "cputime", "raycount", "facingratio",   // diagnostics
        "dpdtime",        // RenderMan motion
        "volumez",        // Redshift volume depth
        "samplerinfo", "wirecolor",                           // V-Ray
        "occlusion",      // ambient occlusion: a 0-1 multiplier, read straight
    };
    for (const char* k : kAnywhere) {
        if (n.find(k) != std::string::npos) return true;
    }

    // 3. Short names, only as a whole dot-separated component. As substrings
    //    "p" or "n" would catch nearly everything, and even as words they would
    //    catch light groups such as "rim_n" or "key_p".
    static const char* const kComponent[] = {
        "z", "zback", "pz",                            // depth (Pz: Karma/Mantra)
        "n", "nn", "nw", "ng", "ngn", "nt", "tn", "vn",  // normals, tangents, view vectors
        "p", "po", "pc", "pow", "pref", "pw", "pworld", "wp",  // positions
        "mv", "mvec", "forward", "backward",           // motion
        "uv", "st", "uvw",                             // texture coordinates
        "id", "objectid", "materialid", "instanceid",  // ids
        "indexob", "indexma",                          // Blender object/material index
        "mist",                                        // Blender mist (a depth ramp)
        "ao",                                          // ambient occlusion (Blender ViewLayer.AO)
    };
    const std::vector<std::string> comps = components(n);
    for (const std::string& c : comps) {
        for (const char* k : kComponent) {
            if (c == k) return true;
        }
    }

    // 4. A few unambiguous words of two letters or more, as a word inside a
    //    compound name: "s_uv", "instanceID", "objectId", "nw_feature".
    static const char* const kWord[] = {"uv", "uvw", "st", "id", "nw", "nworld", "mv", "mvec",
                                        "pref", "pworld", "zdepth", "ao"};
    for (const std::string& w : words(layer_name)) {
        for (const char* k : kWord) {
            if (w == k) return true;
        }
    }

    // 5. The "m_" prefix many studios use for mattes: "m_chars", "m_set".
    for (const std::string& c : comps) {
        if (starts_with(c, "m_")) return true;
    }
    return false;
}

std::vector<LayerOption> enumerate_layers(const std::vector<PartInfo>& parts) {
    std::vector<LayerOption> out;
    const bool multipart = parts.size() > 1;

    for (int pi = 0; pi < static_cast<int>(parts.size()); ++pi) {
        const PartInfo& part = parts[pi];
        if (part.is_deep()) continue;  // cannot be rendered, so never offered

        for (const Layer& L : group_channels(part)) {
            LayerSelection sel;
            if (has_rgb(L.bases)) {
                sel = make_rgb(pi, L);
            } else if (has_luma_chroma(L.bases)) {
                sel = make_luma(pi, L);
            } else if (has_vector(L.bases)) {
                sel = make_vector(pi, L);
            } else if (const std::string* y = find(L.bases, "Y")) {
                sel = make_grey(pi, L, *y);
            } else {
                // A lone channel, alpha excepted: render it as greyscale so a
                // depth or mask pass is still inspectable.
                const std::string* single = nullptr;
                for (const auto& kv : L.bases) {
                    if (kv.first == "A") continue;
                    single = &kv.second;
                    break;
                }
                if (!single) continue;
                sel = make_grey(pi, L, *single);
            }
            if (!sel.valid()) continue;

            const std::string partLabel =
                part.name.empty() ? ("part " + std::to_string(pi)) : part.name;
            auto labelled = [&](const std::string& name) {
                return multipart ? partLabel + " \u00b7 " + name : name;
            };

            LayerOption opt;
            opt.id = std::to_string(pi) + ":" + L.name;
            // An unprefixed lone channel is named by the channel: "Z", not
            // "default", for a depth-only file.
            const std::string layerLabel =
                !L.name.empty() ? L.name
                : (sel.kind == LayerKind::kGrey ? sel.r : std::string("default"));
            opt.label = labelled(layerLabel);
            opt.selection = sel;
            std::string base, ignored;
            split_channel(sel.r, ignored, base);
            opt.data_pass = is_data_layer(part, L) ||
                            (sel.kind == LayerKind::kGrey && is_never_auto_layer(base));
            out.push_back(std::move(opt));

            // Channels the layer's own view does not show -- Z alongside an
            // unprefixed RGB being the common case. §6.3 requires depth and
            // friends to stay listed even though they are never the default,
            // and grouping by prefix would otherwise bury them.
            for (const auto& kv : L.bases) {
                const std::string& base = kv.first;
                if (base == "A") continue;
                if (kv.second == sel.r || kv.second == sel.g || kv.second == sel.b) continue;

                LayerOption ch;
                ch.id = std::to_string(pi) + ":" + L.name + "#" + base;
                ch.label = labelled(kv.second);   // already the full channel name
                ch.selection = make_grey(pi, L, kv.second);
                ch.data_pass = true;  // a bare channel is never the default
                out.push_back(std::move(ch));
            }
        }
    }
    return out;
}

LayerSelection select_primary_layer(const std::vector<PartInfo>& parts) {
    // Part selection, plan §6.3: a preferred-named part containing RGB wins;
    // otherwise the first part containing RGB; otherwise anything renderable.
    // Never a deep part.
    int fallback_rgb = -1;
    int fallback_any = -1;

    for (int i = 0; i < static_cast<int>(parts.size()); ++i) {
        const PartInfo& p = parts[i];
        if (p.is_deep()) continue;
        const bool rgb = part_has_rgb(p);
        if (rgb && is_preferred_name(p.name)) return select_in_part(i, p);
        if (rgb && fallback_rgb < 0) fallback_rgb = i;
        if (fallback_any < 0 && select_in_part(i, p).valid()) fallback_any = i;
    }
    if (fallback_rgb >= 0) return select_in_part(fallback_rgb, parts[fallback_rgb]);
    if (fallback_any >= 0) return select_in_part(fallback_any, parts[fallback_any]);

    // Nothing but data passes. Separate-AOV renders write exactly this -- a
    // depth-only or position-only file -- and the pass is the whole point of
    // the file, so show the first one rather than the generic icon. Cryptomatte
    // hash layers are the exception: they have no readable raw form.
    const std::vector<LayerOption> all = enumerate_layers(parts);
    for (const LayerOption& o : all) {
        const std::string& part_name = parts[static_cast<std::size_t>(o.selection.part_index)].name;
        if (is_crypto(o.selection.layer_name) || is_crypto(part_name)) continue;
        return o.selection;
    }
    // A cryptomatte-only file: its numbered layers ("uCryptoObject00") hold
    // ID hashes, but renderers also write an un-numbered colour preview
    // ("uCryptoObject") that is meant to be looked at. Show that if present.
    for (const LayerOption& o : all) {
        const std::string& n = o.selection.layer_name;
        if (o.selection.kind == LayerKind::kRGB && is_crypto(n) && !n.empty() &&
            !std::isdigit(static_cast<unsigned char>(n.back()))) {
            return o.selection;
        }
    }
    return {};
}

}  // namespace exrcore

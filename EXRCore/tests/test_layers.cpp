// Layer-selection and hardening tests. Plan §9: "Unit tests on layer selection
// -- the most bug-prone logic, so cover it hardest."
#include "EXRCore/exr_layers.h"
#include "EXRCore/exr_limits.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace exrcore;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            std::printf("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        ++g_checks;                                                              \
        auto va__ = (a);                                                         \
        auto vb__ = (b);                                                         \
        if (!(va__ == vb__)) {                                                   \
            std::printf("  FAIL  %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b);\
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

static PartInfo part(std::string name, std::vector<std::string> channels,
                     PartType type = PartType::kScanline) {
    PartInfo p;
    p.name = std::move(name);
    p.type = type;
    for (auto& c : channels) p.channels.push_back(ChannelInfo{c, PixelType::kHalf, 1, 1});
    return p;
}

static void test_split() {
    std::string l, b;
    split_channel("R", l, b);              CHECK(l.empty()); CHECK_EQ(b, std::string("R"));
    split_channel("diffuse.R", l, b);      CHECK_EQ(l, std::string("diffuse"));
                                           CHECK_EQ(b, std::string("R"));
    split_channel("char.diffuse.R", l, b); CHECK_EQ(l, std::string("char.diffuse"));
                                           CHECK_EQ(b, std::string("R"));
}

static void test_plain_rgba() {
    auto s = select_primary_layer({part("", {"R", "G", "B", "A"})});
    CHECK(s.valid());
    CHECK(s.kind == LayerKind::kRGB);
    CHECK(s.layer_name.empty());
    CHECK_EQ(s.r, std::string("R"));
    CHECK(s.has_alpha());
    CHECK_EQ(s.a, std::string("A"));
}

static void test_unprefixed_beats_named() {
    auto s = select_primary_layer({part("", {"R", "G", "B",
                                             "diffuse.R", "diffuse.G", "diffuse.B"})});
    CHECK(s.layer_name.empty());
}

static void test_cryptomatte_never_selected() {
    // Crypto layers sort before an unprefixed layer alphabetically; the rule
    // must be about kind, not order.
    auto s = select_primary_layer({part("", {"CryptoObject00.R", "CryptoObject00.G",
                                             "CryptoObject00.B", "R", "G", "B"})});
    CHECK(s.layer_name.empty());
    CHECK_EQ(s.r, std::string("R"));

    // With nothing but crypto and depth, depth is shown: it is data, but it is
    // all the file has. Cryptomatte is still never the choice.
    auto depth = select_primary_layer({part("", {"CryptoMaterial00.R", "CryptoMaterial00.G",
                                                 "CryptoMaterial00.B", "Z"})});
    CHECK(depth.valid());
    CHECK_EQ(depth.r, std::string("Z"));
}

static void test_data_layers_never_selected() {
    auto s = select_primary_layer({part("", {"Z", "normal.R", "normal.G", "normal.B",
                                             "beauty.R", "beauty.G", "beauty.B"})});
    CHECK_EQ(s.layer_name, std::string("beauty"));
}

static void test_preferred_layer_name() {
    auto s = select_primary_layer({part("", {"diffuse.R", "diffuse.G", "diffuse.B",
                                             "rgba.R", "rgba.G", "rgba.B"})});
    CHECK_EQ(s.layer_name, std::string("rgba"));
}

static void test_first_remaining_layer() {
    auto s = select_primary_layer({part("", {"diffuse.R", "diffuse.G", "diffuse.B",
                                             "specular.R", "specular.G", "specular.B"})});
    CHECK_EQ(s.layer_name, std::string("diffuse"));
}

static void test_alpha_never_borrowed() {
    // Unprefixed A must NOT be attached to the diffuse layer's RGB.
    auto s = select_primary_layer({part("", {"diffuse.R", "diffuse.G", "diffuse.B", "A"})});
    CHECK_EQ(s.layer_name, std::string("diffuse"));
    CHECK(!s.has_alpha());
}

static void test_luma_chroma() {
    auto s = select_primary_layer({part("", {"Y", "RY", "BY"})});
    CHECK(s.kind == LayerKind::kLumaChroma);
    CHECK_EQ(s.g, std::string("Y"));
}

static void test_greyscale() {
    auto y = select_primary_layer({part("", {"Y"})});
    CHECK(y.kind == LayerKind::kGrey);
    CHECK_EQ(y.r, std::string("Y"));

    auto single = select_primary_layer({part("", {"luminance"})});
    CHECK(single.kind == LayerKind::kGrey);
}

static void test_multipart_skips_deep() {
    std::vector<PartInfo> parts = {
        part("depth", {"R", "G", "B"}, PartType::kDeepScanline),
        part("rgba", {"R", "G", "B"}),
    };
    auto s = select_primary_layer(parts);
    CHECK_EQ(s.part_index, 1);
}

static void test_multipart_prefers_named_part() {
    std::vector<PartInfo> parts = {
        part("someAOV", {"R", "G", "B"}),
        part("beauty", {"R", "G", "B"}),
    };
    CHECK_EQ(select_primary_layer(parts).part_index, 1);
}

static void test_multipart_falls_back_to_first_rgb() {
    std::vector<PartInfo> parts = {
        part("aovA", {"R", "G", "B"}),
        part("aovB", {"R", "G", "B"}),
    };
    CHECK_EQ(select_primary_layer(parts).part_index, 0);
}

static void test_empty_and_degenerate() {
    CHECK(!select_primary_layer({}).valid());
    CHECK(!select_primary_layer({part("", {})}).valid());
    CHECK(!select_primary_layer({part("", {"A"})}).valid());  // alpha alone is not an image
}

static void test_enumerate_layers() {
    // Everything selectable is offered, including the never-auto ones, and the
    // primary layer must appear in the list it is chosen from.
    auto opts = enumerate_layers({part("", {"R", "G", "B", "A",
                                            "diffuse.R", "diffuse.G", "diffuse.B",
                                            "CryptoObject00.R", "CryptoObject00.G",
                                            "CryptoObject00.B", "Z"})});
    // default(RGB), Z as a bare channel, diffuse, CryptoObject00
    CHECK_EQ(opts.size(), std::size_t(4));
    CHECK(!opts[0].data_pass);                   // default layer
    bool crypto_listed = false, crypto_excluded = false, z_listed = false;
    for (const auto& o : opts) {
        if (o.selection.layer_name == "CryptoObject00") { crypto_listed = true; crypto_excluded = o.data_pass; }
        if (o.selection.r == "Z") z_listed = true;   // depth stays reachable (§6.3)
    }
    CHECK(crypto_listed);
    CHECK(crypto_excluded);                          // listed, never the default
    CHECK(z_listed);

    // Deep parts are omitted entirely -- they cannot be rendered.
    std::vector<PartInfo> mixed = {
        part("deepbit", {"R", "G", "B"}, PartType::kDeepScanline),
        part("rgba", {"R", "G", "B"}),
    };
    auto m = enumerate_layers(mixed);
    CHECK_EQ(m.size(), std::size_t(1));
    CHECK_EQ(m[0].selection.part_index, 1);

    // The automatic choice is one of the offered options.
    const auto sel = select_primary_layer(mixed);
    bool found = false;
    for (const auto& o : m) {
        if (o.selection.part_index == sel.part_index &&
            o.selection.layer_name == sel.layer_name) found = true;
    }
    CHECK(found);
}

static void test_limits() {
    Box2i ok{0, 0, 639, 359};
    int64_t w = 0, h = 0, n = 0;
    CHECK(ok.extent(w, h));
    CHECK_EQ(w, int64_t(640));
    CHECK_EQ(h, int64_t(360));
    CHECK(ok.pixel_count(n));
    CHECK_EQ(n, int64_t(230400));

    Box2i inverted{10, 10, 0, 0};
    CHECK(!inverted.valid());
    CHECK(!inverted.extent(w, h));

    Box2i huge{0, 0, 2'000'000, 2'000'000};
    CHECK(!huge.extent(w, h));          // exceeds kMaxDimension, rejected not clamped

    Box2i wide{0, 0, 65534, 65534};     // within per-axis bound...
    CHECK(wide.extent(w, h));
    CHECK(!wide.pixel_count(n));        // ...but 4.29e9 pixels exceeds kMaxPixels

    int64_t out = 0;
    CHECK(!checked_mul(INT64_MAX, 2, out));
    CHECK(!checked_mul(-1, 2, out));
    CHECK(checked_mul(1000, 1000, out));
    CHECK_EQ(out, int64_t(1'000'000));
    CHECK(!checked_add(INT64_MAX, 1, out));

    CHECK(buffer_bytes(230400, 4, 2, out));
    CHECK_EQ(out, int64_t(1'843'200));
    CHECK(!buffer_bytes(300'000'000, 40, 4, out));   // 48 GB, over the ceiling
    CHECK(!buffer_bytes(INT64_MAX, 4, 2, out));      // overflow, not a huge number
}

static const LayerOption* option_named(const std::vector<LayerOption>& opts,
                                       const std::string& layer) {
    for (const auto& o : opts) {
        if (o.selection.layer_name == layer) return &o;
    }
    return nullptr;
}

static const LayerOption* option_showing(const std::vector<LayerOption>& opts,
                                         const std::string& channel) {
    for (const auto& o : opts) {
        if (o.selection.r == channel) return &o;
    }
    return nullptr;
}

static void test_position_and_motion_never_selected() {
    // Position and motion passes are data, like depth and normals: listed,
    // never the default while there is imagery to show (§6.3).
    auto s = select_primary_layer({part("", {"P.x", "P.y", "P.z",
                                             "motion.u", "motion.v",
                                             "diffuse.R", "diffuse.G", "diffuse.B"})});
    CHECK_EQ(s.layer_name, std::string("diffuse"));

    // Spellings used by Arnold, Redshift, V-Ray, Karma, Blender and Nuke.
    for (const char* n : {"P", "Pref", "Pworld", "position", "WorldPosition",
                          "motionvector", "MotionVectors", "VRayVelocity", "forward",
                          "backward", "mv", "VRayZDepth", "depth", "ViewLayer.Depth",
                          "ViewLayer.Vector", "N", "VRayNormals", "crypto_object",
                          "PuzzleMatte", "uv"}) {
        if (!is_never_auto_layer(n)) std::printf("  (not flagged: %s)\n", n);
        CHECK(is_never_auto_layer(n));
    }
    // Colour AOVs must not be caught by the broader matching.
    for (const char* n : {"diffuse", "specular", "emission", "beauty", "albedo", "sss",
                          "transmission", "indirect", "ViewLayer.Combined", "coat",
                          "sheen", "volume", "background", "Pass"}) {
        if (is_never_auto_layer(n)) std::printf("  (wrongly flagged: %s)\n", n);
        CHECK(!is_never_auto_layer(n));
    }
}

static void test_vector_passes_shown_as_colour() {
    // x/y/z and u/v are vector components, mapped to red/green/blue the way
    // Nuke's viewer does. The y component must not be mistaken for luminance.
    auto opts = enumerate_layers({part("", {"R", "G", "B",
                                            "P.x", "P.y", "P.z",
                                            "motion.u", "motion.v",
                                            "N.X", "N.Y", "N.Z"})});
    const LayerOption* p = option_named(opts, "P");
    CHECK(p != nullptr);
    if (p) {
        CHECK(p->selection.kind == LayerKind::kRGB);
        CHECK_EQ(p->selection.r, std::string("P.x"));
        CHECK_EQ(p->selection.g, std::string("P.y"));
        CHECK_EQ(p->selection.b, std::string("P.z"));
        CHECK(p->data_pass);
    }
    const LayerOption* mv = option_named(opts, "motion");
    CHECK(mv != nullptr);
    if (mv) {
        CHECK(mv->selection.kind == LayerKind::kRGB);
        CHECK_EQ(mv->selection.r, std::string("motion.u"));
        CHECK_EQ(mv->selection.g, std::string("motion.v"));
        CHECK(mv->selection.b.empty());             // two components: blue stays 0
        CHECK(mv->data_pass);
    }
    const LayerOption* n = option_named(opts, "N");
    CHECK(n != nullptr && n->selection.r == "N.X" && n->data_pass);

    // The components are shown together, not repeated as bare channels.
    CHECK(option_showing(opts, "P.y") == nullptr || option_showing(opts, "P.y") == p);
    CHECK(!option_named(opts, "")->data_pass);      // the beauty is imagery

    // A vector layer is data whatever it is called.
    auto odd = enumerate_layers({part("", {"R", "G", "B", "foo.x", "foo.y", "foo.z"})});
    CHECK(option_named(odd, "foo") && option_named(odd, "foo")->data_pass);
}

static void test_bare_channel_labels() {
    // A channel the layer's own view does not show is offered on its own,
    // labelled by its full name once -- not "diffuse.diffuse.Z".
    auto opts = enumerate_layers({part("", {"diffuse.R", "diffuse.G", "diffuse.B",
                                            "diffuse.Z", "R", "G", "B", "Z"})});
    const LayerOption* dz = option_showing(opts, "diffuse.Z");
    CHECK(dz != nullptr);
    if (dz) CHECK_EQ(dz->label, std::string("diffuse.Z"));
    const LayerOption* z = option_showing(opts, "Z");
    CHECK(z != nullptr);
    if (z) { CHECK_EQ(z->label, std::string("Z")); CHECK(z->data_pass); }
}

static void test_data_only_file_shows_its_data() {
    // Separate-AOV renders write one pass per file. A depth-only or
    // position-only EXR has no imagery to prefer, so its data pass is shown
    // rather than the generic icon.
    auto z = select_primary_layer({part("", {"Z"})});
    CHECK(z.valid());
    CHECK(z.kind == LayerKind::kGrey);
    CHECK_EQ(z.r, std::string("Z"));
    auto zopts = enumerate_layers({part("", {"Z"})});
    CHECK(zopts.size() == 1 && zopts[0].data_pass);

    auto p = select_primary_layer({part("", {"P.x", "P.y", "P.z"})});
    CHECK(p.valid());
    CHECK(p.kind == LayerKind::kRGB);
    CHECK_EQ(p.r, std::string("P.x"));

    auto mv = select_primary_layer({part("", {"MotionVectors.R", "MotionVectors.G",
                                              "MotionVectors.B"})});
    CHECK(mv.valid());

    // Cryptomatte alone is hash data with no readable raw form: still nothing.
    auto c = select_primary_layer({part("", {"CryptoObject00.R", "CryptoObject00.G",
                                             "CryptoObject00.B"})});
    CHECK(!c.valid());
}

static void test_data_part_names() {
    // Multi-part renders often name the part after the pass and leave the
    // channels unprefixed, so the part name has to count.
    std::vector<PartInfo> parts = {
        part("P", {"R", "G", "B"}),
        part("depth", {"Z"}),
        part("diffuse", {"R", "G", "B"}),
    };
    CHECK_EQ(select_primary_layer(parts).part_index, 2);
    auto opts = enumerate_layers(parts);
    CHECK_EQ(opts.size(), std::size_t(3));
    if (opts.size() == 3) {
        CHECK(opts[0].data_pass);
        CHECK(opts[1].data_pass);
        CHECK(!opts[2].data_pass);
    }
}

static void test_blender_combined() {
    // Blender names its beauty "<view layer>.Combined" and sorts AO before it.
    auto s = select_primary_layer({part("", {"ViewLayer.AO.R", "ViewLayer.AO.G",
                                             "ViewLayer.AO.B", "ViewLayer.Combined.R",
                                             "ViewLayer.Combined.G", "ViewLayer.Combined.B",
                                             "ViewLayer.Combined.A", "ViewLayer.Depth.Z"})});
    CHECK_EQ(s.layer_name, std::string("ViewLayer.Combined"));
}

static void test_nuke_long_channel_names() {
    // Nuke writes non-rgba layers with long names: "mattes.red", not "mattes.R".
    // From a real comp: these must be one RGB layer, not three greyscale ones.
    auto opts = enumerate_layers({part("", {"R", "G", "B", "A",
                                            "mattes.red", "mattes.green", "mattes.blue",
                                            "diffuse.red", "diffuse.green", "diffuse.blue",
                                            "diffuse.alpha"})});
    CHECK_EQ(opts.size(), std::size_t(3));
    const LayerOption* m = option_named(opts, "mattes");
    CHECK(m != nullptr);
    if (m) {
        CHECK(m->selection.kind == LayerKind::kRGB);
        CHECK_EQ(m->selection.r, std::string("mattes.red"));
        CHECK_EQ(m->selection.b, std::string("mattes.blue"));
        CHECK(m->data_pass);
    }
    const LayerOption* d = option_named(opts, "diffuse");
    CHECK(d != nullptr && !d->data_pass && d->selection.a == "diffuse.alpha");
    auto s = select_primary_layer({part("", {"diffuse.red", "diffuse.green", "diffuse.blue"})});
    CHECK_EQ(s.r, std::string("diffuse.red"));
}

int main() {
    test_split();
    test_plain_rgba();
    test_unprefixed_beats_named();
    test_cryptomatte_never_selected();
    test_data_layers_never_selected();
    test_preferred_layer_name();
    test_first_remaining_layer();
    test_alpha_never_borrowed();
    test_luma_chroma();
    test_greyscale();
    test_multipart_skips_deep();
    test_multipart_prefers_named_part();
    test_multipart_falls_back_to_first_rgb();
    test_empty_and_degenerate();
    test_enumerate_layers();
    test_position_and_motion_never_selected();
    test_vector_passes_shown_as_colour();
    test_bare_channel_labels();
    test_data_only_file_shows_its_data();
    test_data_part_names();
    test_blender_combined();
    test_nuke_long_channel_names();
    test_limits();

    std::printf("\n  %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

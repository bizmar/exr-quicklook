// Generates the plan §9 fixture corpus using the real OpenEXR writer, so the
// files exercise code paths our hand-rolled Python writer cannot reach: DWAA,
// DWAB, multi-part, deep, subsampled luminance-chroma, 32-bit float.
#include <ImfChannelList.h>
#include <ImfChromaticities.h>
#include <ImfDeepFrameBuffer.h>
#include <ImfDeepScanLineOutputFile.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfMultiPartOutputFile.h>
#include <ImfPreviewImage.h>
#include <ImfTiledOutputFile.h>
#include <ImfOutputFile.h>
#include <ImfOutputPart.h>
#include <ImfPartType.h>
#include <ImfStandardAttributes.h>
#include <ImfStringAttribute.h>

#include <half.h>

#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace {

const int W = 256, H = 144;

Imf::Chromaticities acescg() {
    return Imf::Chromaticities(Imath::V2f(0.713f, 0.293f), Imath::V2f(0.165f, 0.830f),
                               Imath::V2f(0.128f, 0.044f), Imath::V2f(0.32168f, 0.33767f));
}
Imf::Chromaticities aces2065() {
    return Imf::Chromaticities(Imath::V2f(0.7347f, 0.2653f), Imath::V2f(0.0f, 1.0f),
                               Imath::V2f(0.0001f, -0.077f), Imath::V2f(0.32168f, 0.33767f));
}

// A recognisable ramp: red rises left-to-right, green top-to-bottom, blue flat.
void fill(std::vector<half>& r, std::vector<half>& g, std::vector<half>& b, float scale,
          int w = W, int h = H) {
    r.resize(size_t(w) * h); g.resize(size_t(w) * h); b.resize(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * w + x;
            r[i] = half(scale * float(x) / float(w - 1));
            g[i] = half(scale * float(y) / float(h - 1));
            b[i] = half(scale * 0.25f);
        }
    }
}

void add_layer(Imf::Header& hdr, Imf::FrameBuffer& fb, const std::string& prefix,
               std::vector<half>& r, std::vector<half>& g, std::vector<half>& b) {
    const std::string p = prefix.empty() ? "" : prefix + ".";
    for (auto* pair : {&r, &g, &b}) (void)pair;
    const char* base[3] = {"R", "G", "B"};
    std::vector<half>* data[3] = {&r, &g, &b};
    for (int i = 0; i < 3; ++i) {
        const std::string name = p + base[i];
        hdr.channels().insert(name, Imf::Channel(Imf::HALF));
        fb.insert(name, Imf::Slice(Imf::HALF, reinterpret_cast<char*>(data[i]->data()),
                                   sizeof(half), sizeof(half) * W));
    }
}

void write_scanline(const std::string& path, Imf::Header hdr, Imf::FrameBuffer& fb, int h = H) {
    Imf::OutputFile file(path.c_str(), hdr);
    file.setFrameBuffer(fb);
    file.writePixels(h);
    std::printf("  %s\n", path.c_str());
}

// Multi-layer AOV file with everything the selector must ignore.
void make_multilayer(const std::string& path, Imf::Compression c) {
    Imf::Header hdr(W, H);
    hdr.compression() = c;
    Imf::addChromaticities(hdr, acescg());

    static std::vector<half> br, bg, bb, dr, dg, db, cr, cg, cb, mr, mg, mb, z;
    fill(br, bg, bb, 1.0f);
    fill(dr, dg, db, 0.5f);
    fill(cr, cg, cb, 0.9f);
    fill(mr, mg, mb, 0.2f);
    z.assign(size_t(W) * H, half(10.0f));

    Imf::FrameBuffer fb;
    add_layer(hdr, fb, "", br, bg, bb);                 // the one that must win
    add_layer(hdr, fb, "diffuse", dr, dg, db);
    add_layer(hdr, fb, "CryptoObject00", cr, cg, cb);   // must never be chosen
    add_layer(hdr, fb, "mask", mr, mg, mb);             // must never be chosen
    hdr.channels().insert("Z", Imf::Channel(Imf::FLOAT));
    static std::vector<float> zf(size_t(W) * H, 10.0f);
    fb.insert("Z", Imf::Slice(Imf::FLOAT, reinterpret_cast<char*>(zf.data()),
                              sizeof(float), sizeof(float) * W));
    write_scanline(path, hdr, fb);
}

// No unprefixed layer: the selector must fall through to a named one, and must
// still refuse crypto/depth.
void make_aov_only(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::ZIP_COMPRESSION;
    Imf::addChromaticities(hdr, acescg());
    static std::vector<half> dr, dg, db, sr, sg, sb, cr, cg, cb;
    fill(dr, dg, db, 0.6f); fill(sr, sg, sb, 0.3f); fill(cr, cg, cb, 0.9f);
    Imf::FrameBuffer fb;
    add_layer(hdr, fb, "CryptoAsset00", cr, cg, cb);
    add_layer(hdr, fb, "diffuse", dr, dg, db);
    add_layer(hdr, fb, "specular", sr, sg, sb);
    write_scanline(path, hdr, fb);
}

// A layer literally named "beauty" alongside others.
void make_named_beauty(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::DWAB_COMPRESSION;
    Imf::addChromaticities(hdr, acescg());
    static std::vector<half> ar, ag, ab, br, bg, bb;
    fill(ar, ag, ab, 0.4f); fill(br, bg, bb, 1.0f);
    Imf::FrameBuffer fb;
    add_layer(hdr, fb, "aaa_first", ar, ag, ab);
    add_layer(hdr, fb, "beauty", br, bg, bb);
    write_scanline(path, hdr, fb);
}

void make_simple(const std::string& path, Imf::Compression c, const Imf::Chromaticities* chroma,
                 bool with_alpha, Imf::PixelType type = Imf::HALF,
                 const char* interop_id = nullptr, const char* arnold_cs = nullptr,
                 const std::vector<std::pair<const char*, const char*>>& extra = {}) {
    Imf::Header hdr(W, H);
    hdr.compression() = c;
    if (chroma) Imf::addChromaticities(hdr, *chroma);
    if (interop_id) Imf::addColorInteropID(hdr, interop_id);
    if (arnold_cs) hdr.insert("arnold/color_space", Imf::StringAttribute(arnold_cs));
    for (const auto& [name, value] : extra) hdr.insert(name, Imf::StringAttribute(value));
    static std::vector<half> r, g, b, a;
    static std::vector<float> rf, gf, bf;
    Imf::FrameBuffer fb;
    if (type == Imf::FLOAT) {
        rf.assign(size_t(W) * H, 0.0f); gf.assign(size_t(W) * H, 0.0f); bf.assign(size_t(W) * H, 0.0f);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            const size_t i = size_t(y) * W + x;
            rf[i] = float(x) / (W - 1); gf[i] = float(y) / (H - 1); bf[i] = 0.25f;
        }
        const char* names[3] = {"R", "G", "B"};
        std::vector<float>* d[3] = {&rf, &gf, &bf};
        for (int i = 0; i < 3; ++i) {
            hdr.channels().insert(names[i], Imf::Channel(Imf::FLOAT));
            fb.insert(names[i], Imf::Slice(Imf::FLOAT, reinterpret_cast<char*>(d[i]->data()),
                                           sizeof(float), sizeof(float) * W));
        }
    } else {
        fill(r, g, b, 1.0f);
        add_layer(hdr, fb, "", r, g, b);
    }
    if (with_alpha) {
        a.assign(size_t(W) * H, half(1.0f));
        hdr.channels().insert("A", Imf::Channel(Imf::HALF));
        fb.insert("A", Imf::Slice(Imf::HALF, reinterpret_cast<char*>(a.data()),
                                  sizeof(half), sizeof(half) * W));
    }
    write_scanline(path, hdr, fb);
}

// Y/RY/BY with the chroma planes subsampled 2x2, as EXR intends.
void make_luma_chroma(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::PIZ_COMPRESSION;
    hdr.channels().insert("Y", Imf::Channel(Imf::HALF));
    hdr.channels().insert("RY", Imf::Channel(Imf::HALF, 2, 2));
    hdr.channels().insert("BY", Imf::Channel(Imf::HALF, 2, 2));

    static std::vector<half> y(size_t(W) * H);
    static std::vector<half> ry(size_t(W / 2) * (H / 2), half(0.1f));
    static std::vector<half> by(size_t(W / 2) * (H / 2), half(-0.1f));
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) y[size_t(j) * W + i] = half(float(i) / (W - 1));

    Imf::FrameBuffer fb;
    fb.insert("Y", Imf::Slice(Imf::HALF, reinterpret_cast<char*>(y.data()),
                              sizeof(half), sizeof(half) * W));
    fb.insert("RY", Imf::Slice(Imf::HALF, reinterpret_cast<char*>(ry.data()),
                               sizeof(half), sizeof(half) * (W / 2), 2, 2));
    fb.insert("BY", Imf::Slice(Imf::HALF, reinterpret_cast<char*>(by.data()),
                               sizeof(half), sizeof(half) * (W / 2), 2, 2));
    write_scanline(path, hdr, fb);
}

void make_overscan(const std::string& path) {
    const Imath::Box2i display(Imath::V2i(0, 0), Imath::V2i(W - 1, H - 1));
    const Imath::Box2i data(Imath::V2i(-16, -16), Imath::V2i(W + 15, H + 15));
    Imf::Header hdr(display, data);
    hdr.compression() = Imf::ZIP_COMPRESSION;
    Imf::addChromaticities(hdr, acescg());
    const int dw = data.max.x - data.min.x + 1, dh = data.max.y - data.min.y + 1;
    static std::vector<half> r, g, b;
    fill(r, g, b, 1.0f, dw, dh);
    Imf::FrameBuffer fb;
    const char* names[3] = {"R", "G", "B"};
    std::vector<half>* d[3] = {&r, &g, &b};
    for (int i = 0; i < 3; ++i) {
        hdr.channels().insert(names[i], Imf::Channel(Imf::HALF));
        char* base = reinterpret_cast<char*>(d[i]->data())
                   - (ptrdiff_t(data.min.x) * ptrdiff_t(sizeof(half)))
                   - (ptrdiff_t(data.min.y) * ptrdiff_t(dw) * ptrdiff_t(sizeof(half)));
        fb.insert(names[i], Imf::Slice(Imf::HALF, base, sizeof(half), sizeof(half) * dw));
    }
    Imf::OutputFile file(path.c_str(), hdr);
    file.setFrameBuffer(fb);
    file.writePixels(dh);
    std::printf("  %s\n", path.c_str());
}

// Two parts: an AOV first, the beauty second. The selector must prefer part 1.
void make_multipart(const std::string& path) {
    std::vector<Imf::Header> headers(2);
    const char* names[2] = {"someAOV", "beauty"};
    for (int p = 0; p < 2; ++p) {
        headers[p] = Imf::Header(W, H);
        headers[p].setName(names[p]);
        headers[p].setType(Imf::SCANLINEIMAGE);
        headers[p].compression() = Imf::ZIP_COMPRESSION;
        Imf::addChromaticities(headers[p], acescg());
        for (const char* c : {"R", "G", "B"})
            headers[p].channels().insert(c, Imf::Channel(Imf::HALF));
    }
    Imf::MultiPartOutputFile file(path.c_str(), headers.data(), 2);
    static std::vector<half> r, g, b;
    for (int p = 0; p < 2; ++p) {
        fill(r, g, b, p == 0 ? 0.3f : 1.0f);
        Imf::FrameBuffer fb;
        const char* names3[3] = {"R", "G", "B"};
        std::vector<half>* d[3] = {&r, &g, &b};
        for (int i = 0; i < 3; ++i)
            fb.insert(names3[i], Imf::Slice(Imf::HALF, reinterpret_cast<char*>(d[i]->data()),
                                            sizeof(half), sizeof(half) * W));
        Imf::OutputPart part(file, p);
        part.setFrameBuffer(fb);
        part.writePixels(H);
    }
    std::printf("  %s\n", path.c_str());
}

// Deep scanline. Must degrade gracefully -- never crash, never be selected.
void make_deep(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.setType(Imf::DEEPSCANLINE);
    hdr.compression() = Imf::ZIPS_COMPRESSION;
    for (const char* c : {"R", "G", "B"}) hdr.channels().insert(c, Imf::Channel(Imf::HALF));
    hdr.channels().insert("Z", Imf::Channel(Imf::FLOAT));

    Imf::DeepScanLineOutputFile file(path.c_str(), hdr);
    std::vector<unsigned int> counts(W, 1);
    std::vector<half> r(W, half(0.5f)), g(W, half(0.5f)), b(W, half(0.5f));
    std::vector<float> z(W, 5.0f);
    std::vector<half*> rp(W), gp(W), bp(W);
    std::vector<float*> zp(W);
    for (int i = 0; i < W; ++i) { rp[i] = &r[i]; gp[i] = &g[i]; bp[i] = &b[i]; zp[i] = &z[i]; }

    Imf::DeepFrameBuffer fb;
    fb.insertSampleCountSlice(Imf::Slice(Imf::UINT,
        reinterpret_cast<char*>(counts.data()), sizeof(unsigned int), 0));
    fb.insert("R", Imf::DeepSlice(Imf::HALF, reinterpret_cast<char*>(rp.data()),
                                  sizeof(half*), 0, sizeof(half)));
    fb.insert("G", Imf::DeepSlice(Imf::HALF, reinterpret_cast<char*>(gp.data()),
                                  sizeof(half*), 0, sizeof(half)));
    fb.insert("B", Imf::DeepSlice(Imf::HALF, reinterpret_cast<char*>(bp.data()),
                                  sizeof(half*), 0, sizeof(half)));
    fb.insert("Z", Imf::DeepSlice(Imf::FLOAT, reinterpret_cast<char*>(zp.data()),
                                  sizeof(float*), 0, sizeof(float)));
    file.setFrameBuffer(fb);
    for (int y = 0; y < H; ++y) file.writePixels(1);
    std::printf("  %s\n", path.c_str());
}


// NaN and infinity, deliberately. Plan §6.4: handle explicitly, never
// propagate. Placed in known bands so a render can be checked by eye.
void make_nan_inf(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::ZIP_COMPRESSION;
    Imf::addChromaticities(hdr, acescg());
    static std::vector<half> r, g, b;
    fill(r, g, b, 1.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = size_t(y) * W + x;
            if (y < H / 4)                 r[i] = half(nan);
            else if (y < H / 2)            g[i] = half(inf);
            else if (y < 3 * H / 4)        b[i] = half(-inf);
        }
    }
    Imf::FrameBuffer fb;
    add_layer(hdr, fb, "", r, g, b);
    write_scanline(path, hdr, fb);
}

// Wildly out-of-gamut values, including negatives. These are the colours where
// the ACES 2.0 gamut compression does its most nonlinear work, and where a
// baked 3D LUT is least accurate -- see docs/aces-transform-options.md.
void make_out_of_gamut(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::ZIP_COMPRESSION;
    Imf::addChromaticities(hdr, acescg());
    static std::vector<half> r, g, b;
    r.assign(size_t(W) * H, half(0.f)); g = r; b = r;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = size_t(y) * W + x;
            const float t = float(x) / (W - 1) * 12.0f;   // up to 12 stops over
            switch ((y * 6) / H) {
                case 0: r[i] = half(t); break;                       // pure red
                case 1: g[i] = half(t); break;                       // pure green
                case 2: b[i] = half(t); break;                       // pure blue
                case 3: r[i] = half(t); g[i] = half(-0.2f * t); break;  // negative G
                case 4: b[i] = half(t); r[i] = half(-0.2f * t); break;  // negative R
                default: r[i] = half(t); g[i] = half(t); b[i] = half(t);
            }
        }
    }
    Imf::FrameBuffer fb;
    add_layer(hdr, fb, "", r, g, b);
    write_scanline(path, hdr, fb);
}

// A realistic AOV stack. Exercises the "request only the channels we need"
// saving in §6.4 -- 43 channels, of which the selector should read three.
void make_many_channels(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::DWAA_COMPRESSION;
    Imf::addChromaticities(hdr, acescg());
    static std::vector<std::vector<half>> store;
    store.clear();
    store.reserve(64);
    Imf::FrameBuffer fb;
    const char* aovs[] = {"diffuse", "specular", "sss", "transmission", "emission",
                          "coat", "sheen", "volume", "background", "albedo",
                          "CryptoObject00", "CryptoMaterial00", "normal", "position"};
    for (const char* a : aovs) {
        store.emplace_back(); store.emplace_back(); store.emplace_back();
        auto& rr = store[store.size() - 3];
        auto& gg = store[store.size() - 2];
        auto& bb2 = store[store.size() - 1];
        fill(rr, gg, bb2, 0.4f);
        add_layer(hdr, fb, a, rr, gg, bb2);
    }
    store.emplace_back(); store.emplace_back(); store.emplace_back();
    auto& r = store[store.size() - 3];
    auto& g = store[store.size() - 2];
    auto& b = store[store.size() - 1];
    fill(r, g, b, 1.0f);
    add_layer(hdr, fb, "", r, g, b);              // the one that must win
    write_scanline(path, hdr, fb);
}

// Nested dot-separated layer names, e.g. "character.diffuse.R".
void make_nested_layers(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::ZIP_COMPRESSION;
    static std::vector<half> ar, ag, ab, br, bg, bb2;
    fill(ar, ag, ab, 0.5f); fill(br, bg, bb2, 0.8f);
    Imf::FrameBuffer fb;
    add_layer(hdr, fb, "character.diffuse", ar, ag, ab);
    add_layer(hdr, fb, "character.specular", br, bg, bb2);
    write_scanline(path, hdr, fb);
}

// Single channel only -- must render as greyscale, not fail.
void make_single_channel(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::ZIP_COMPRESSION;
    hdr.channels().insert("Y", Imf::Channel(Imf::HALF));
    static std::vector<half> y(size_t(W) * H);
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) y[size_t(j) * W + i] = half(float(i) / (W - 1));
    Imf::FrameBuffer fb;
    fb.insert("Y", Imf::Slice(Imf::HALF, reinterpret_cast<char*>(y.data()),
                              sizeof(half), sizeof(half) * W));
    write_scanline(path, hdr, fb);
}

// Carries a `preview` attribute -- the §6.4 fast path for small thumbnails.
void make_with_preview(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::ZIP_COMPRESSION;
    Imf::addChromaticities(hdr, acescg());
    const int pw = 64, ph = 36;
    std::vector<Imf::PreviewRgba> px(size_t(pw) * ph);
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x)
            px[size_t(y) * pw + x] = Imf::PreviewRgba(
                (unsigned char)(255 * x / (pw - 1)), (unsigned char)(255 * y / (ph - 1)),
                64, 255);
    hdr.setPreviewImage(Imf::PreviewImage(pw, ph, px.data()));
    static std::vector<half> r, g, b;
    fill(r, g, b, 1.0f);
    Imf::FrameBuffer fb;
    add_layer(hdr, fb, "", r, g, b);
    write_scanline(path, hdr, fb);
}

// Tiled and mip-mapped -- §6.4 wants the smallest adequate mip level.
void make_tiled_mipmap(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::ZIP_COMPRESSION;
    Imf::addChromaticities(hdr, acescg());
    hdr.setType(Imf::TILEDIMAGE);
    hdr.setTileDescription(Imf::TileDescription(64, 64, Imf::MIPMAP_LEVELS));
    for (const char* c : {"R", "G", "B"}) hdr.channels().insert(c, Imf::Channel(Imf::HALF));

    Imf::TiledOutputFile file(path.c_str(), hdr);
    for (int level = 0; level < file.numLevels(); ++level) {
        const int lw = file.levelWidth(level), lh = file.levelHeight(level);
        std::vector<half> r, g, b;
        fill(r, g, b, 1.0f, lw, lh);
        Imf::FrameBuffer fb;
        const char* names[3] = {"R", "G", "B"};
        std::vector<half>* d[3] = {&r, &g, &b};
        for (int i = 0; i < 3; ++i)
            fb.insert(names[i], Imf::Slice(Imf::HALF, reinterpret_cast<char*>(d[i]->data()),
                                           sizeof(half), sizeof(half) * lw));
        file.setFrameBuffer(fb);
        file.writeTiles(0, file.numXTiles(level) - 1, 0, file.numYTiles(level) - 1, level);
    }
    std::printf("  %s\n", path.c_str());
}

// A float channel filled by `f(x, y)`. The storage outlives the write because
// `keep` owns it.
void add_float(Imf::Header& hdr, Imf::FrameBuffer& fb, std::vector<std::vector<float>>& keep,
               const std::string& name, float (*f)(int, int)) {
    keep.emplace_back(size_t(W) * H);
    std::vector<float>& v = keep.back();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) v[size_t(y) * W + x] = f(x, y);
    hdr.channels().insert(name, Imf::Channel(Imf::FLOAT));
    fb.insert(name, Imf::Slice(Imf::FLOAT, reinterpret_cast<char*>(v.data()),
                               sizeof(float), sizeof(float) * W));
}

float pos_x(int x, int) { return float(x) / float(W - 1); }
float pos_y(int, int y) { return float(y) / float(H - 1); }
float pos_z(int, int) { return 0.5f; }
float depth_value(int, int) { return 0.25f; }
float motion_u(int, int) { return 0.75f; }
float motion_v(int, int) { return 0.1f; }

// Beauty plus the data passes the preview must show raw: position (x/y/z
// components), depth, and two-component motion. Known values, so a test can
// check that they reach the screen untransformed.
void make_data_passes(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::ZIP_COMPRESSION;
    Imf::addChromaticities(hdr, acescg());
    static std::vector<half> r, g, b;
    fill(r, g, b, 1.0f);
    static std::vector<std::vector<float>> keep;
    keep.reserve(8);
    Imf::FrameBuffer fb;
    add_layer(hdr, fb, "", r, g, b);
    add_float(hdr, fb, keep, "P.x", pos_x);
    add_float(hdr, fb, keep, "P.y", pos_y);
    add_float(hdr, fb, keep, "P.z", pos_z);
    add_float(hdr, fb, keep, "depth.Z", depth_value);
    add_float(hdr, fb, keep, "motion.u", motion_u);
    add_float(hdr, fb, keep, "motion.v", motion_v);
    write_scanline(path, hdr, fb);
}

// Separate-AOV renders: one pass per file, nothing else in it.
void make_depth_only(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::ZIP_COMPRESSION;
    static std::vector<std::vector<float>> keep;
    keep.reserve(2);
    Imf::FrameBuffer fb;
    add_float(hdr, fb, keep, "Z", depth_value);
    write_scanline(path, hdr, fb);
}

void make_position_only(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::ZIP_COMPRESSION;
    static std::vector<std::vector<float>> keep;
    keep.reserve(4);
    Imf::FrameBuffer fb;
    add_float(hdr, fb, keep, "P.x", pos_x);
    add_float(hdr, fb, keep, "P.y", pos_y);
    add_float(hdr, fb, keep, "P.z", pos_z);
    write_scanline(path, hdr, fb);
}

// PQ-encoded (SMPTE ST 2084) greys, as an HDR master stores them: four
// vertical bands of known code values 0, 0.25, 0.5081 (100 nits), 0.75. No
// colour tag -- real PQ EXRs carry none either.
void make_pq_bands(const std::string& path) {
    Imf::Header hdr(W, H);
    hdr.compression() = Imf::ZIP_COMPRESSION;
    static std::vector<std::vector<float>> keep;
    keep.reserve(4);
    Imf::FrameBuffer fb;
    auto band = [](int x, int) {
        static const float codes[4] = {0.0f, 0.25f, 0.5081f, 0.75f};
        return codes[(x * 4) / W];
    };
    add_float(hdr, fb, keep, "R", band);
    add_float(hdr, fb, keep, "G", band);
    add_float(hdr, fb, keep, "B", band);
    write_scanline(path, hdr, fb);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "Tests/Fixtures/corpus";
    std::printf("writing fixtures to %s\n", dir.c_str());
    try {
        make_multilayer(dir + "/dwaa-multilayer-acescg.exr", Imf::DWAA_COMPRESSION);
        make_multilayer(dir + "/dwab-multilayer-acescg.exr", Imf::DWAB_COMPRESSION);
        make_aov_only(dir + "/aov-only-no-beauty.exr");
        make_named_beauty(dir + "/named-beauty-layer.exr");
        const Imf::Chromaticities ap0 = aces2065();
        make_simple(dir + "/aces2065-1.exr", Imf::ZIP_COMPRESSION, &ap0, true);
        make_simple(dir + "/no-chromaticities.exr", Imf::ZIP_COMPRESSION, nullptr, false);
        make_simple(dir + "/float32.exr", Imf::ZIP_COMPRESSION, nullptr, false, Imf::FLOAT);
        make_luma_chroma(dir + "/luminance-chroma.exr");
        make_overscan(dir + "/overscan.exr");
        make_multipart(dir + "/multipart.exr");
        make_deep(dir + "/deep-scanline.exr");
        make_nan_inf(dir + "/nan-inf.exr");
        make_out_of_gamut(dir + "/out-of-gamut.exr");
        make_many_channels(dir + "/aov-43-channels.exr");
        make_nested_layers(dir + "/nested-layer-names.exr");
        make_single_channel(dir + "/single-channel.exr");
        make_with_preview(dir + "/has-preview-attr.exr");
        make_tiled_mipmap(dir + "/tiled-mipmap.exr");
        make_data_passes(dir + "/data-passes.exr");
        make_pq_bands(dir + "/pq-bands.exr");
        // colorInteropID (OpenEXR 3.4). Same pixels as aces2065-1.exr and
        // no-chromaticities.exr, so renders can be compared byte for byte.
        make_simple(dir + "/interop-ap0.exr", Imf::ZIP_COMPRESSION, nullptr, true, Imf::HALF,
                    "lin_ap0_scene");
        make_simple(dir + "/interop-conflict.exr", Imf::ZIP_COMPRESSION, &ap0, true, Imf::HALF,
                    "lin_ap1_scene");
        make_simple(dir + "/interop-awg3.exr", Imf::ZIP_COMPRESSION, nullptr, false, Imf::HALF,
                    "ocio:lin_awg3_scene");
        make_simple(dir + "/interop-log.exr", Imf::ZIP_COMPRESSION, nullptr, false, Imf::HALF,
                    "ocio:acescct_ap1_scene");
        make_simple(dir + "/interop-data.exr", Imf::ZIP_COMPRESSION, nullptr, false, Imf::HALF,
                    "data");
        // arnold/color_space: Arnold writes its OCIO working space by name.
        make_simple(dir + "/arnold-ap0.exr", Imf::ZIP_COMPRESSION, nullptr, false, Imf::HALF,
                    nullptr, "ACES2065-1");
        make_simple(dir + "/arnold-alias.exr", Imf::ZIP_COMPRESSION, nullptr, false, Imf::HALF,
                    nullptr, "Utility - Linear - sRGB");     // ACES 1.x config spelling
        make_simple(dir + "/arnold-display.exr", Imf::ZIP_COMPRESSION, nullptr, false, Imf::HALF,
                    nullptr, "sRGB - Display");              // not scene-linear: ignored
        make_simple(dir + "/arnold-vs-interop.exr", Imf::ZIP_COMPRESSION, nullptr, false, Imf::HALF,
                    "lin_ap1_scene", "ACES2065-1");          // the interop ID wins
        make_simple(dir + "/arnold-linear.exr", Imf::ZIP_COMPRESSION, nullptr, false, Imf::HALF,
                    nullptr, "linear");      // Arnold's built-in manager: linear sRGB
        // Blender before 5.0: no colour tag, recognised by its metadata stamp
        // (values as Tears of Steel and Poly Haven files carry them).
        make_simple(dir + "/blender-4-stamp.exr", Imf::ZIP_COMPRESSION, nullptr, false, Imf::HALF,
                    nullptr, nullptr, {{"File", "/projects/shot/lighting.blend"},
                                       {"RenderTime", "00:13.14"}, {"Scene", "Scene"}});
        make_simple(dir + "/blender-4-multilayer.exr", Imf::ZIP_COMPRESSION, nullptr, false,
                    Imf::HALF, nullptr, nullptr,
                    {{"BlenderMultiChannel", "Blender V2.55.1 and newer"}});
        make_simple(dir + "/blender-4-cycles.exr", Imf::ZIP_COMPRESSION, nullptr, false, Imf::HALF,
                    nullptr, nullptr, {{"cycles.View Layer.samples", "128"}});
        // Blender 5.0+ names itself and tags its files: left to its own tags.
        make_simple(dir + "/blender-5-untagged.exr", Imf::ZIP_COMPRESSION, nullptr, false,
                    Imf::HALF, nullptr, nullptr,
                    {{"Software", "Blender 5.0.0"}, {"File", "/projects/shot/lighting.blend"}});
        make_simple(dir + "/blender-4-tagged.exr", Imf::ZIP_COMPRESSION, nullptr, false, Imf::HALF,
                    "lin_ap0_scene", nullptr, {{"File", "/projects/shot/lighting.blend"}});
        make_depth_only(dir + "/depth-only.exr");
        make_position_only(dir + "/position-only.exr");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAILED: %s\n", e.what());
        return 1;
    }
    return 0;
}

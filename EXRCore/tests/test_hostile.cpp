// Hostile-input regressions found in the 2026-10-08 adversarial review. Each
// case writes its own files with the real OpenEXR writer into the directory
// given on the command line.
//
// 1. File swapped between the header read and the decode: the decode sized
//    its buffers from the first header, and OpenEXR wrote past them.
// 2. Text from the file forging rows in the info panel.
// 3. A file listing far more layers than any menu can show.
#include "EXRCore/exr_api.h"
#include "EXRCore/exr_decode.h"
#include "EXRCore/exr_layers.h"
#include "EXRCore/exr_reader.h"

#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfMultiPartOutputFile.h>
#include <ImfOutputPart.h>
#include <ImfPartType.h>
#include <ImfRgbaFile.h>
#include <ImfStringAttribute.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;

static void expect(bool ok, const char* what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

static void write_rgba(const std::string& path, int w, int h, const char* interop = nullptr) {
    Imf::Header hd(w, h);
    if (interop) hd.insert("colorInteropID", Imf::StringAttribute(interop));
    std::vector<Imf::Rgba> px(std::size_t(w) * h, Imf::Rgba(0.18f, 0.18f, 0.18f, 1));
    Imf::RgbaOutputFile f(path.c_str(), hd, Imf::WRITE_RGBA);
    f.setFrameBuffer(px.data(), 1, w);
    f.writePixels(h);
}

static std::string describe(const std::string& path) {
    char buf[8192] = {};
    exr_describe(path.c_str(), buf, sizeof buf);
    return buf;
}

static int count_rows(const std::string& text, const char* label) {
    const std::string needle = std::string(label) + "\t";
    int n = 0;
    for (std::size_t at = 0; at < text.size();) {
        if (text.compare(at, needle.size(), needle) == 0) ++n;
        const std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    return n;
}

static void test_file_swapped_before_decode(const std::string& dir) {
    using namespace exrcore;
    const std::string p = dir + "/swap.exr";
    write_rgba(p, 16, 16);
    FileInfo info;
    std::string err;
    expect(inspect_file(p, info, err), "swap: the small file inspects");
    const LayerSelection sel = select_primary_layer(info.parts);
    write_rgba(p, 4096, 16);   // now 256x wider than the buffers will be
    Image img;
    const bool ok = decode_layer(p, info, sel, img, err);
    expect(!ok && err.find("changed") != std::string::npos,
           "swap: a wider file at decode time is refused, not written past the buffers");
}

static void test_info_rows_cannot_be_forged(const std::string& dir) {
    const std::string p = dir + "/forge.exr";
    write_rgba(p, 8, 8, "bogus\nColour\tRec.709 (verified)");
    const std::string d = describe(p);
    expect(count_rows(d, "Colour") == 1, "forge: a newline in a tag adds no row");
    expect(d.find("Colour\tRec.709 (verified)") == std::string::npos,
           "forge: the forged value is not a row of its own");

    const std::string q = dir + "/bidi.exr";
    write_rgba(q, 8, 8, "abc\xE2\x80\xAE" "def");   // U+202E RIGHT-TO-LEFT OVERRIDE
    expect(describe(q).find("abcdef") != std::string::npos,
           "forge: bidirectional overrides are removed");
}

static void test_layer_menu_is_bounded(const std::string& dir) {
    const std::string p = dir + "/many.exr";
    const int parts = 64, extra = 32;   // 64 x 33 = 2112 layer entries
    std::vector<Imf::Header> hs;
    for (int i = 0; i < parts; ++i) {
        Imf::Header h(1, 1);
        char n[32];
        std::snprintf(n, sizeof n, "part%02d", i);
        h.setName(n);
        h.setType(Imf::SCANLINEIMAGE);
        for (const char* c : {"R", "G", "B"}) h.channels().insert(c, Imf::Channel(Imf::HALF));
        for (int c = 0; c < extra; ++c) {
            std::snprintf(n, sizeof n, "pass%02d.Y", c);
            h.channels().insert(n, Imf::Channel(Imf::HALF));
        }
        hs.push_back(h);
    }
    {
        Imf::MultiPartOutputFile f(p.c_str(), hs.data(), parts);
        for (int i = 0; i < parts; ++i) {
            Imf::OutputPart o(f, i);
            Imf::FrameBuffer fb;
            o.setFrameBuffer(fb);
            o.writePixels(1);
        }
    }
    EXRSource* s = exr_open(p.c_str(), 64, 0, nullptr);
    expect(s != nullptr, "layers: the file opens");
    if (!s) return;
    const int n = exr_source_layer_count(s);
    expect(n > 0 && n <= 1024 + 2, "layers: the menu is capped near 1024 entries");
    bool active_listed = false;
    for (int i = 0; i < n; ++i) {
        if (std::strcmp(exr_source_layer_id(s, i), exr_source_active_layer(s)) == 0) active_listed = true;
    }
    expect(active_listed, "layers: the layer on screen is still listed");
    exr_close(s);

    // The cut must not hide a layer that a carried selection asks for by id.
    const std::string last = "63:pass31";   // ids are "part:layer"
    EXRSource* t = exr_open(p.c_str(), 64, 0, last.c_str());
    expect(t && std::string(exr_source_active_layer(t)).find("pass31") != std::string::npos,
           "layers: a carried layer beyond the cap still opens and is listed");
    if (t) {
        bool listed = false;
        for (int i = 0; i < exr_source_layer_count(t); ++i) {
            if (std::strcmp(exr_source_layer_id(t, i), exr_source_active_layer(t)) == 0) listed = true;
        }
        expect(listed, "layers: ...and appears in the menu");
        exr_close(t);
    }
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "build";
    test_file_swapped_before_decode(dir);
    test_info_rows_cannot_be_forged(dir);
    test_layer_menu_is_bounded(dir);
    std::printf("  hostile input: %d failures\n", failures);
    return failures ? 1 : 0;
}

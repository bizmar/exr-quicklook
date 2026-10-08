// Golden-image regression (plan §9). Every fixture is rendered at thumbnail
// size through the shipping pipeline, in the default view and in Raw, and
// compared with a reference PNG in Tests/Golden/. Raw pins down the decoder
// alone; the default view adds the matrix, shaper and LUT.
//
//   test_golden <golden-dir> <fixture-dir>... [--update]
//
// A pixel may differ by at most kTolerance (1/255 of full scale) in any
// channel, enough for floating-point noise between CPUs, never for a real
// change. Files that must not render are recorded in MANIFEST.txt as
// "rejected", so a file that starts or stops rendering fails too.
//
// --update rewrites the references. Review the changed PNGs in the diff the
// way Tests/Fixtures/realworld/expected.txt is reviewed: each one is a real
// file whose output your change altered.
//
// The PNGs are 16-bit RGB holding Display P3 code values (tagged with a cICP
// chunk), written and read by the small codec below so that no colour
// management touches the numbers on the way in or out.
#include "EXRCore/exr_api.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kEdge = 256;
constexpr int kTolerance = 257;   // 1/255 in 16-bit code values

struct Image {
    int w = 0, h = 0;
    std::vector<uint16_t> rgb;   // w * h * 3
};

// ---- minimal PNG, 16-bit RGB, filter "Up" on every row ----

void put32(std::string& s, uint32_t v) {
    for (int i = 3; i >= 0; --i) s += char((v >> (i * 8)) & 0xFF);
}

void chunk(std::string& out, const char* type, const std::string& data) {
    put32(out, uint32_t(data.size()));
    std::string body = std::string(type, 4) + data;
    out += body;
    put32(out, uint32_t(crc32(0, reinterpret_cast<const Bytef*>(body.data()), uInt(body.size()))));
}

bool write_png(const std::string& path, const Image& img) {
    const std::size_t row = std::size_t(img.w) * 6;
    std::vector<uint8_t> raw((row + 1) * img.h);
    std::vector<uint8_t> prev(row, 0), cur(row);
    for (int y = 0; y < img.h; ++y) {
        for (int i = 0; i < img.w * 3; ++i) {
            const uint16_t v = img.rgb[std::size_t(y) * img.w * 3 + i];
            cur[i * 2] = uint8_t(v >> 8);
            cur[i * 2 + 1] = uint8_t(v & 0xFF);
        }
        uint8_t* dst = &raw[y * (row + 1)];
        dst[0] = 2;   // Up
        for (std::size_t i = 0; i < row; ++i) dst[1 + i] = uint8_t(cur[i] - prev[i]);
        prev = cur;
    }
    uLongf zlen = compressBound(uLong(raw.size()));
    std::string z(zlen, '\0');
    if (compress2(reinterpret_cast<Bytef*>(&z[0]), &zlen, raw.data(), uLong(raw.size()), 9) != Z_OK)
        return false;
    z.resize(zlen);

    std::string png("\x89PNG\r\n\x1a\n", 8), ihdr;
    put32(ihdr, uint32_t(img.w));
    put32(ihdr, uint32_t(img.h));
    ihdr += char(16); ihdr += char(2); ihdr += char(0); ihdr += char(0); ihdr += char(0);
    chunk(png, "IHDR", ihdr);
    chunk(png, "cICP", std::string("\x0c\x0d\x00\x01", 4));   // P3 D65, sRGB transfer, full range
    chunk(png, "IDAT", z);
    chunk(png, "IEND", "");
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(png.data(), std::streamsize(png.size()));
    return bool(f);
}

uint32_t get32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

// Reads only what write_png writes. Anything else is reported as unreadable.
bool read_png(const std::string& path, Image& img) {
    std::ifstream f(path, std::ios::binary);
    const std::string s((std::istreambuf_iterator<char>(f)), {});
    if (s.size() < 8 || s.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) != 0) return false;
    const auto* p = reinterpret_cast<const uint8_t*>(s.data());
    std::size_t at = 8;
    std::string z;
    while (at + 12 <= s.size()) {
        const uint32_t len = get32(p + at);
        if (at + 12 + len > s.size()) return false;
        const std::string type(s, at + 4, 4);
        const uint8_t* d = p + at + 8;
        if (type == "IHDR") {
            img.w = int(get32(d)); img.h = int(get32(d + 4));
            if (d[8] != 16 || d[9] != 2 || img.w <= 0 || img.h <= 0 || img.w > 4096 || img.h > 4096) return false;
        } else if (type == "IDAT") {
            z.append(reinterpret_cast<const char*>(d), len);
        }
        at += 12 + len;
    }
    const std::size_t row = std::size_t(img.w) * 6;
    std::vector<uint8_t> raw((row + 1) * img.h);
    uLongf rawlen = uLongf(raw.size());
    if (uncompress(raw.data(), &rawlen, reinterpret_cast<const Bytef*>(z.data()), uLong(z.size())) != Z_OK ||
        rawlen != raw.size())
        return false;
    img.rgb.assign(std::size_t(img.w) * img.h * 3, 0);
    std::vector<uint8_t> prev(row, 0), cur(row);
    for (int y = 0; y < img.h; ++y) {
        const uint8_t* src = &raw[y * (row + 1)];
        if (src[0] != 0 && src[0] != 2) return false;
        for (std::size_t i = 0; i < row; ++i) cur[i] = uint8_t(src[1 + i] + (src[0] == 2 ? prev[i] : 0));
        for (int i = 0; i < img.w * 3; ++i)
            img.rgb[std::size_t(y) * img.w * 3 + i] = uint16_t((cur[i * 2] << 8) | cur[i * 2 + 1]);
        prev = cur;
    }
    return true;
}

// ---- rendering ----

bool render(const std::string& path, const char* view, Image& out) {
    EXRRenderOptions o{};
    o.max_edge = kEdge;
    o.view = view;
    EXRRenderResult r{};
    if (!exr_render(path.c_str(), &o, &r)) return false;
    out.w = r.width; out.h = r.height;
    out.rgb.resize(std::size_t(r.width) * r.height * 3);
    for (std::size_t i = 0, n = std::size_t(r.width) * r.height; i < n; ++i) {
        out.rgb[i * 3 + 0] = r.pixels[i * 4 + 0];
        out.rgb[i * 3 + 1] = r.pixels[i * 4 + 1];
        out.rgb[i * 3 + 2] = r.pixels[i * 4 + 2];
    }
    exr_render_free(&r);
    return true;
}

std::vector<std::string> exr_files(const std::string& dir) {
    std::vector<std::string> out;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".exr") == 0) out.push_back(dir + "/" + n);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string base(const std::string& p) { return p.substr(p.find_last_of('/') + 1); }

}  // namespace

int main(int argc, char** argv) {
    bool update = false;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--update") == 0) update = true;
        else args.push_back(argv[i]);
    }
    if (args.size() < 2) {
        std::fprintf(stderr, "usage: test_golden <golden-dir> <fixture-dir>... [--update]\n");
        return 2;
    }
    const std::string golden = args[0];
    const std::string manifest_path = golden + "/MANIFEST.txt";

    // "name view status" lines: status is "png" or "rejected".
    std::map<std::string, std::string> expected;
    {
        std::ifstream m(manifest_path);
        std::string line;
        while (std::getline(m, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            std::string name, view, status;
            if (ls >> name >> view >> status) expected[name + " " + view] = status;
        }
    }

    struct View { const char* id; const char* tag; };
    const View views[] = {{nullptr, "default"}, {"raw", "raw"}};

    std::ostringstream manifest;
    manifest << "# Golden images: Tools/test-all.sh checks these; regenerate with\n"
                "#   build/test_golden Tests/Golden <fixture dirs> --update\n"
                "# and review every changed PNG. <file> <view> <png|rejected>\n";
    int failures = 0, checked = 0, max_seen = 0;
    for (std::size_t a = 1; a < args.size(); ++a) {
        for (const std::string& file : exr_files(args[a])) {
            const std::string name = base(file);
            for (const View& v : views) {
                const std::string key = name + " " + v.tag;
                const std::string png = golden + "/" + name + "." + v.tag + ".png";
                Image got;
                const bool ok = render(file, v.id, got);
                manifest << name << " " << v.tag << " " << (ok ? "png" : "rejected") << "\n";
                ++checked;
                if (update) {
                    if (ok && !write_png(png, got)) { std::printf("  FAIL  cannot write %s\n", png.c_str()); ++failures; }
                    continue;
                }
                const auto it = expected.find(key);
                const std::string want = it == expected.end() ? "(no reference)" : it->second;
                if (want != (ok ? "png" : "rejected")) {
                    std::printf("  FAIL  %-40s %-8s now %s, reference says %s\n", name.c_str(), v.tag,
                                ok ? "renders" : "is rejected", want.c_str());
                    ++failures;
                    continue;
                }
                if (!ok) continue;
                Image ref;
                if (!read_png(png, ref)) { std::printf("  FAIL  %-40s %-8s reference unreadable\n", name.c_str(), v.tag); ++failures; continue; }
                if (ref.w != got.w || ref.h != got.h) {
                    std::printf("  FAIL  %-40s %-8s %dx%d, reference %dx%d\n", name.c_str(), v.tag, got.w, got.h, ref.w, ref.h);
                    ++failures;
                    continue;
                }
                int worst = 0;
                std::size_t over = 0;
                for (std::size_t i = 0; i < got.rgb.size(); ++i) {
                    const int d = std::abs(int(got.rgb[i]) - int(ref.rgb[i]));
                    worst = std::max(worst, d);
                    if (d > kTolerance) ++over;
                }
                max_seen = std::max(max_seen, worst);
                if (over) {
                    std::printf("  FAIL  %-40s %-8s %zu values off by up to %.1f/255\n", name.c_str(), v.tag,
                                over, worst / 257.0);
                    ++failures;
                }
            }
        }
    }
    for (const auto& kv : expected) {
        if (manifest.str().find("\n" + kv.first + " ") == std::string::npos) {
            std::printf("  FAIL  %s is in the manifest but no longer among the fixtures\n", kv.first.c_str());
            ++failures;
        }
    }
    if (update) {
        std::ofstream(manifest_path, std::ios::trunc) << manifest.str();
        std::printf("  golden: wrote %d references to %s\n", checked, golden.c_str());
        return failures ? 1 : 0;
    }
    std::printf("  golden: %d renders compared, largest difference %.2f/255, %d failures\n", checked,
                max_seen / 257.0, failures);
    return failures ? 1 : 0;
}

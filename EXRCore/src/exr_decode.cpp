#include "EXRCore/exr_decode.h"

#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfInputPart.h>
#include <ImfMultiPartInputFile.h>
#include <ImfThreading.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>

namespace exrcore {
namespace {

// Values a scene-linear pipeline can carry without poisoning downstream maths.
// Plan §6.4: handle NaN and infinity explicitly, never propagate.
constexpr float kMaxFinite = 65504.0f;  // half max, so this survives a half round-trip

inline float sanitise(float v) {
    if (std::isnan(v)) return 0.0f;
    if (v > kMaxFinite) return kMaxFinite;
    if (v < -kMaxFinite) return -kMaxFinite;
    return v;
}

// One decoded channel plane covering the data window.
struct Plane {
    std::vector<float> data;
    bool present = false;
};

// Points one channel's slice at a band-sized buffer. `band_y0` is the image
// row the buffer's first row corresponds to, so the notional (0,0) base is
// offset by both the data window origin and the band start.
void point_slice(Imf::FrameBuffer& fb, const std::string& channel, std::vector<float>& buf,
                 int64_t dw_w, int32_t dx0, int64_t band_y0, float fill) {
    if (channel.empty()) return;
    char* base = reinterpret_cast<char*>(buf.data())
               - (static_cast<std::ptrdiff_t>(dx0) * static_cast<std::ptrdiff_t>(sizeof(float)))
               - (static_cast<std::ptrdiff_t>(band_y0) * static_cast<std::ptrdiff_t>(dw_w)
                  * static_cast<std::ptrdiff_t>(sizeof(float)));
    fb.insert(channel, Imf::Slice(Imf::FLOAT, base, sizeof(float),
                                  static_cast<std::size_t>(dw_w) * sizeof(float),
                                  1, 1, fill));
}

// Wall-clock budget for a decode. OpenEXR has no cancellation API, so the
// deadline is enforced between scanline bands: a pathological file costs at
// most one band of overrun, not an unbounded hang. Plan §6.6/§6.7.
class Deadline {
public:
    explicit Deadline(int64_t millis)
        : end_(std::chrono::steady_clock::now() + std::chrono::milliseconds(millis)) {}
    [[nodiscard]] bool expired() const { return std::chrono::steady_clock::now() > end_; }

private:
    std::chrono::steady_clock::time_point end_;
};

// Scanlines per read. Small enough that the deadline is checked often, large
// enough that OpenEXR's own threading still has work to chew on.
constexpr int64_t kBandHeight = 64;

}  // namespace

bool decode_layer(const std::string& path, const FileInfo& info,
                  const LayerSelection& sel, Image& out, std::string& error,
                  const DecodeOptions& options) {
    error.clear();
    if (!sel.valid()) { error = "no selectable layer"; return false; }
    if (sel.part_index < 0 || sel.part_index >= static_cast<int>(info.details.size())) {
        error = "part index out of range";
        return false;
    }
    if (sel.kind == LayerKind::kLumaChroma) {
        // Y/RY/BY needs chroma upsampling; not implemented yet. Fail loudly
        // rather than render something wrong.
        error = "luminance-chroma layers are not decoded yet";
        return false;
    }

    const PartDetail& detail = info.details[sel.part_index];
    const Box2i& dw = detail.data_window;
    // The output frame: the display window by default (D6), or the data window
    // when the user has explicitly asked to see the overscan.
    const Box2i& disp = options.use_data_window ? detail.data_window
                                                : detail.display_window;

    int64_t dw_w = 0, dw_h = 0, disp_w = 0, disp_h = 0;
    if (!dw.extent(dw_w, dw_h) || !disp.extent(disp_w, disp_h)) {
        error = "window rejected by limits";
        return false;
    }
    int64_t dw_pixels = 0;
    if (!dw.pixel_count(dw_pixels)) { error = "dataWindow pixel count over limit"; return false; }

    // Target size. Downsampling is integrated into the band walk below, so the
    // full-resolution image is never held.
    int64_t out_w = disp_w, out_h = disp_h;
    if (options.max_edge > 0 && (disp_w > options.max_edge || disp_h > options.max_edge)) {
        const double scale = static_cast<double>(options.max_edge) /
                             static_cast<double>(std::max(disp_w, disp_h));
        out_w = std::max<int64_t>(1, static_cast<int64_t>(disp_w * scale));
        out_h = std::max<int64_t>(1, static_cast<int64_t>(disp_h * scale));
    }

    // Only the output image and one band of scanlines are ever allocated.
    // Both are bounds-checked before a single byte is reserved -- this is the
    // CVE-2026-28977 failure mode, so it is checked, not assumed.
    int64_t disp_pixels = 0, out_pixels = 0, out_bytes = 0, band_bytes = 0;
    if (!disp.pixel_count(disp_pixels)) {
        error = "displayWindow pixel count over limit";
        return false;
    }
    if (!checked_mul(out_w, out_h, out_pixels) ||
        !buffer_bytes(out_pixels, 4, sizeof(float), out_bytes)) {
        error = "output buffer exceeds memory ceiling";
        return false;
    }
    int64_t band_samples_checked = 0;
    if (!checked_mul(dw_w, kBandHeight, band_samples_checked) ||
        !buffer_bytes(band_samples_checked, 4, sizeof(float), band_bytes)) {
        error = "band buffer exceeds memory ceiling";
        return false;
    }
    (void)dw_pixels;

    try {
        Imf::setGlobalThreadCount(Limits::kDecodeThreads);
        Imf::MultiPartInputFile file(path.c_str());
        Imf::InputPart part(file, sel.part_index);

        // The file is opened again here, so it may no longer be the one
        // inspect_file() read: rewritten by a renderer since, or served
        // differently on each open by a hostile network share. Every buffer
        // below is sized from the first header and OpenEXR writes wherever the
        // slices point, so a wider data window now would write past them --
        // demonstrated under AddressSanitizer. Decode only the file we sized.
        const Imath::Box2i now = part.header().dataWindow();
        if (now.min.x != dw.min_x || now.min.y != dw.min_y ||
            now.max.x != dw.max_x || now.max.y != dw.max_y) {
            error = "file changed since its header was read";
            return false;
        }

        out.width = static_cast<int32_t>(out_w);
        out.height = static_cast<int32_t>(out_h);
        out.rgba.assign(static_cast<std::size_t>(out_pixels) * 4, 0.0f);
        // Box-filter accumulation: every source pixel adds into exactly one
        // destination cell, and the sums are normalised once at the end.
        std::vector<uint32_t> counts(static_cast<std::size_t>(out_pixels), 0);
        const double x_scale = static_cast<double>(out_w) / static_cast<double>(disp_w);
        const double y_scale = static_cast<double>(out_h) / static_cast<double>(disp_h);

        const std::size_t band_samples = static_cast<std::size_t>(band_samples_checked);
        std::vector<float> pr(band_samples), pg(band_samples), pb(band_samples),
                           pa(band_samples);

        // Only the rows the display window actually needs are read. On an
        // overscan file that skips the wasted border entirely.
        const int64_t y_begin = std::max<int64_t>(disp.min_y, dw.min_y);
        const int64_t y_end   = std::min<int64_t>(disp.max_y, dw.max_y);
        const int64_t x_begin = std::max<int64_t>(disp.min_x, dw.min_x);
        const int64_t x_end   = std::min<int64_t>(disp.max_x, dw.max_x);

        // A display window that does not intersect the data window is legal and
        // must render as an entirely background-filled frame, not an error.
        // openexr-images DisplayWindow/README.rst is explicit about this for
        // t09-t12: "The display window and the data window do not overlap. The
        // entire display window should be filled with the background color."
        // The band loop below simply does not execute, leaving the zero-filled
        // output, which is exactly that. Rejecting here was tried and was wrong.
        Deadline deadline(Limits::kDeadlineMillis);
        for (int64_t y0 = y_begin; y0 <= y_end; y0 += kBandHeight) {
            if (deadline.expired()) { error = "decode deadline exceeded"; return false; }
            const int64_t y1 = std::min<int64_t>(y0 + kBandHeight - 1, y_end);

            std::fill(pr.begin(), pr.end(), 0.0f);
            std::fill(pg.begin(), pg.end(), 0.0f);
            std::fill(pb.begin(), pb.end(), 0.0f);
            std::fill(pa.begin(), pa.end(), 1.0f);

            // Request only this layer's channels (§6.4). On a 40-channel AOV
            // file this is the difference between fast and unusable.
            // A greyscale layer names one channel for all three. A FrameBuffer
            // holds one slice per name -- a second insert replaces the first --
            // so read it once and reuse it, or red and green stay zero.
            const bool g_is_r = !sel.g.empty() && sel.g == sel.r;
            const bool b_is_r = !sel.b.empty() && sel.b == sel.r;
            Imf::FrameBuffer fb;
            point_slice(fb, sel.r, pr, dw_w, dw.min_x, y0, 0.0f);
            if (!g_is_r) point_slice(fb, sel.g, pg, dw_w, dw.min_x, y0, 0.0f);
            if (!b_is_r) point_slice(fb, sel.b, pb, dw_w, dw.min_x, y0, 0.0f);
            point_slice(fb, sel.a, pa, dw_w, dw.min_x, y0, 1.0f);
            part.setFrameBuffer(fb);
            part.readPixels(static_cast<int>(y0), static_cast<int>(y1));

            const std::vector<float>& sg = g_is_r ? pr : pg;
            const std::vector<float>& sb = b_is_r ? pr : pb;
            const bool have_r = !sel.r.empty(), have_g = !sel.g.empty();
            const bool have_b = !sel.b.empty(), have_a = !sel.a.empty();
            for (int64_t y = y0; y <= y1; ++y) {
                const int64_t src_row = (y - y0) * dw_w;
                int64_t dy = static_cast<int64_t>((y - disp.min_y) * y_scale);
                if (dy >= out_h) dy = out_h - 1;
                const int64_t dst_row = dy * out_w;
                for (int64_t x = x_begin; x <= x_end; ++x) {
                    const std::size_t si = static_cast<std::size_t>(src_row + (x - dw.min_x));
                    int64_t dx = static_cast<int64_t>((x - disp.min_x) * x_scale);
                    if (dx >= out_w) dx = out_w - 1;
                    const std::size_t di = static_cast<std::size_t>((dst_row + dx) * 4);
                    out.rgba[di + 0] += sanitise(have_r ? pr[si] : 0.0f);
                    out.rgba[di + 1] += sanitise(have_g ? sg[si] : 0.0f);
                    out.rgba[di + 2] += sanitise(have_b ? sb[si] : 0.0f);
                    out.rgba[di + 3] += sanitise(have_a ? pa[si] : 1.0f);
                    ++counts[static_cast<std::size_t>(dst_row + dx)];
                }
            }
        }

        for (std::size_t i = 0; i < counts.size(); ++i) {
            if (counts[i] == 0) continue;  // outside the data window; stays black
            const float inv = 1.0f / static_cast<float>(counts[i]);
            out.rgba[i * 4 + 0] *= inv;
            out.rgba[i * 4 + 1] *= inv;
            out.rgba[i * 4 + 2] *= inv;
            out.rgba[i * 4 + 3] *= inv;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "unknown error while decoding";
        return false;
    }
}

}  // namespace exrcore

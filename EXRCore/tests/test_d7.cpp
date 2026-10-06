// D7: thumbnail and preview must be pixel-identical with no override active.
//
// Both extensions funnel through exr_render, but they spell the default view
// differently -- the thumbnail passes NULL, the preview passes the explicit
// default id. If those ever resolve differently, D7 breaks silently. That is
// the invariant worth testing, so it is tested rather than assumed.
#include "EXRCore/exr_api.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;

static bool render(const char* path, const char* view, int max_edge, float stops,
                   std::vector<uint16_t>& out, int32_t& w, int32_t& h) {
    EXRRenderOptions opt{};
    opt.max_edge = max_edge;
    opt.exposure_stops = stops;
    opt.view = view;
    EXRRenderResult r{};
    if (!exr_render(path, &opt, &r)) return false;
    w = r.width; h = r.height;
    out.assign(r.pixels, r.pixels + size_t(r.width) * r.height * 4);
    exr_render_free(&r);
    return true;
}

static void check(bool ok, const char* what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "Tests/Fixtures/corpus/dwaa-multilayer-acescg.exr";

    std::vector<uint16_t> thumb, preview;
    int32_t tw = 0, th = 0, pw = 0, ph = 0;

    // Thumbnail spells the default as NULL; preview spells it explicitly.
    const bool a = render(path, nullptr, 512, 0.0f, thumb, tw, th);
    const bool b = render(path, exr_default_view_id(), 512, 0.0f, preview, pw, ph);
    check(a && b, "both render paths succeed");
    if (!(a && b)) return 1;

    check(tw == pw && th == ph, "same dimensions");
    check(thumb.size() == preview.size(), "same buffer size");
    check(std::memcmp(thumb.data(), preview.data(), thumb.size() * sizeof(uint16_t)) == 0,
          "D7: NULL view and explicit default view are pixel-identical");

    // Determinism: the same request twice must give the same bytes.
    std::vector<uint16_t> again;
    int32_t aw = 0, ah = 0;
    render(path, nullptr, 512, 0.0f, again, aw, ah);
    check(again == thumb, "render is deterministic");

    // And an override must actually change something, or the badge would lie.
    std::vector<uint16_t> exposed;
    render(path, nullptr, 512, 1.0f, exposed, aw, ah);
    check(exposed != thumb, "an exposure override changes the output");

    // The preview decodes once and re-transforms; the thumbnail renders
    // one-shot. Those are different code paths reaching the same pixels, and
    // the parallelised transform is exactly where that could silently break.
    {
        EXRRenderOptions o{};
        o.max_edge = 512;
        o.exposure_stops = 1.5f;
        EXRRenderResult one{}, cached{};
        const bool a1 = exr_render(path, &o, &one) != 0;
        EXRSource* src = exr_open(path, 512, 0, nullptr);
        const bool a2 = src && exr_source_render(src, &o, &cached) != 0;
        check(a1 && a2, "one-shot and cached paths both render");
        if (a1 && a2) {
            const size_t n = size_t(one.width) * one.height * 4;
            check(one.width == cached.width && one.height == cached.height &&
                  std::memcmp(one.pixels, cached.pixels, n * sizeof(uint16_t)) == 0,
                  "cached re-render is byte-identical to one-shot");
        }
        exr_render_free(&one);
        exr_render_free(&cached);
        if (src) exr_close(src);
    }

    // Same class of hazard as the view id: the thumbnail asks for the layer
    // implicitly (NULL) while the preview passes the active id explicitly. If
    // those ever resolved differently, D7 would break silently.
    {
        EXRSource* a = exr_open(path, 512, 0, nullptr);
        const char* active = a ? exr_source_active_layer(a) : "";
        EXRSource* b = exr_open(path, 512, 0, active);
        std::vector<uint16_t> pa, pb;
        int32_t aw = 0, ah = 0, bw = 0, bh = 0;
        bool ok = a && b;
        if (ok) {
            EXRRenderOptions o{};
            EXRRenderResult ra{}, rb{};
            ok = exr_source_render(a, &o, &ra) && exr_source_render(b, &o, &rb);
            if (ok) {
                aw = ra.width; ah = ra.height; bw = rb.width; bh = rb.height;
                pa.assign(ra.pixels, ra.pixels + size_t(aw) * ah * 4);
                pb.assign(rb.pixels, rb.pixels + size_t(bw) * bh * 4);
                exr_render_free(&ra); exr_render_free(&rb);
            }
        }
        check(ok && pa == pb && aw == bw && ah == bh,
              "D7: implicit and explicit layer selection are pixel-identical");
        if (a) exr_close(a);
        if (b) exr_close(b);
    }

    std::printf("\n  D7: %d failures\n", failures);
    return failures ? 1 : 0;
}

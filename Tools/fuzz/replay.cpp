// Replays files through the same public-API calls as the fuzzer, without
// mutating them: for reproducing a crasher, and checking its fix.
//   build by Tools/fuzz.sh; run: build/fuzz/replay <file>...
#include "EXRCore/exr_api.h"
#include <cstdio>
#include <initializer_list>
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        for (int max_edge : {256, 0}) {
            for (int dw : {0, 1}) {
                if (EXRSource* src = exr_open(argv[i], max_edge, dw, nullptr)) {
                    EXRRenderOptions o{};
                    EXRRenderResult r{};
                    if (exr_source_render(src, &o, &r)) exr_render_free(&r);
                    for (int l = 0; l < exr_source_layer_count(src) && l < 8; ++l) {
                        if (EXRSource* s2 = exr_open(argv[i], max_edge, dw, exr_source_layer_id(src, l))) exr_close(s2);
                    }
                    exr_close(src);
                }
            }
        }
        char buf[8192];
        exr_describe(argv[i], buf, sizeof buf);
        std::printf("replayed %s\n", argv[i]);
    }
    return 0;
}

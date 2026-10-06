// Assumed-input colourspaces for D9(a), generated from the OCIO ACES 2.0
// Studio config by Tools/bake-lut/gen_spaces.py.
//
// The set mirrors what a Nuke or Resolve user expects to pick from: ACES, the
// common display gamuts, then camera vendor gamuts. Only scene-linear spaces
// appear — the override supplies chromaticities to the primaries matrix, and
// EXR pixel data is scene-linear, so log encodings such as ACEScct or S-Log3
// would be wrong here however familiar they look in a grading application.
#pragma once

#include <cstring>

namespace exrcore {

struct NamedSpace {
    const char* id;
    const char* label;
    const char* interop_id;  // Color Interop Forum ID, as OCIO assigns it
    float chroma[8];  // rx ry gx gy bx by wx wy
};

extern const NamedSpace kSpaces[];
extern const int kSpaceCount;

// The space a file's colorInteropID names, or nullptr. Only the scene-linear
// spaces are in the table, so a log or display-encoded ID ("ocio:acescct_ap1_scene",
// "srgb_rec709_display") is deliberately not found: its primaries alone would
// render it wrongly.
[[nodiscard]] inline const NamedSpace* find_space_by_interop(const char* interop_id) {
    if (!interop_id || !*interop_id) return nullptr;
    for (int i = 0; i < kSpaceCount; ++i) {
        if (std::strcmp(kSpaces[i].interop_id, interop_id) == 0) return &kSpaces[i];
    }
    return nullptr;
}

}  // namespace exrcore

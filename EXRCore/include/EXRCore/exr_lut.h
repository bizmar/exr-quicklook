// Baked ACES 2.0 output transforms. Plan §7 / decision D3.
//
// The tables are generated at build time by Tools/bake-lut/bake.py and compiled
// in, so the shipped extension links no colour-management library (D4) and
// cannot fail by losing a resource file.
//
// Runtime pipeline, after decode and the primaries matrix:
//     linear AP1 -> ACEScct encode -> tetrahedral lookup -> display code values
#pragma once

#include <cstdint>
#include <string>

namespace exrcore {

struct BakedLut {
    const char* name;       // e.g. "aces2_p3d65_sdr100"
    const char* view;       // ACES view name, for the metadata panel
    const char* display;    // OCIO display name
    int size;               // cube edge, e.g. 65
    const std::uint16_t* data;  // size^3 fp16 RGB triples, red varying fastest
    const char* sha256;     // hash of the canonical binary, see lut-provenance.md
};

extern const BakedLut kBakedLuts[];
extern const int kBakedLutCount;

// Input transforms for PQ (ST 2084) HDR masters: the inverse of an ACES 2.0 HDR
// output transform, baked from OCIO. Domain: PQ code values in [0,1] (no
// shaper). Output: scene-linear ACEScg. Offered in the input-colourspace
// picker; never applied unless chosen.
struct InputLut {
    BakedLut lut;
    const char* label;      // for the picker, e.g. "P3-D65 PQ (HDR master, 1000 nits)"
};
extern const InputLut kInputLuts[];
extern const int kInputLutCount;

[[nodiscard]] const InputLut* find_input_lut(const char* name);

// `r`, `g`, `b` are PQ code values on entry (clamped to [0,1]) and
// scene-linear ACEScg on exit.
void apply_input_lut(const BakedLut& lut, float& r, float& g, float& b);

// Looks a LUT up by name. Returns nullptr if absent -- callers must treat that
// as "fall back to the generic icon", never as "render untransformed".
[[nodiscard]] const BakedLut* find_lut(const std::string& name);

// The shipped default (decision D5).
inline constexpr const char* kDefaultLutName = "aces2_p3d65_sdr100";

// ACEScct encode: the shaper that maps scene-linear AP1 onto the LUT's [0,1]
// domain. Exposed for tests.
[[nodiscard]] float acescct_encode(float linear);

// Applies the transform in place. `r`, `g`, `b` are scene-linear AP1 on entry
// and display code values in [0,1] on exit.
void apply_lut(const BakedLut& lut, float& r, float& g, float& b);

}  // namespace exrcore

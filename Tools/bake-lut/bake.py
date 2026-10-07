#!/usr/bin/env python3
"""
Bakes the ACES 2.0 output transforms to 3D LUTs. Build-time only -- the shipped
extension links no colour-management library at all (decision D4).

Requires PyOpenColorIO. On this machine that means Homebrew's python3.14:
    /opt/homebrew/bin/python3.14 Tools/bake-lut/bake.py

Emits, per view:
  EXRCore/Resources/<name>.lut     canonical binary, hashed for provenance
and one generated C++ file holding all of them:
  EXRCore/src/exr_lut_data.cpp

The LUT domain is ACEScct-encoded AP1. Decode produces linear AP1 (after the
chromaticities matrix), so the runtime pipeline is:
    linear AP1  ->  ACEScct encode  ->  tetrahedral lookup  ->  display code values
"""
import hashlib
import pathlib
import struct
import sys

try:
    import numpy as np
    import PyOpenColorIO as ocio
except ImportError as e:
    sys.exit(f"needs numpy and PyOpenColorIO ({e}).\n"
             f"Try: /opt/homebrew/bin/python3.14 {' '.join(sys.argv)}")

CONFIG = "studio-config-v4.0.0_aces-v2.0_ocio-v2.5"
INPUT_SPACE = "ACEScg"

# name, display, view, cube size. The default is 65^3; alternates are 33^3,
# which docs/aces-transform-options.md §4 shows is adequate for them.
# Every SDR view the Studio config offers, default first.
#
# Excluded alongside the HDR views: SDR views on an HDR-encoded display
# ("Display P3 HDR", "Rec.2100-PQ", "ST2084-P3-D65"). The view is SDR but the
# display encoding is not, so the output would still be PQ/HLG.
#
# The Studio config also carries 15 HDR views (PQ and HLG, 500-4000 nits).
# They are deliberately absent: we emit SDR and tag the result Display P3, so a
# PQ-encoded render tagged as SDR would look badly wrong. Adding them needs EDR
# output support first, which is a scope change, not a bake change.
VIEWS = [
    ("aces2_p3d65_sdr100",   "Display P3 - Display",
     "ACES 2.0 - SDR 100 nits (P3 D65)", 65),
    ("aces2_srgb_sdr100",    "sRGB - Display",
     "ACES 2.0 - SDR 100 nits (Rec.709)", 33),
    ("aces2_g22_709_sdr100", "Gamma 2.2 Rec.709 - Display",
     "ACES 2.0 - SDR 100 nits (Rec.709)", 33),
    ("aces2_rec709_sdr100",  "Rec.1886 Rec.709 - Display",
     "ACES 2.0 - SDR 100 nits (Rec.709)", 33),
    ("aces2_p3d65_p3",       "P3-D65 - Display",
     "ACES 2.0 - SDR 100 nits (P3 D65)", 33),
    ("aces2_p3d65_709",      "P3-D65 - Display",
     "ACES 2.0 - SDR 100 nits (Rec.709)", 33),
]

# Input transforms for PQ (SMPTE ST 2084) HDR masters, offered in the
# input-colourspace picker. A PQ EXR is display-referred -- already tone-mapped,
# brightness in absolute nits -- so these bake OCIO's *inverse* of the ACES 2.0
# HDR output transform: PQ code values in, scene-linear ACEScg out. The domain is
# PQ code values in [0,1], which is bounded, so no shaper is needed. 65^3: the
# whole PQ -> SDR chain then matches OCIO to 0.07/255 mean, ~3/255 worst
# (measured 2026-10-07; 33^3 was ~10/255 worst).
# name, label, display, view, cube size
INPUTS = [
    ("pq_p3d65_1000", "P3-D65 PQ (HDR master, 1000 nits)",
     "ST2084-P3-D65 - Display", "ACES 2.0 - HDR 1000 nits (P3 D65)", 65),
    ("pq_p3d65_4000", "P3-D65 PQ (HDR master, 4000 nits)",
     "ST2084-P3-D65 - Display", "ACES 2.0 - HDR 4000 nits (P3 D65)", 65),
    ("pq_rec2100_1000", "Rec.2100 PQ (HDR master, 1000 nits)",
     "Rec.2100-PQ - Display", "ACES 2.0 - HDR 1000 nits (Rec.2020)", 65),
]

MAGIC = b"EXRL"
VERSION = 1


def cct_to_lin(y):
    """ACEScct decode. The shaper that gives the LUT a sane scene-linear domain."""
    return np.where(y <= 0.155251141552511,
                    (y - 0.0729055341958355) / 10.5402377416545,
                    2.0 ** (y * 17.52 - 9.72))


def bake(cfg, display, view, size):
    cpu = cfg.getProcessor(INPUT_SPACE, display, view,
                           ocio.TRANSFORM_DIR_FORWARD).getDefaultCPUProcessor()
    g = np.linspace(0.0, 1.0, size, dtype=np.float32)
    bb, gg, rr = np.meshgrid(g, g, g, indexing="ij")
    grid = np.stack([rr, gg, bb], axis=-1).reshape(-1, 3)
    buf = np.ascontiguousarray(cct_to_lin(grid).astype(np.float32))
    cpu.applyRGB(buf)
    # Display code values are in [0,1]; clamp so fp16 storage is exact-ish and
    # the runtime never has to handle out-of-range table entries.
    return np.clip(buf, 0.0, 1.0).astype(np.float16)


def bake_inverse(cfg, display, view, size):
    t = ocio.DisplayViewTransform(src=INPUT_SPACE, display=display, view=view,
                                  direction=ocio.TRANSFORM_DIR_INVERSE)
    cpu = cfg.getProcessor(t).getDefaultCPUProcessor()
    g = np.linspace(0.0, 1.0, size, dtype=np.float32)
    bb, gg, rr = np.meshgrid(g, g, g, indexing="ij")
    buf = np.ascontiguousarray(np.stack([rr, gg, bb], axis=-1).reshape(-1, 3))
    cpu.applyRGB(buf)
    # Scene-linear out: keep the range (up to ~1500 for a 4000-nit master; fp16
    # holds it), but no NaN and nothing negative.
    return np.clip(np.nan_to_num(buf, nan=0.0), 0.0, 60000.0).astype(np.float16)


def serialise(size, data):
    """MAGIC | version | size | reserved | fp16 RGB triples, r fastest."""
    head = MAGIC + struct.pack("<III", VERSION, size, 0)
    return head + data.tobytes()


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    res = root / "EXRCore" / "Resources"
    res.mkdir(parents=True, exist_ok=True)

    cfg = ocio.Config.CreateFromBuiltinConfig(CONFIG)
    print(f"OCIO {ocio.GetVersion()}   config {CONFIG}")

    entries = []
    for name, display, view, size in VIEWS:
        data = bake(cfg, display, view, size)
        blob = serialise(size, data)
        (res / f"{name}.lut").write_bytes(blob)
        digest = hashlib.sha256(blob).hexdigest()
        entries.append((name, display, view, size, digest, len(blob), data))
        print(f"  {name:<24} {size:>3}^3  {len(blob)/1024:8.1f} KiB  {digest[:16]}")

    inputs = []
    for name, label, display, view, size in INPUTS:
        data = bake_inverse(cfg, display, view, size)
        blob = serialise(size, data)
        (res / f"{name}.lut").write_bytes(blob)
        digest = hashlib.sha256(blob).hexdigest()
        inputs.append((name, label, display, view, size, digest, len(blob), data))
        print(f"  {name:<24} {size:>3}^3  {len(blob)/1024:8.1f} KiB  {digest[:16]}  (input, inverse)")

    # Generated C++ so the LUTs are compiled in. An appex that cannot find a
    # resource file has no fallback; a compiled-in table cannot go missing.
    out = root / "EXRCore" / "src" / "exr_lut_data.cpp"
    with out.open("w") as f:
        f.write("// GENERATED by Tools/bake-lut/bake.py -- do not edit.\n")
        f.write(f"// OCIO {ocio.GetVersion()}, config {CONFIG}\n")
        f.write('#include "EXRCore/exr_lut.h"\n\n#include <cstdint>\n\n')
        f.write("namespace exrcore {\nnamespace {\n\n")
        for name, size, data in [(e[0], e[3], e[6]) for e in entries] + \
                                [(i[0], i[4], i[7]) for i in inputs]:
            flat = data.view(np.uint16).ravel()
            f.write(f"const std::uint16_t k_{name}[] = {{\n")
            for i in range(0, len(flat), 16):
                f.write("".join(f"{v}," for v in flat[i:i + 16]) + "\n")
            f.write("};\n\n")
        f.write("}  // namespace\n\n")
        f.write("const BakedLut kBakedLuts[] = {\n")
        for name, display, view, size, digest, _n, _d2 in entries:
            f.write(f'    {{"{name}", "{view}", "{display}", {size}, k_{name}, "{digest}"}},\n')
        f.write("};\n")
        f.write(f"const int kBakedLutCount = {len(entries)};\n\n")
        f.write("// Input transforms: PQ code values -> scene-linear ACEScg (inverse view).\n")
        f.write("const InputLut kInputLuts[] = {\n")
        for name, label, display, view, size, digest, _n, _d in inputs:
            f.write(f'    {{{{"{name}", "{view}", "{display}", {size}, k_{name}, "{digest}"}}, "{label}"}},\n')
        f.write("};\n")
        f.write(f"const int kInputLutCount = {len(inputs)};\n\n")
        f.write("}  // namespace exrcore\n")
    print(f"\n  wrote {out.relative_to(root)}  ({out.stat().st_size/1024/1024:.1f} MB)")

    prov = root / "docs" / "lut-provenance.md"
    with prov.open("w") as f:
        f.write("# LUT provenance\n\n")
        f.write("Generated by `Tools/bake-lut/bake.py`. Regenerate and compare hashes with\n")
        f.write("`Tools/verify-lut.sh`; CI fails if they drift.\n\n")
        f.write("| Field | Value |\n|---|---|\n")
        f.write(f"| OCIO version | {ocio.GetVersion()} |\n")
        f.write(f"| Built-in config | `{CONFIG}` |\n")
        f.write(f"| Input colour space | `{INPUT_SPACE}` (scene-linear AP1) |\n")
        f.write("| Shaper | ACEScct encoding of the AP1 domain |\n")
        f.write("| Interpolation | tetrahedral, at runtime |\n")
        f.write("| Storage | IEEE half (fp16), RGB triples, red varying fastest |\n\n")
        f.write("## Baked views\n\n")
        f.write("| Name | Display | View | Size | Bytes | SHA-256 |\n|---|---|---|---|---|---|\n")
        for name, display, view, size, digest, n, _ in entries:
            f.write(f"| `{name}` | {display} | {view} | {size}³ | {n:,} | `{digest}` |\n")
        f.write("\n## Input transforms (inverse views, for PQ HDR masters)\n\n")
        f.write("Domain: PQ code values in [0,1]. Output: scene-linear ACEScg.\n\n")
        f.write("| Name | Display | Inverse of view | Size | Bytes | SHA-256 |\n|---|---|---|---|---|---|\n")
        for name, _label, display, view, size, digest, n, _ in inputs:
            f.write(f"| `{name}` | {display} | {view} | {size}³ | {n:,} | `{digest}` |\n")
        f.write("\n## Why these\n\n")
        f.write("`aces2_p3d65_sdr100` is the shipped default (decision D5 plus the\n")
        f.write("2026-09-06 decision recorded in `aces-transform-options.md`). The two\n")
        f.write("Rec.709 variants back the overlay's View transform control for people on\n")
        f.write("sRGB displays; §4 of that document shows 33³ is adequate for alternates.\n\n")
        f.write("Raw / no transform is not a LUT -- it is an identity path in code.\n")
    print(f"  wrote {prov.relative_to(root)}")


if __name__ == "__main__":
    main()

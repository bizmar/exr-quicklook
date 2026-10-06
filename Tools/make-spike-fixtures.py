#!/usr/bin/env python3
"""
Minimal OpenEXR writer for the Phase 0 spike.

Writes single-part, scanline, NO_COMPRESSION, half-float RGB files. This is
deliberately not a general EXR writer -- it exists only so the spike has valid
.exr files to point Finder at before EXRCore exists. Phase 4 replaces it.
"""
import struct
import sys
from pathlib import Path

MAGIC = b"\x76\x2f\x31\x01"
HALF = 1  # PixelType.HALF

# ACEScg / AP1 primaries with the ACES white point (D60-ish).
AP1 = (0.713, 0.293, 0.165, 0.830, 0.128, 0.044, 0.32168, 0.33767)


def attr(name, kind, payload):
    return name.encode() + b"\0" + kind.encode() + b"\0" + struct.pack("<i", len(payload)) + payload


def channels(names):
    out = b""
    for n in sorted(names):  # EXR requires alphabetical channel order
        out += n.encode() + b"\0" + struct.pack("<i", HALF) + struct.pack("<B", 0) + b"\0\0\0"
        out += struct.pack("<ii", 1, 1)  # xSampling, ySampling
    return out + b"\0"


def box2i(x0, y0, x1, y1):
    return struct.pack("<iiii", x0, y0, x1, y1)


def write_exr(path, width, height, pixel_fn, chroma=AP1, overscan=0):
    """pixel_fn(x, y) -> (r, g, b) scene-linear floats."""
    dx0, dy0 = -overscan, -overscan
    dx1, dy1 = width - 1 + overscan, height - 1 + overscan
    dw, dh = dx1 - dx0 + 1, dy1 - dy0 + 1

    header = b""
    header += attr("channels", "chlist", channels(["R", "G", "B"]))
    header += attr("compression", "compression", struct.pack("<B", 0))  # NO_COMPRESSION
    header += attr("dataWindow", "box2i", box2i(dx0, dy0, dx1, dy1))
    header += attr("displayWindow", "box2i", box2i(0, 0, width - 1, height - 1))
    header += attr("lineOrder", "lineOrder", struct.pack("<B", 0))  # INCREASING_Y
    header += attr("pixelAspectRatio", "float", struct.pack("<f", 1.0))
    header += attr("screenWindowCenter", "v2f", struct.pack("<ff", 0.0, 0.0))
    header += attr("screenWindowWidth", "float", struct.pack("<f", 1.0))
    if chroma is not None:
        header += attr("chromaticities", "chromaticities", struct.pack("<8f", *chroma))
    header += b"\0"

    # Channel order within a scanline is alphabetical: B, G, R.
    rows = []
    for y in range(dy0, dy1 + 1):
        b_ch, g_ch, r_ch = [], [], []
        for x in range(dx0, dx1 + 1):
            r, g, b = pixel_fn(x, y)
            r_ch.append(r); g_ch.append(g); b_ch.append(b)
        rows.append(struct.pack("<%de" % dw, *b_ch)
                    + struct.pack("<%de" % dw, *g_ch)
                    + struct.pack("<%de" % dw, *r_ch))

    row_bytes = len(rows[0])
    offset_table_size = 8 * dh
    first = len(MAGIC) + 4 + len(header) + offset_table_size
    offsets = [first + i * (8 + row_bytes) for i in range(dh)]

    with open(path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<i", 2))  # version 2, single-part scanline
        f.write(header)
        f.write(struct.pack("<%dQ" % dh, *offsets))
        for i, row in enumerate(rows):
            f.write(struct.pack("<ii", dy0 + i, row_bytes))
            f.write(row)
    return path


def gradient(x, y):
    """Horizontal hue sweep, vertical exposure ramp into HDR (>1.0)."""
    u = (x % 640) / 639.0
    v = (y % 360) / 359.0
    level = 0.02 * (64.0 ** v)          # ~0.02 to ~1.3, well past 1.0 at the top
    if u < 0.333:
        r, g, b = 1.0, u * 3.0, 0.0
    elif u < 0.666:
        r, g, b = 1.0 - (u - 0.333) * 3.0, 1.0, (u - 0.333) * 3.0
    else:
        r, g, b = (u - 0.666) * 3.0, 1.0 - (u - 0.666) * 3.0, 1.0
    return r * level, g * level, b * level


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "Tests/Fixtures/spike")
    out.mkdir(parents=True, exist_ok=True)

    made = []
    made.append(write_exr(out / "spike-acescg.exr", 640, 360, gradient, chroma=AP1))
    made.append(write_exr(out / "spike-no-chromaticities.exr", 640, 360, gradient, chroma=None))
    made.append(write_exr(out / "spike-overscan.exr", 640, 360, gradient, chroma=AP1, overscan=32))

    # Phase 0 also wrote a .exrspike positive control under a third-party UTI.
    # The question it answered is settled (docs/uti-findings.md), and the app
    # no longer declares that type, so it is not generated any more.

    for p in made:
        print(f"{p}  {p.stat().st_size:,} bytes")


if __name__ == "__main__":
    main()

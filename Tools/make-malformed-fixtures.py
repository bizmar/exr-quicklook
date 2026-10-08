#!/usr/bin/env python3
"""
Builds the malformed corpus (plan §9). Every file here must be rejected
cleanly: no crash, no hang, no allocation of an attacker-chosen size.

Cases are produced by mutating a known-good EXR, so each differs from a valid
file in exactly one way.
"""
import struct
import sys
from pathlib import Path


def find_attr(data, name, kind):
    """Locate an attribute's payload: returns (offset, size) of the value."""
    needle = name.encode() + b"\0" + kind.encode() + b"\0"
    i = data.find(needle)
    if i < 0:
        return None
    off = i + len(needle)
    (size,) = struct.unpack_from("<i", data, off)
    return off + 4, size


def patch_box(data, name, box):
    loc = find_attr(data, name, "box2i")
    if loc is None:
        raise SystemExit(f"could not find {name}")
    off, size = loc
    assert size == 16
    return data[:off] + struct.pack("<iiii", *box) + data[off + 16:]


def attr(name, kind, value):
    return name.encode() + b"\0" + kind.encode() + b"\0" + struct.pack("<i", len(value)) + value


def tiled_header(width, height, tile_w, tile_h, channels="RGB"):
    """A tiled header with no pixel data after it. The header is all the
    resource limits need to see: these files must be rejected before
    OpenEXR allocates anything for their chunks."""
    chlist = b"".join(c.encode() + b"\0" + struct.pack("<iB3xii", 1, 0, 1, 1)
                      for c in channels) + b"\0"
    box = struct.pack("<iiii", 0, 0, width - 1, height - 1)
    body = (attr("channels", "chlist", chlist)
            + attr("compression", "compression", bytes([3]))           # ZIP
            + attr("dataWindow", "box2i", box)
            + attr("displayWindow", "box2i", box)
            + attr("lineOrder", "lineOrder", bytes([0]))
            + attr("pixelAspectRatio", "float", struct.pack("<f", 1.0))
            + attr("screenWindowCenter", "v2f", struct.pack("<ff", 0, 0))
            + attr("screenWindowWidth", "float", struct.pack("<f", 1.0))
            + attr("tiles", "tiledesc", struct.pack("<IIB", tile_w, tile_h, 0))
            + b"\0")
    return struct.pack("<ii", 20000630, 2 | 0x200) + body + bytes(8)


def main():
    src = Path(sys.argv[1] if len(sys.argv) > 1
               else "Tests/Fixtures/spike/spike-acescg.exr")
    out = Path(sys.argv[2] if len(sys.argv) > 2 else "Tests/Fixtures/malformed")
    out.mkdir(parents=True, exist_ok=True)
    good = src.read_bytes()

    cases = {}

    # Truncation at several points: mid-header, mid-offset-table, mid-pixels.
    for pct in (1, 10, 50, 95):
        cases[f"truncated-{pct:02d}pct.exr"] = good[: max(1, len(good) * pct // 100)]

    cases["empty.exr"] = b""
    cases["magic-only.exr"] = good[:4]
    cases["garbage.exr"] = bytes(range(256)) * 64
    cases["wrong-magic.exr"] = b"\xde\xad\xbe\xef" + good[4:]

    # dataWindow larger than any real image. The allocation this implies is
    # 2^31 * 2^31 pixels; the point is that nothing tries to allocate it.
    cases["datawindow-huge.exr"] = patch_box(good, "dataWindow",
                                             (0, 0, 2_147_483_646, 2_147_483_646))
    # Inverted window: max < min.
    cases["datawindow-inverted.exr"] = patch_box(good, "dataWindow", (100, 100, 0, 0))
    # Geometry modelled on CVE-2026-28977: dimensions whose product overflows
    # 32-bit and whose 64-bit product is still absurd.
    cases["datawindow-overflow.exr"] = patch_box(good, "dataWindow",
                                                 (0, 0, 65535, 2_147_483_646))
    # displayWindow disagreeing wildly with dataWindow.
    cases["displaywindow-huge.exr"] = patch_box(good, "displayWindow",
                                                (-1_000_000, -1_000_000,
                                                 1_000_000, 1_000_000))
    # A negative-extent display window.
    cases["displaywindow-inverted.exr"] = patch_box(good, "displayWindow", (10, 10, 0, 0))

    # One tile covering the whole frame. Within the pixel limit, yet reading it
    # as scanlines made OpenEXR cache 7.2 GB for a 1.8 MB file.
    cases["tile-huge.exr"] = tiled_header(65535, 4577, 65535, 4577)
    # 1x1 tiles: millions of chunks, each an offset-table entry and a read.
    cases["tile-count.exr"] = tiled_header(4096, 4096, 1, 1)

    # One channel subsampled 1-in-144 vertically in a 32-line DWAA file.
    # OpenEXRCore counts one row of it in chunks that hold none, and its DWA
    # decoder then reads an unset row pointer: a crash in OpenEXR 3.4.16 (and
    # 3.5.2), found by Tools/fuzz.sh on 2026-10-08. Must be rejected up front.
    aov = bytearray(Path("Tests/Fixtures/corpus/aov-43-channels.exr").read_bytes())
    at = aov.index(b"background.G\0") + len(b"background.G\0")
    struct.pack_into("<i", aov, at + 12, 144)   # type, pLinear, 3 reserved, xSampling, ySampling
    cases["dwaa-ysampling.exr"] = bytes(aov)

    for name, blob in cases.items():
        (out / name).write_bytes(blob)
        print(f"  {name:<32} {len(blob):>10,} bytes")
    print(f"\n{len(cases)} malformed fixtures in {out}")


if __name__ == "__main__":
    main()

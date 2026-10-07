#!/usr/bin/env python3
"""
Reads OpenEXR headers from the first bytes of a file -- no pixels needed.

Layer detection only ever looks at part names, channel names and a few
attributes, so a header is all the real-world corpus needs. That lets the
corpus be built from partial (HTTP Range) downloads of large production
frames, and lets only *facts* -- names and attributes, never pixels -- be
committed.

Dev tool only. Not part of the shipped app; the app parses headers with the
OpenEXR library. Stdlib only.

    exrheader.py FILE...          print a JSON summary per file
"""
import json
import struct
import sys

MAGIC = 20000630
PIXEL_TYPES = {0: "uint", 1: "half", 2: "float"}
COMPRESSIONS = ["none", "rle", "zips", "zip", "piz", "pxr24", "b44", "b44a",
                "dwaa", "dwab", "htj2k256", "htj2k32"]


class NeedMore(Exception):
    """The buffer ends inside the header; fetch more bytes and retry."""


class Reader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def take(self, n):
        if self.p + n > len(self.d):
            raise NeedMore()
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def cstr(self):
        end = self.d.find(b"\0", self.p)
        if end < 0:
            raise NeedMore()
        s = self.d[self.p:end].decode("utf-8", "replace")
        self.p = end + 1
        return s

    def i32(self):
        return struct.unpack("<i", self.take(4))[0]


def parse_chlist(v):
    chans, p = [], 0
    while p < len(v) and v[p] != 0:
        end = v.index(b"\0", p)
        name = v[p:end].decode("utf-8", "replace")
        ptype, _lin, xs, ys = struct.unpack("<iB3xii", v[end + 1:end + 17])
        chans.append({"name": name, "type": PIXEL_TYPES.get(ptype, ptype),
                      "xs": xs, "ys": ys})
        p = end + 17
    return chans


def parse_value(typ, v):
    try:
        if typ == "chlist":
            return parse_chlist(v)
        if typ == "string":
            return v.decode("utf-8", "replace")
        if typ == "int":
            return struct.unpack("<i", v)[0]
        if typ == "float":
            return struct.unpack("<f", v)[0]
        if typ == "double":
            return struct.unpack("<d", v)[0]
        if typ == "compression":
            return COMPRESSIONS[v[0]] if v[0] < len(COMPRESSIONS) else v[0]
        if typ == "box2i":
            return list(struct.unpack("<4i", v))
        if typ == "chromaticities":
            return [round(x, 5) for x in struct.unpack("<8f", v)]
        if typ == "stringvector":
            out, p = [], 0
            while p + 4 <= len(v):
                n = struct.unpack("<i", v[p:p + 4])[0]
                out.append(v[p + 4:p + 4 + n].decode("utf-8", "replace"))
                p += 4 + n
            return out
    except Exception:
        pass
    return None  # not summarised; the attribute name and type still are


def parse(data):
    """Returns a header summary. Raises NeedMore if `data` is too short."""
    r = Reader(data)
    if len(data) < 8:
        raise NeedMore()
    if struct.unpack("<i", data[:4])[0] != MAGIC:
        raise ValueError("not an OpenEXR file")
    r.p = 4
    version = r.i32()
    flags = {"tiled": bool(version & 0x200), "long_names": bool(version & 0x400),
             "deep": bool(version & 0x800), "multipart": bool(version & 0x1000)}
    parts = []
    while True:
        attrs = {}
        while True:
            name = r.cstr()
            if name == "":
                break
            typ = r.cstr()
            size = r.i32()
            if size < 0 or size > 64 << 20:
                raise ValueError(f"implausible attribute size {size}")
            value = r.take(size)
            attrs[name] = {"type": typ, "value": parse_value(typ, value)}
        if not attrs:          # the empty header that ends a multi-part list
            break
        parts.append(attrs)
        if not flags["multipart"]:
            break
    return {"flags": flags, "header_bytes": r.p, "parts": parts}


def summarise(h):
    """The facts the layer rules use, in a compact, diff-friendly form."""
    out = {"multipart": h["flags"]["multipart"], "parts": []}
    for a in h["parts"]:
        get = lambda k: (a.get(k) or {}).get("value")
        part = {
            "name": get("name") or "",
            "type": get("type") or ("tiledimage" if h["flags"]["tiled"] else "scanlineimage"),
            "compression": get("compression"),
            "channels": [c["name"] for c in (get("channels") or [])],
            "chromaticities": get("chromaticities"),
            "colorInteropID": get("colorInteropID"),
        }
        # Which tool wrote the file and anything naming a colour space -- but
        # never machine names, users or paths, even though the source files
        # are public: they are not needed and need not be republished.
        def keep(k):
            k = k.lower()
            if any(t in k for t in ("host", "computer", "user", "owner", "machine", "cpu",
                                    "hw", "path", "file", "dir", "node")):
                return False
            return any(t in k for t in ("software", "version", "writer", "renderer",
                                        "colorspace", "color_space", "ocio"))
        hints = {}
        for k, v in a.items():
            if v["type"] == "string" and v["value"] is not None and keep(k):
                val = v["value"].split(" : ")[0]   # "OpenImageIO 2.1 : oiiotool <command>"
                hints[k] = val[:80]
        if hints:
            part["hints"] = hints
        part["attribute_names"] = sorted(a.keys())
        out["parts"].append(part)
    return out


if __name__ == "__main__":
    for path in sys.argv[1:]:
        with open(path, "rb") as f:
            data = f.read()
        try:
            print(json.dumps({"file": path, **summarise(parse(data))}, indent=1))
        except NeedMore:
            print(json.dumps({"file": path, "error": "truncated inside header"}))
        except ValueError as e:
            print(json.dumps({"file": path, "error": str(e)}))

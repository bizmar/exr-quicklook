#!/usr/bin/env python3
"""
Builds the real-world header corpus in Vendor/corpus/ (git-ignored).

Every source below is openly licensed and listed with its licence. Large
production frames are fetched as *headers only* (an HTTP Range request for the
first bytes, extended until the header parses); small test files are fetched
whole. Nothing here is committed: Tools/corpus/manifest.py later extracts the
facts the layer rules need -- part and channel names, colour attributes -- into
Tests/Fixtures/realworld/headers.json, which is.

    build_corpus.py [--budget-mb N] [--only SOURCE]
"""
import argparse
import html.parser
import json
import os
import re
import subprocess
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
import exrheader  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "Vendor", "corpus")
UA = {"User-Agent": "exr-quicklook-corpus/1.0 (+https://github.com/bizmar/exr-quicklook)"}
spent = 0
budget = 0


def get(url, rng=None):
    global spent
    req = urllib.request.Request(url, headers=dict(UA, **({"Range": rng} if rng else {})))
    with urllib.request.urlopen(req, timeout=120) as r:
        data = r.read()
    spent += len(data)
    if spent > budget:
        sys.exit(f"budget of {budget >> 20} MB reached -- stopping")
    return data


def fetch_header(url, dest):
    """Range-fetch until the header parses. Stores only the header bytes."""
    if os.path.exists(dest):
        return "cached"
    n = 256 << 10
    while n <= 32 << 20:
        data = get(url, f"bytes=0-{n - 1}")
        try:
            h = exrheader.parse(data)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as f:
                f.write(data[:h["header_bytes"]])
            return f"header {h['header_bytes']} B"
        except exrheader.NeedMore:
            if len(data) < n:          # the whole file was shorter than asked
                raise ValueError("file ends inside its header")
            n *= 4
    raise ValueError("header larger than 32 MB")


def fetch_whole(url, dest):
    if os.path.exists(dest):
        return "cached"
    data = get(url)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "wb") as f:
        f.write(data)
    return f"whole {len(data)} B"


def gh_tree(repo, pattern=r"\.exr$"):
    out = subprocess.run(["gh", "api", f"repos/{repo}/git/trees/HEAD?recursive=1",
                          "--jq", ".tree[] | select(.type==\"blob\") | \"\\(.size)\\t\\(.path)\""],
                         capture_output=True, text=True, check=True).stdout
    rows = [l.split("\t", 1) for l in out.splitlines()]
    return [(int(s), p) for s, p in rows if re.search(pattern, p, re.I)]


class Links(html.parser.HTMLParser):
    def __init__(self):
        super().__init__()
        self.hrefs = []

    def handle_starttag(self, tag, attrs):
        if tag == "a":
            self.hrefs += [v for k, v in attrs if k == "href"]


def listing(url):
    p = Links()
    p.feed(get(url).decode("utf-8", "replace"))
    return p.hrefs


# ---------------------------------------------------------------------------
# Sources. id -> (licence, credit, generator of (url, relative path, mode))

def sole_mates():
    base = "https://media.githubusercontent.com/media/DigitalProductionExampleLibrary/SoleMates/main/"
    for _size, path in gh_tree("DigitalProductionExampleLibrary/SoleMates"):
        yield base + path, path.split("/")[-1], "header"


def cryptomatte():
    base = "https://raw.githubusercontent.com/Psyop/Cryptomatte/master/"
    for size, path in gh_tree("Psyop/Cryptomatte"):
        yield base + path, path.replace("/", "__"), "header" if size > 2 << 20 else "whole"


def gaffer():
    base = "https://raw.githubusercontent.com/GafferHQ/gaffer/main/"
    for size, path in gh_tree("GafferHQ/gaffer"):
        yield base + path, path.replace("/", "__"), "whole" if size < 1 << 20 else "header"


def oiio():
    base = "https://raw.githubusercontent.com/AcademySoftwareFoundation/OpenImageIO/main/"
    for size, path in gh_tree("AcademySoftwareFoundation/OpenImageIO"):
        if "/ref/" in path:            # generated reference outputs, not inputs
            continue
        yield base + path, path.replace("/", "__"), "whole" if size < 1 << 20 else "header"


def tlrender():
    yield ("https://raw.githubusercontent.com/darbyjohnston/tlRender/main/etc/SampleData/Cubes.0001.exr",
           "Cubes.0001.exr", "whole")


def tears_of_steel():
    base = "https://ftp.lysator.liu.se/pub/xiph/media/tearsofsteel/tearsofsteel-frames-exr/"
    for d in listing(base):
        if not d.endswith("/") or d.startswith(("/", "?", "..")):
            continue
        try:
            exrs = sorted(h for h in listing(base + d) if h.lower().endswith(".exr"))
        except Exception:
            continue
        if exrs:                        # first frame of each scene
            yield base + d + exrs[0], d.strip("/") + "__" + exrs[0], "header"


def netflix():
    base = "https://s3.amazonaws.com/download.opencontent.netflix.com/"
    for key in ("sparks/aces_image_sequence_59_94_fps/SPARKS_ACES_00000.exr",
                "CosmosLaundromat/exr/CosmosLaundromat_2k24p_HDR_P3PQ_00000.exr",
                "Nocturne/nocturne_120fps/s01e04/vdm/hdr/p3d65_pq/20180307_02/3840x2160/"
                "nocturne_120fps_s01e04_vdm_hdr_p3d65_pq_20180307_02_3840x2160_dovi_4000nit_0000000.exr"):
        # Whole frames: colour handling is judged on pixels, not just tags.
        yield base + key, key.split("/")[-1], "whole"


def polyhaven(n=24):
    assets = json.loads(get("https://api.polyhaven.com/assets?t=hdris"))
    # A spread across categories and authors rather than the first N.
    picked = sorted(assets.items(), key=lambda kv: (-kv[1].get("download_count", 0)))[:n]
    for slug, _meta in picked:
        files = json.loads(get(f"https://api.polyhaven.com/files/{slug}"))
        url = (((files.get("hdri") or {}).get("1k") or {}).get("exr") or {}).get("url")
        if url:
            yield url, f"{slug}_1k.exr", "whole"


SOURCES = {
    "solemates": ("ASWF Digital Assets License v1.1", "NAS Sole Mates - HDR Production Example "
                  "Copyright 2025 Netflix, Inc. All rights reserved.", sole_mates),
    "cryptomatte": ("BSD-3-Clause", "Psyop Cryptomatte sample images", cryptomatte),
    "gaffer": ("BSD-3-Clause", "Gaffer test images (Contributors to the Gaffer project)", gaffer),
    "oiio": ("Apache-2.0", "OpenImageIO testsuite images (Contributors to the OpenImageIO project)", oiio),
    "tlrender": ("BSD-3-Clause", "tlRender sample data (Darby Johnston)", tlrender),
    "tearsofsteel": ("CC-BY-3.0", "(CC) Blender Foundation | mango.blender.org", tears_of_steel),
    "netflix": ("CC-BY-4.0", "Netflix Open Content (Sparks, Cosmos Laundromat, Nocturne)", netflix),
    "polyhaven": ("CC0-1.0", "Poly Haven HDRIs", polyhaven),
}


def main():
    global budget
    ap = argparse.ArgumentParser()
    ap.add_argument("--budget-mb", type=int, default=2048)
    ap.add_argument("--only")
    a = ap.parse_args()
    budget = a.budget_mb << 20
    index_path = os.path.join(OUT, "index.json")
    index = json.load(open(index_path)) if os.path.exists(index_path) else {}
    for sid, (licence, credit, gen) in SOURCES.items():
        if a.only and sid != a.only:
            continue
        print(f"== {sid} ({licence})", flush=True)
        try:
            items = list(gen())
        except Exception as e:
            print(f"   could not list: {e}")
            continue
        for url, rel, mode in items:
            dest = os.path.join(OUT, sid, rel)
            try:
                how = (fetch_header if mode == "header" else fetch_whole)(url, dest)
                index[f"{sid}/{rel}"] = {"source": sid, "url": url, "licence": licence,
                                         "credit": credit, "mode": mode}
            except Exception as e:
                how = f"FAILED {e}"
            print(f"   {how:<22} {rel}", flush=True)
        os.makedirs(OUT, exist_ok=True)
        json.dump(index, open(index_path, "w"), indent=1, sort_keys=True)
        print(f"   ({spent / 1e6:.1f} MB downloaded so far)", flush=True)


if __name__ == "__main__":
    main()

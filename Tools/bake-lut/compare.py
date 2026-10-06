#!/usr/bin/env python3
"""Compares the compiled-in LUT runtime against OCIO's direct evaluation."""
import pathlib, subprocess, sys
import numpy as np, PyOpenColorIO as ocio

ROOT = pathlib.Path(__file__).resolve().parents[2]
CONFIG = "cg-config-v4.0.0_aces-v2.0_ocio-v2.5"
# LUT name -> (display, view, budget on mean error, budget on p99.9), in /255.
CASES = {
    "aces2_p3d65_sdr100":  ("Display P3 - Display", "ACES 2.0 - SDR 100 nits (P3 D65)", 0.35, 18.0),
    "aces2_srgb_sdr100":   ("sRGB - Display",       "ACES 2.0 - SDR 100 nits (Rec.709)", 1.00, 26.0),
}

cfg = ocio.Config.CreateFromBuiltinConfig(CONFIG)
rng = np.random.default_rng(20260906)
samples = np.concatenate([
    rng.random((20000, 3)).astype(np.float32) * 2.0,     # ordinary range
    rng.random((10000, 3)).astype(np.float32) * 16.0,    # HDR highlights
    np.eye(3, dtype=np.float32)[rng.integers(0, 3, 10000)]
        * rng.random((10000, 1)).astype(np.float32) * 8.0,   # saturated primaries
    rng.random((5000, 3)).astype(np.float32) * 0.02,     # near black
])

text = "\n".join(f"{r} {g} {b}" for r, g, b in samples)
fail = 0
for name, (display, view, mean_budget, p999_budget) in CASES.items():
    proc = cfg.getProcessor("ACEScg", display, view,
                            ocio.TRANSFORM_DIR_FORWARD).getDefaultCPUProcessor()
    ref = np.ascontiguousarray(samples.copy()); proc.applyRGB(ref)
    ref = np.clip(ref, 0.0, 1.0)

    out = subprocess.run([str(ROOT / "build" / "lut_apply"), name],
                         input=text, capture_output=True, text=True, check=True)
    got = np.array([[float(v) for v in line.split()]
                    for line in out.stdout.strip().splitlines()], dtype=np.float32)

    err = np.abs(ref - got) * 255.0
    mean, p999, mx = err.mean(), np.percentile(err, 99.9), err.max()
    ok = mean <= mean_budget and p999 <= p999_budget
    fail += 0 if ok else 1
    print(f"  {'ok  ' if ok else 'FAIL'}  {name:<22} "
          f"mean {mean:5.3f} (<= {mean_budget})   p99.9 {p999:5.2f} (<= {p999_budget})   max {mx:5.2f}")

if fail:
    print("\n  LUT runtime drifted from OCIO beyond the documented budget.")
sys.exit(1 if fail else 0)

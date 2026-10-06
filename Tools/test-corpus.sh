#!/bin/bash
#
# Regression test: layer selection against real OpenEXR-written files.
# Each expectation encodes a rule from plan §6.3.
#
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
C="$ROOT/Tests/Fixtures/corpus"
CLI="$ROOT/build/exrcli"
[ -x "$CLI" ] || { echo "Run Tools/build-cli.sh first." >&2; exit 1; }
[ -d "$C" ] || { echo "Run build/make-fixtures first." >&2; exit 1; }

pass=0; fail=0
# file | grep pattern that must appear | what rule it proves
check() {
  local file="$1" pattern="$2" why="$3"
  local out; out=$("$CLI" "$C/$file" 2>&1)
  if echo "$out" | grep -qE "$pattern"; then
    printf '  ok    %-32s %s\n' "$file" "$why"; pass=$((pass+1))
  else
    printf '  FAIL  %-32s %s\n' "$file" "$why"
    printf '        expected to match: %s\n' "$pattern"; fail=$((fail+1))
  fi
}

check dwaa-multilayer-acescg.exr 'layer +\(default, unprefixed\)' \
      "unprefixed RGB beats diffuse/crypto/mask"
check dwaa-multilayer-acescg.exr 'compression +DWAA' "DWAA parsed (D2 hard requirement)"
check dwab-multilayer-acescg.exr 'compression +DWAB' "DWAB parsed"
check dwaa-multilayer-acescg.exr 'chromaticities ACEScg' "ACEScg primaries recognised"
check aov-only-no-beauty.exr     'layer +diffuse' "falls through to first real AOV, not crypto"
check aov-only-no-beauty.exr     'R=diffuse\.R'   "channel names fully qualified"
check named-beauty-layer.exr     'layer +beauty'  "named beauty wins over earlier layer"
check aces2065-1.exr             'ACES2065-1'     "AP0 primaries recognised"
check aces2065-1.exr             'alpha +A'       "alpha taken from the same layer"
check no-chromaticities.exr      'chromaticities ABSENT' "missing chromaticities detected (D9)"
check float32.exr                'kind +RGB'      "32-bit float handled"
check luminance-chroma.exr       'kind +luminance-chroma' "Y/RY/BY detected"
check overscan.exr               'overscan +yes'  "overscan detected (D6)"
check multipart.exr              'part +1'        "prefers the part named beauty"
check deep-scanline.exr          'none selectable' "deep parts are never selected"
check nan-inf.exr                'kind +RGB'      "NaN/Inf file still decodes"
check out-of-gamut.exr           'kind +RGB'      "out-of-gamut values decode"
check aov-43-channels.exr        'channels +45'   "45-channel AOV stack parsed"
check aov-43-channels.exr        'layer +\(default' "beauty wins over 14 AOVs incl. cryptomattes"
check nested-layer-names.exr     'character[.]diffuse' "nested dot-separated layer names"
check single-channel.exr         'kind +greyscale' "single channel renders greyscale"
check has-preview-attr.exr       'preview attr +yes' "preview attribute detected (§6.4 fast path)"
check tiled-mipmap.exr           'type +tiledimage'  "tiled/mipmapped file parsed"
check data-passes.exr            'layer +\(default'  "beauty wins over P, depth and motion"
check data-passes.exr            'P +data +R=P\.x G=P\.y B=P\.z' "position listed as data, x/y/z as RGB"
check data-passes.exr            'motion +data +R=motion\.u G=motion\.v B=0' "two-component motion, blue zero"
check depth-only.exr             'R=Z G=Z B=Z' "a depth-only file shows its depth"
check position-only.exr          'layer +P'    "a position-only file shows its position"

# NaN and Inf must not survive into the decoded image (§6.4).
# Build it if absent rather than skipping: a test that silently disappears when
# an unrelated script cleans build/ is worse than one that fails.
if [ ! -x "$ROOT/build/nancheck" ] && [ -f "$ROOT/build/lib/libEXRCore.a" ]; then
  INS="$ROOT/Vendor/openexr/install"
  c++ -std=c++17 -O1 -I "$ROOT/EXRCore/include" "$ROOT/EXRCore/tests/nancheck.cpp" \
      "$ROOT/build/lib/libEXRCore.a" \
      -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
      -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -lopenjph \
      -o "$ROOT/build/nancheck" 2>/dev/null
fi
if [ -x "$ROOT/build/nancheck" ]; then
  if "$ROOT/build/nancheck" "$C/nan-inf.exr" >/dev/null 2>&1; then
    printf '  ok    %-32s %s\n' "nan-inf.exr" "no NaN/Inf survives decode"; pass=$((pass+1))
  else
    printf '  FAIL  %-32s %s\n' "nan-inf.exr" "NaN or Inf leaked through decode"; fail=$((fail+1))
  fi
fi

# Real-world plate: DWAA level 15, AP0, genuine overscan (D6).
REAL="$ROOT/Tests/Fixtures/real/LCF_05-01_01_X1_0039_AMaZE.exr"
if [ -f "$REAL" ]; then
  out=$("$CLI" "$REAL" 2>&1)
  for pat in 'DWAA \(level 15\)' 'ACES2065-1' 'overscan +yes' 'colorInteropID +lin_ap0_scene'; do
    if echo "$out" | grep -qE "$pat"; then
      printf '  ok    %-32s %s\n' "real plate" "$pat"; pass=$((pass+1))
    else
      printf '  FAIL  %-32s %s\n' "real plate" "$pat"; fail=$((fail+1))
    fi
  done
fi

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]

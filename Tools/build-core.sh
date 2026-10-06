#!/bin/bash
#
# Builds EXRCore as a universal static library for the app extensions.
# Produces build/lib/<arch>/libEXRCore.a plus a fat build/lib/libEXRCore.a.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INS="$ROOT/Vendor/openexr/install"
OUT="$ROOT/build/lib"
DEPLOY=14.0
ARCHS=(arm64 x86_64)
[ "${1:-}" = "--arm64-only" ] && ARCHS=(arm64)

[ -f "$INS/lib/libOpenEXR-3_4.a" ] || { echo "Run Tools/build-openexr.sh first." >&2; exit 1; }
[ -f "$ROOT/EXRCore/src/exr_lut_data.cpp" ] || {
  echo "Run Tools/bake-lut/bake.py first (generated LUT data missing)." >&2; exit 1; }

SRCS=(exr_layers.cpp exr_reader.cpp exr_decode.cpp exr_color.cpp exr_lut.cpp
      exr_lut_data.cpp exr_spaces_data.cpp exr_api.cpp)

rm -rf "$OUT"; mkdir -p "$OUT"
slices=()
for arch in "${ARCHS[@]}"; do
  d="$OUT/$arch"; mkdir -p "$d"
  for s in "${SRCS[@]}"; do
    c++ -std=c++17 -O2 -fvisibility=hidden -Wall \
        -target "$arch-apple-macos$DEPLOY" \
        -I "$ROOT/EXRCore/include" -I "$INS/include" \
        -I "$INS/include/OpenEXR" -I "$INS/include/Imath" \
        -c "$ROOT/EXRCore/src/$s" -o "$d/${s%.cpp}.o"
  done
  libtool -static -o "$d/libEXRCore.a" "$d"/*.o 2>/dev/null
  slices+=("$d/libEXRCore.a")
done

if [ "${#slices[@]}" -gt 1 ]; then
  lipo -create "${slices[@]}" -output "$OUT/libEXRCore.a"
else
  cp "${slices[0]}" "$OUT/libEXRCore.a"
fi

printf 'built %s  (%s)\n' "$OUT/libEXRCore.a" "$(lipo -archs "$OUT/libEXRCore.a")"

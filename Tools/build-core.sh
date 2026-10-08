#!/bin/bash
#
# Builds EXRCore as a universal static library for the app extensions.
# Produces build/lib/<arch>/libEXRCore.a plus a fat build/lib/libEXRCore.a.
#
#   --arm64-only        one architecture
# -ffp-contract=off: Apple silicon would otherwise fuse multiply-adds, which
# Intel's baseline cannot, and the two rounded extreme values differently
# (up to 1.9/255 on nan-inf.exr). Unfused, every Mac renders the same pixels
# to 0.25/255, at no measurable cost (236 ms either way on the 6K plate).
#
#   --keep-data-object  reuse an up-to-date exr_lut_data.o (21 MB of generated
#                       tables) instead of recompiling it. The CodeQL workflow
#                       builds it before tracing starts: it is data, and
#                       extracting it took most of a 20-minute traced build.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INS="$ROOT/Vendor/openexr/install"
OUT="$ROOT/build/lib"
DEPLOY=14.0
ARCHS=(arm64 x86_64)
KEEP_DATA=0
for arg in "$@"; do
  case "$arg" in
    --arm64-only)       ARCHS=(arm64) ;;
    --keep-data-object) KEEP_DATA=1 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

[ -f "$INS/lib/libOpenEXR-3_4.a" ] || { echo "Run Tools/build-openexr.sh first." >&2; exit 1; }
[ -f "$ROOT/EXRCore/src/exr_lut_data.cpp" ] || {
  echo "Run Tools/bake-lut/bake.py first (generated LUT data missing)." >&2; exit 1; }

SRCS=(exr_layers.cpp exr_reader.cpp exr_decode.cpp exr_color.cpp exr_lut.cpp
      exr_lut_data.cpp exr_spaces_data.cpp exr_api.cpp)

if [ "$KEEP_DATA" = 1 ]; then
  find "$OUT" -name '*.o' ! -name 'exr_lut_data.o' -delete 2>/dev/null || true
else
  rm -rf "$OUT"
fi
mkdir -p "$OUT"
slices=()
for arch in "${ARCHS[@]}"; do
  d="$OUT/$arch"; mkdir -p "$d"
  for s in "${SRCS[@]}"; do
    if [ "$KEEP_DATA" = 1 ] && [ "$s" = exr_lut_data.cpp ] &&
       [ "$d/exr_lut_data.o" -nt "$ROOT/EXRCore/src/exr_lut_data.cpp" ]; then
      continue
    fi
    c++ -std=c++17 -O2 -fvisibility=hidden -Wall -ffile-prefix-map="$ROOT=." -ffp-contract=off \
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

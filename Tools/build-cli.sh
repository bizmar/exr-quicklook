#!/bin/bash
# Builds EXRCLI against the pinned static OpenEXR in Vendor/.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INS="$ROOT/Vendor/openexr/install"
[ -f "$INS/lib/libOpenEXR-3_4.a" ] || { echo "Run Tools/build-openexr.sh first." >&2; exit 1; }
mkdir -p "$ROOT/build"
c++ -std=c++17 -Wall -Wextra -O2 \
    -I "$ROOT/EXRCore/include" -I "$INS/include" -I "$INS/include/OpenEXR" -I "$INS/include/Imath" \
    "$ROOT/EXRCore/src/exr_layers.cpp" "$ROOT/EXRCore/src/exr_reader.cpp" \
    "$ROOT/EXRCore/src/exr_decode.cpp" "$ROOT/EXRCore/src/exr_color.cpp" \
    "$ROOT/EXRCore/src/exr_lut.cpp" "$ROOT/EXRCore/src/exr_lut_data.cpp" \
    "$ROOT/EXRCLI/main.cpp" "$ROOT/EXRCLI/png_writer.cpp" \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
    -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -lopenjph \
    -framework ImageIO -framework CoreGraphics -framework CoreFoundation \
    -o "$ROOT/build/exrcli"
echo "built build/exrcli"

# The §9 fixture writer. Uses the real OpenEXR writer so the corpus reaches
# code paths (DWAA/DWAB, multi-part, deep) a hand-rolled writer cannot.
c++ -std=c++17 -O1 \
    -I "$INS/include" -I "$INS/include/OpenEXR" -I "$INS/include/Imath" \
    "$ROOT/Tools/make-fixtures/main.cpp" \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
    -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -lopenjph \
    -o "$ROOT/build/make-fixtures"
echo "built build/make-fixtures"

# Performance against the plan's §6.7 budget (Tools/bench/main.cpp).
c++ -std=c++17 -O2 -I "$ROOT/EXRCore/include" "$ROOT/Tools/bench/main.cpp" "$ROOT/build/lib/libEXRCore.a" \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
    -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -lopenjph \
    -o "$ROOT/build/bench"
echo "built build/bench"

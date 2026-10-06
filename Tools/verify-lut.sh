#!/bin/bash
#
# Option D golden check (plan §7 step 4, §9 golden-image regression).
#
# 1. Re-bakes the LUTs and confirms the SHA-256 matches docs/lut-provenance.md.
# 2. Compares our compiled-in tetrahedral runtime against OCIO's direct
#    evaluation, and fails if the error exceeds the documented budget.
#
# Needs PyOpenColorIO. Build-time / CI only -- never shipped.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY=${PYOCIO:-/opt/homebrew/bin/python3.14}
cd "$ROOT"

hr() { printf '\n\033[1m--- %s ---\033[0m\n' "$*"; }

"$PY" -c "import PyOpenColorIO, numpy" 2>/dev/null || {
  echo "PyOpenColorIO/numpy unavailable at $PY -- set PYOCIO to an interpreter that has them." >&2
  exit 1; }

hr "1. LUT hashes reproduce"
before=$(shasum -a 256 EXRCore/Resources/*.lut 2>/dev/null | awk '{print $1}' | sort)
"$PY" Tools/bake-lut/bake.py >/dev/null || exit 1
after=$(shasum -a 256 EXRCore/Resources/*.lut | awk '{print $1}' | sort)
if [ "$before" = "$after" ]; then echo "  ok    hashes unchanged after re-bake"
else echo "  FAIL  re-baking changed the LUTs"; echo "$before"; echo "$after"; exit 1; fi

for f in EXRCore/Resources/*.lut; do
  h=$(shasum -a 256 "$f" | cut -d' ' -f1)
  grep -q "$h" docs/lut-provenance.md \
    && echo "  ok    $(basename "$f") matches provenance" \
    || { echo "  FAIL  $(basename "$f") not in docs/lut-provenance.md"; exit 1; }
done

hr "2. runtime matches OCIO"
[ -x build/lut_apply ] || c++ -std=c++17 -O2 -I EXRCore/include \
  EXRCore/src/exr_lut.cpp EXRCore/src/exr_lut_data.cpp EXRCore/tests/lut_apply.cpp \
  -o build/lut_apply || exit 1
"$PY" Tools/bake-lut/compare.py || exit 1

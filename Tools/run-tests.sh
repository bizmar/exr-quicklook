#!/bin/bash
# Builds and runs the EXRCore unit tests. No OpenEXR dependency yet -- the
# layer-selection and hardening logic is deliberately pure so it tests fast.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/build"
c++ -std=c++17 -Wall -Wextra -Werror -O1 -I "$ROOT/EXRCore/include" \
    "$ROOT/EXRCore/src/exr_layers.cpp" \
    "$ROOT/EXRCore/tests/test_layers.cpp" \
    -o "$ROOT/build/test_layers"
c++ -std=c++17 -Wall -Wextra -Werror -O1 -I "$ROOT/EXRCore/include" \
    "$ROOT/EXRCore/src/exr_color.cpp" \
    "$ROOT/EXRCore/tests/test_color.cpp" \
    -o "$ROOT/build/test_color"
"$ROOT/build/test_layers"
"$ROOT/build/test_color"

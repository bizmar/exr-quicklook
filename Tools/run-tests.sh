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
c++ -std=c++17 -Wall -Wextra -Werror -O1 -I "$ROOT/EXRCore/include" \
    "$ROOT/EXRCore/src/exr_layers.cpp" \
    "$ROOT/EXRCore/tests/test_realworld.cpp" \
    -o "$ROOT/build/test_realworld"
"$ROOT/build/test_layers"
"$ROOT/build/test_color"
# Layer rules against names from real production files (Tests/Fixtures/realworld).
# If this fails after a deliberate rule change, rerun with --update and review
# the diff of expected.txt: it is the rule change's effect on real files.
"$ROOT/build/test_realworld" "$ROOT/Tests/Fixtures/realworld/channels.tsv" \
    "$ROOT/Tests/Fixtures/realworld/expected.txt"

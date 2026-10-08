#!/bin/bash
#
# Builds the pinned Imath + OpenEXR again with AddressSanitizer and UBSan,
# for this machine's architecture only, into Vendor/openexr/install-asan.
# Tools/fuzz.sh links it when present, so a bad read or write *inside* OpenEXR
# is reported where it happens instead of only when it happens to segfault
# (the 2026-10-08 DWA crash was caught only because it read address 0).
#
# Fuzzing only: never used for the app. Needs the sources that
# Tools/build-openexr.sh clones (same pinned tags).
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
V="$ROOT/Vendor/openexr"
SRC="$V/src"
INSTALL="$V/install-asan"
B="$V/build-asan"

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }

[ -d "$SRC/openexr/.git" ] && [ -d "$SRC/Imath/.git" ] ||
  { echo "Run Tools/build-openexr.sh first (it clones the pinned sources)." >&2; exit 1; }

if [ -f "$INSTALL/lib/libOpenEXR-3_4.a" ] && [ "${1:-}" != "--clean" ]; then
  say "Already built at $INSTALL (use --clean to rebuild)"
  exit 0
fi

# UBSan stays recoverable here: OpenEXR is third-party, so its reports are
# logged for reading rather than stopping the fuzzer at the first one.
SAN="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined"
common=(
  -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
  -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_C_FLAGS_DEBUG="$SAN"
  -DCMAKE_CXX_FLAGS_DEBUG="$SAN"
  -DCMAKE_INSTALL_PREFIX="$INSTALL"
  -DCMAKE_PREFIX_PATH="$INSTALL"
  -DCMAKE_DEBUG_POSTFIX=
  -DBUILD_SHARED_LIBS=OFF
  -DBUILD_TESTING=OFF
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

say "Imath $(git -C "$SRC/Imath" rev-parse --short HEAD), sanitized"
cmake -S "$SRC/Imath" -B "$B/imath" -G "Unix Makefiles" "${common[@]}" \
      -DIMATH_INSTALL_PKG_CONFIG=OFF > "$B-imath.log" 2>&1
cmake --build "$B/imath" -j"$(sysctl -n hw.ncpu)" --target install >> "$B-imath.log" 2>&1

say "OpenEXR $(git -C "$SRC/openexr" rev-parse --short HEAD), sanitized"
cmake -S "$SRC/openexr" -B "$B/openexr" -G "Unix Makefiles" "${common[@]}" \
      -DOPENEXR_BUILD_TOOLS=OFF -DOPENEXR_BUILD_EXAMPLES=OFF \
      -DOPENEXR_INSTALL_TOOLS=OFF -DOPENEXR_INSTALL_EXAMPLES=OFF \
      -DOPENEXR_INSTALL_PKG_CONFIG=OFF -DOPENEXR_FORCE_INTERNAL_IMATH=OFF \
      -DOPENEXR_FORCE_INTERNAL_DEFLATE=ON -DOPENEXR_FORCE_INTERNAL_OPENJPH=ON \
      > "$B-openexr.log" 2>&1
cmake --build "$B/openexr" -j"$(sysctl -n hw.ncpu)" \
      --target OpenEXR OpenEXRCore OpenEXRUtil Iex IlmThread >> "$B-openexr.log" 2>&1
cmake --install "$B/openexr" >> "$B-openexr.log" 2>&1

say "Installed"
ls "$INSTALL/lib"/*.a | sed 's/^/    /'

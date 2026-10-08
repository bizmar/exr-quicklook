#!/bin/bash
#
# Builds Imath + OpenEXR as universal (arm64 + x86_64) STATIC libraries into
# Vendor/openexr/install. Pinned tags, no Homebrew dependency, libdeflate
# fetched and built internally so the result is self-contained (decision D4).
#
# Idempotent: skips straight to the summary if the install tree already exists.
# Force a rebuild with --clean.
#
set -euo pipefail

IMATH_TAG=v3.2.2
OPENEXR_TAG=v3.4.16

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
V="$ROOT/Vendor/openexr"
SRC="$V/src"
INSTALL="$V/install"
DEPLOY=14.0
ARCHS="arm64;x86_64"

[ "${1:-}" = "--clean" ] && rm -rf "$V"

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }

if [ -f "$INSTALL/lib/libOpenEXR.a" ] && [ "${1:-}" != "--clean" ]; then
  say "Already built at $INSTALL (use --clean to rebuild)"
else
  mkdir -p "$SRC"

  clone() { # clone <repo> <tag> <dir>
    if [ ! -d "$SRC/$3/.git" ]; then
      say "Cloning $3 $2"
      git clone --depth 1 --branch "$2" "https://github.com/AcademySoftwareFoundation/$1.git" "$SRC/$3"
    fi
    printf '    %s @ %s\n' "$3" "$(git -C "$SRC/$3" rev-parse HEAD)"
  }

  clone Imath   "$IMATH_TAG"   Imath
  clone openexr "$OPENEXR_TAG" openexr

  common=(
    -DCMAKE_OSX_ARCHITECTURES="$ARCHS"
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOY"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$INSTALL"
    -DCMAKE_PREFIX_PATH="$INSTALL"
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_TESTING=OFF
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    # Source paths end up in the binary through __FILE__ (OpenJPH's error
    # macros); keep the builder's home directory out of the shipped app.
    -DCMAKE_C_FLAGS="-ffile-prefix-map=$ROOT=."
    -DCMAKE_CXX_FLAGS="-ffile-prefix-map=$ROOT=."
  )

  say "Building Imath $IMATH_TAG"
  cmake -S "$SRC/Imath" -B "$V/build-imath" -G "Unix Makefiles" "${common[@]}" \
        -DIMATH_INSTALL_PKG_CONFIG=OFF > "$V/imath-configure.log" 2>&1
  cmake --build "$V/build-imath" -j"$(sysctl -n hw.ncpu)" --target install \
        > "$V/imath-build.log" 2>&1

  say "Building OpenEXR $OPENEXR_TAG"
  cmake -S "$SRC/openexr" -B "$V/build-openexr" -G "Unix Makefiles" "${common[@]}" \
        -DOPENEXR_BUILD_TOOLS=OFF \
        -DOPENEXR_BUILD_EXAMPLES=OFF \
        -DOPENEXR_INSTALL_TOOLS=OFF \
        -DOPENEXR_INSTALL_EXAMPLES=OFF \
        -DOPENEXR_INSTALL_PKG_CONFIG=OFF \
        -DOPENEXR_FORCE_INTERNAL_IMATH=OFF \
        -DOPENEXR_FORCE_INTERNAL_DEFLATE=ON \
        -DOPENEXR_FORCE_INTERNAL_OPENJPH=ON \
        > "$V/openexr-configure.log" 2>&1
  # Build the library targets explicitly rather than "all". The default
  # target set includes website/example binaries that link against OpenJPH,
  # which we neither ship nor need, and whose link failure would otherwise
  # abort the whole build after the libraries are already good.
  cmake --build "$V/build-openexr" -j"$(sysctl -n hw.ncpu)" \
        --target OpenEXR OpenEXRCore OpenEXRUtil Iex IlmThread \
        > "$V/openexr-build.log" 2>&1
  cmake --install "$V/build-openexr" > "$V/openexr-install.log" 2>&1

  # Since 3.4.16 OpenJPH (HTJ2K) is vendored and compiled into OpenEXRCore,
  # so there is no separate archive to install or link.
fi

say "Installed static libraries"
for a in "$INSTALL"/lib/*.a; do
  printf '    %-28s %s\n' "$(basename "$a")" "$(lipo -archs "$a" 2>/dev/null)"
done
printf '\n    headers: %s\n' "$INSTALL/include"

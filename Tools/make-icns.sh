#!/bin/bash
# Logo -> EXRPreview/AppIcon.icns, end to end:
#   1. make-icon-tile --photo  the logo photo (drawn on white) covering a
#                              conforming macOS icon tile -- see that tool for
#                              why a tile, and why not background removal
#   2. iconutil                every size the .icns needs
# Tools/make-icon (transparent art) remains for artwork that is not on white.
# CLT only -- no actool, so a plain .icns via CFBundleIconFile (CLAUDE.md).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOGO="${1:-$ROOT/docs/images/EXRlogo_v2.jpg}"
BUILD="$ROOT/build/icon"
mkdir -p "$BUILD"
SDK=$(xcrun --show-sdk-path)
xcrun swiftc -sdk "$SDK" -O -o "$BUILD/make-icon-tile" "$ROOT/Tools/make-icon-tile/main.swift"
"$BUILD/make-icon-tile" --photo "$LOGO" "$ROOT/docs/images/AppIcon-1024.png" "${ICON_SCALE:-1.04}"

SET="$BUILD/AppIcon.iconset"
rm -rf "$SET"; mkdir -p "$SET"
for s in 16 32 128 256 512; do
  sips -z $s $s "$ROOT/docs/images/AppIcon-1024.png" --out "$SET/icon_${s}x${s}.png" >/dev/null
  d=$((s * 2))
  sips -z $d $d "$ROOT/docs/images/AppIcon-1024.png" --out "$SET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$SET" -o "$ROOT/EXRPreview/AppIcon.icns"
echo "wrote EXRPreview/AppIcon.icns and docs/images/AppIcon-1024.png"

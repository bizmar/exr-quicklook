#!/bin/bash
#
# Builds "EXR Quick Look.app" with its two app extensions using only the Command Line
# Tools toolchain -- no Xcode, no .xcodeproj.
#
# Requires Tools/build-openexr.sh and Tools/build-core.sh to have run first. The bundle layout, Info.plists,
# entitlements and code signature are what LaunchServices and PluginKit
# actually look at, so this is equivalent to an Xcode "Sign to Run Locally"
# build for the purposes of the Phase 0 question.
#
# Usage:
#   Tools/build-app.sh                 # declare both UTIs (default)
#   Tools/build-app.sh --control-only  # drop com.ilm.openexr-image
#   Tools/build-app.sh --arm64-only    # skip the x86_64 slice
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
APP="$BUILD/EXR Quick Look.app"
SDK="$(xcrun --show-sdk-path)"
DEPLOY=14.0

CONTROL_ONLY=0
ARCHES=(arm64 x86_64)
for arg in "$@"; do
  case "$arg" in
    --control-only) CONTROL_ONLY=1 ;;
    --arm64-only)   ARCHES=(arm64) ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }

# Clean only what this script produces. build/lib (Tools/build-core.sh) and the
# diagnostic helpers must survive, or the link below has nothing to link against.
rm -rf "$APP" "$BUILD/obj"
# Builds from before the 2026-10-06 rename, so two copies never compete in
# LaunchServices with the same bundle id.
if [ -d "$BUILD/EXRPreview.app" ]; then
  /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -u "$BUILD/EXRPreview.app" 2>/dev/null || true
  rm -rf "$BUILD/EXRPreview.app"
fi
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/PlugIns" "$BUILD/obj"

# ---------------------------------------------------------------- compile ---
CORE_LIB="$ROOT/build/lib/libEXRCore.a"
OEXR="$ROOT/Vendor/openexr/install"
[ -f "$CORE_LIB" ] || { echo "Run Tools/build-core.sh first." >&2; exit 1; }

# Flags that let Swift import the EXRCore C module and link the C++ core.
CORE_SWIFT_FLAGS=(
  -Xcc -fmodule-map-file="$ROOT/EXRCore/include/module.modulemap"
  -Xcc -I"$ROOT/EXRCore/include"
)
CORE_LINK_FLAGS=(
  -L"$ROOT/build/lib" -lEXRCore
  -L"$OEXR/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4
  -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -lopenjph
  -lc++
)

# $1 product name  $2 source dir  $3... extra swiftc flags and source files
compile() {
  local name="$1" src="$2"; shift 2
  local slices=()
  for arch in "${ARCHES[@]}"; do
    local out="$BUILD/obj/$name.$arch"
    xcrun swiftc \
      -sdk "$SDK" \
      -target "$arch-apple-macos$DEPLOY" \
      -swift-version 5 \
      -O -parse-as-library \
      -module-name "$name" \
      "$@" \
      -o "$out" \
      "$src"/*.swift
    slices+=("$out")
  done
  if [ "${#slices[@]}" -gt 1 ]; then
    lipo -create "${slices[@]}" -output "$BUILD/obj/$name"
  else
    cp "${slices[0]}" "$BUILD/obj/$name"
  fi
}

say "Compiling EXRPreview (host app) for ${ARCHES[*]}"
# The host app is an information window only: it uses neither EXRCore nor the
# shared renderer, so it does not link them -- that saved ~21 MB of decoder and
# colour tables per build. A fork that restores the preferences UI (which needs
# EXRRenderer.views/colorspaces via EXRPreferences) must add back
# "$ROOT/Shared/EXRRenderer.swift" "$ROOT/Shared/EXRPreferences.swift" and the
# CORE_SWIFT_FLAGS / CORE_LINK_FLAGS here.
compile EXRPreview "$ROOT/EXRPreview" \
  -framework SwiftUI -framework AppKit

say "Compiling EXRThumbnail (appex) for ${ARCHES[*]}"
compile EXRThumbnail "$ROOT/EXRThumbnail" \
  -application-extension \
  "$ROOT/Shared/EXRRenderer.swift" "$ROOT/Shared/EXRPreferences.swift" \
  "${CORE_SWIFT_FLAGS[@]}" "${CORE_LINK_FLAGS[@]}" \
  -framework QuickLookThumbnailing -framework CoreGraphics \
  -Xlinker -e -Xlinker _NSExtensionMain

say "Compiling EXRQuickLook (appex) for ${ARCHES[*]}"
compile EXRQuickLook "$ROOT/EXRQuickLook" \
  -application-extension \
  "$ROOT/Shared/EXRRenderer.swift" "$ROOT/Shared/EXRPreferences.swift" \
  "${CORE_SWIFT_FLAGS[@]}" "${CORE_LINK_FLAGS[@]}" \
  -framework Quartz -framework AppKit \
  -Xlinker -e -Xlinker _NSExtensionMain

# --------------------------------------------------------------- assemble ---
say "Assembling bundles"
cp "$BUILD/obj/EXRPreview" "$APP/Contents/MacOS/EXRPreview"
cp "$ROOT/EXRPreview/Info.plist" "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"

# App icon. A plain .icns via CFBundleIconFile, because actool (asset catalogs)
# needs full Xcode and this toolchain deliberately does not have it.
if [ -f "$ROOT/EXRPreview/AppIcon.icns" ]; then
  mkdir -p "$APP/Contents/Resources"
  cp "$ROOT/EXRPreview/AppIcon.icns" "$APP/Contents/Resources/AppIcon.icns"
fi

# The bundled libraries' BSD/MIT licences require their notices to travel with
# any binary redistribution, so the app carries them. Regenerate the notices
# with Tools/make-notices.sh when a pinned version changes.
for f in LICENSE THIRD_PARTY_NOTICES.md; do
  [ -f "$ROOT/$f" ] || { echo "missing $f -- binaries may not ship without it" >&2; exit 1; }
  mkdir -p "$APP/Contents/Resources"
  cp "$ROOT/$f" "$APP/Contents/Resources/$f"
done

for pair in "EXRThumbnail:$ROOT/EXRThumbnail" "EXRQuickLook:$ROOT/EXRQuickLook"; do
  name="${pair%%:*}"; dir="${pair#*:}"
  ax="$APP/Contents/PlugIns/$name.appex"
  mkdir -p "$ax/Contents/MacOS"
  cp "$BUILD/obj/$name" "$ax/Contents/MacOS/$name"
  cp "$dir/Info.plist" "$ax/Contents/Info.plist"
  printf 'XPC!????' > "$ax/Contents/PkgInfo"
  if [ "$CONTROL_ONLY" = 1 ]; then
    plutil -remove 'NSExtension.NSExtensionAttributes.QLSupportedContentTypes.0' \
      "$ax/Contents/Info.plist"
  fi
done

if [ "$CONTROL_ONLY" = 1 ]; then
  say "CONTROL-ONLY build: com.ilm.openexr-image removed from both extensions"
fi

# ------------------------------------------------------------------- sign ---
# Inside out: extensions first, then the app. Ad-hoc identity ("-") is the
# command-line equivalent of Xcode's "Sign to Run Locally".
say "Ad-hoc signing"
for name in EXRThumbnail EXRQuickLook; do
  codesign --force --sign - --timestamp=none \
    --entitlements "$ROOT/$name/$name.entitlements" \
    "$APP/Contents/PlugIns/$name.appex"
done
codesign --force --sign - --timestamp=none \
  --entitlements "$ROOT/EXRPreview/EXRPreview.entitlements" \
  "$APP"

say "Verifying signature"
codesign --verify --deep --strict --verbose=2 "$APP"

say "Building diagnostic helpers"
for helper in utitool qlprobe qlpreviewprobe qlpanelprobe; do
  xcrun swiftc -sdk "$SDK" -target "$(uname -m)-apple-macos$DEPLOY" -O \
    -o "$BUILD/$helper" "$ROOT/Tools/$helper/main.swift"
done

say "Built $APP"
lipo -archs "$APP/Contents/MacOS/EXRPreview" | sed 's/^/    archs: /'
for name in EXRThumbnail EXRQuickLook; do
  printf '    %s declares: %s\n' "$name" \
    "$(plutil -extract NSExtension.NSExtensionAttributes.QLSupportedContentTypes json -o - \
        "$APP/Contents/PlugIns/$name.appex/Contents/Info.plist")"
done

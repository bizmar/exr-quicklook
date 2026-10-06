#!/bin/bash
#
# Installs EXR Quick Look into ~/Applications and forces LaunchServices to
# re-scan it, so PluginKit sees the two extensions.
#
# ~/Applications rather than /Applications: no sudo, and LaunchServices scans
# it on the same terms.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/build/EXR Quick Look.app"
DEST_DIR="$HOME/Applications"
DEST="$DEST_DIR/EXR Quick Look.app"
LEGACY="$DEST_DIR/EXRPreview.app"   # the name before 2026-10-06
LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister

[ -d "$SRC" ] || { echo "No build at $SRC -- run Tools/build-spike.sh first." >&2; exit 1; }

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }

# Two installed copies with one bundle id would leave PluginKit choosing
# between them, so the old name is deregistered and removed first.
if [ -d "$LEGACY" ]; then
  say "Removing the pre-rename install at $LEGACY"
  pluginkit -r "$LEGACY/Contents/PlugIns/EXRThumbnail.appex" 2>/dev/null || true
  pluginkit -r "$LEGACY/Contents/PlugIns/EXRQuickLook.appex" 2>/dev/null || true
  "$LSREGISTER" -u "$LEGACY" 2>/dev/null || true
  rm -rf "$LEGACY"
fi

say "Installing to $DEST"
mkdir -p "$DEST_DIR"
# Deregister what is there before replacing it. If the bundle ids changed (as
# they did on 2026-10-06), the old ids would otherwise stay registered.
if [ -d "$DEST" ]; then
  pluginkit -r "$DEST/Contents/PlugIns/EXRThumbnail.appex" 2>/dev/null || true
  pluginkit -r "$DEST/Contents/PlugIns/EXRQuickLook.appex" 2>/dev/null || true
  "$LSREGISTER" -u "$DEST" 2>/dev/null || true
fi
rm -rf "$DEST"
cp -R "$SRC" "$DEST"
xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true

say "Registering with LaunchServices"
"$LSREGISTER" -f "$DEST"

say "Registering extensions with PluginKit"
pluginkit -a "$DEST/Contents/PlugIns/EXRThumbnail.appex"
pluginkit -a "$DEST/Contents/PlugIns/EXRQuickLook.appex"

say "Done. Now run Tools/spike-status.sh"

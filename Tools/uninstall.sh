#!/bin/bash
# Removes EXR Quick Look and every trace of its registration, under both the
# current name and the one used before 2026-10-06.
set -euo pipefail
LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister

for DEST in "$HOME/Applications/EXR Quick Look.app" "$HOME/Applications/EXRPreview.app"; do
  if [ -d "$DEST" ]; then
    pluginkit -r "$DEST/Contents/PlugIns/EXRThumbnail.appex" 2>/dev/null || true
    pluginkit -r "$DEST/Contents/PlugIns/EXRQuickLook.appex" 2>/dev/null || true
    "$LSREGISTER" -u "$DEST" || true
    rm -rf "$DEST"
    echo "Removed $DEST"
  else
    echo "Not installed at $DEST"
  fi
done

qlmanage -r cache >/dev/null 2>&1 || true
killall Finder 2>/dev/null || true
echo "Thumbnail cache reset and Finder restarted."

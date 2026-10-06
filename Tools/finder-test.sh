#!/bin/bash
#
# Creates a folder Finder has never cached thumbnails for, fills it with the
# spike fixtures, and opens it in icon view. A fresh folder matters: Quick Look
# caches thumbnails aggressively, and a stale cache entry looks exactly like
# "the extension was not invoked".
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIXTURES="$ROOT/Tests/Fixtures/spike"
DEST="$HOME/Desktop/exr-quicklook-test-$(date +%H%M%S)"

[ -d "$FIXTURES" ] || { echo "No fixtures -- run Tools/make-spike-fixtures.py first." >&2; exit 1; }

mkdir -p "$DEST"
cp "$FIXTURES"/* "$DEST/"
qlmanage -r cache >/dev/null 2>&1 || true

echo "Test folder: $DEST"
echo
echo "Expected: three rendered gradient thumbnails (one with overscan cropped)."
echo "Press space on any of them: expected the preview with the HUD buttons."
echo
echo "Delete when done:  rm -rf \"$DEST\""
open "$DEST"

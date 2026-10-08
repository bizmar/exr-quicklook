#!/bin/bash
# Builds the release disk image attached to GitHub Releases:
#   dist/EXR-Quick-Look-<version>.dmg
# A styled window: EXR Quick Look.app with an arrow to an Applications
# shortcut, and INSTALL.txt, LICENSE.txt and THIRD_PARTY_NOTICES.md below.
# The app is universal and ad-hoc signed -- this project is not notarised.
#
# Needs dmgbuild in build/dmg/venv (it writes Finder's layout without
# scripting Finder), pinned by hash:
#   python3 -m venv build/dmg/venv
#   build/dmg/venv/bin/pip install --require-hashes -r Tools/dmg-requirements.txt
#
# Published releases are built by CI on a v* tag, not by running this locally:
# CI attests the DMG and drafts the release (see .github/workflows/ci.yml).
#
# Runs the full test suite first unless --skip-tests is given: a release is the
# one build other people install.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
say() { printf '\033[1m==>\033[0m %s\n' "$*"; }

VERSION=$(plutil -extract CFBundleShortVersionString raw EXRPreview/Info.plist)
for p in EXRThumbnail/Info.plist EXRQuickLook/Info.plist; do
  v=$(plutil -extract CFBundleShortVersionString raw "$p")
  [ "$v" = "$VERSION" ] || { echo "version mismatch: $p is $v, app is $VERSION" >&2; exit 1; }
done
NAME="EXR-Quick-Look-$VERSION"

say "Building $VERSION"
Tools/build-core.sh >/dev/null
Tools/build-app.sh >/dev/null
APP="build/EXR Quick Look.app"
[ "$(lipo -archs "$APP/Contents/MacOS/EXRPreview")" = "x86_64 arm64" ] || { echo "not universal" >&2; exit 1; }
codesign --verify --deep --strict "$APP"

if [ "${1:-}" != "--skip-tests" ]; then
  say "Running the test suite"
  Tools/test-all.sh >/dev/null || { echo "tests failed -- not packaging" >&2; exit 1; }
fi

say "Staging"
STAGE="dist/$NAME"
rm -rf "$STAGE" "dist/$NAME.dmg" "dist/$NAME.zip"
mkdir -p "$STAGE"
ditto "$APP" "$STAGE/EXR Quick Look.app"
cp LICENSE "$STAGE/LICENSE.txt"   # an extension, so a double-click opens it
cp THIRD_PARTY_NOTICES.md "$STAGE/"
cat > "$STAGE/INSTALL.txt" <<TXT
EXR Quick Look $VERSION

1. Drag "EXR Quick Look" onto the Applications folder in this window.
2. Open it once. macOS will block it because it is not notarised. Then either
   - System Settings > Privacy & Security > scroll down > Open Anyway, or
   - in Terminal: xattr -dr com.apple.quarantine "/Applications/EXR Quick Look.app"
3. System Settings > General > Login Items & Extensions > Extensions >
   Quick Look (or EXR Quick Look, sorted by app): switch on both Preview and
   Thumbnail. The app's window has a button that opens this pane.

Existing Finder thumbnails may not change until you open a folder you have not
viewed since installing, or log out and back in.

https://github.com/bizmar/exr-quicklook
TXT
# No extended attributes from this machine should travel in the archive.
xattr -cr "$STAGE"

DMGBUILD="build/dmg/venv/bin/dmgbuild"
[ -x "$DMGBUILD" ] || { echo "dmgbuild missing -- see the header of this script" >&2; exit 1; }

say "Background"
xcrun swiftc -O -o build/dmg/make-dmg-background Tools/make-dmg-background/main.swift
build/dmg/make-dmg-background build/dmg >/dev/null
# One TIFF holding both resolutions, so Finder picks the sharp one on Retina.
tiffutil -cathidpicheck build/dmg/background.png build/dmg/background@2x.png \
  -out build/dmg/background.tiff >/dev/null 2>&1

say "Disk image"
"$DMGBUILD" -s Tools/dmg-settings.py -D stage="$STAGE" -D background=build/dmg/background.tiff \
  -D icon=EXRPreview/AppIcon.icns "EXR Quick Look $VERSION" "dist/$NAME.dmg" >/dev/null
hdiutil verify "dist/$NAME.dmg" >/dev/null
( cd dist && shasum -a 256 "$NAME.dmg" > "$NAME.dmg.sha256" )
say "dist/$NAME.dmg  ($(du -h "dist/$NAME.dmg" | cut -f1))"
cat "dist/$NAME.dmg.sha256"

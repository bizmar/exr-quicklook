#!/bin/bash
# Builds the release archive attached to GitHub Releases:
#   dist/EXR-Quick-Look-<version>.zip
# containing EXR Quick Look.app (universal, ad-hoc signed -- this project is not
# notarised), LICENSE, THIRD_PARTY_NOTICES.md and a short install note.
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
rm -rf "$STAGE" "dist/$NAME.zip"
mkdir -p "$STAGE"
ditto "$APP" "$STAGE/EXR Quick Look.app"
cp LICENSE THIRD_PARTY_NOTICES.md "$STAGE/"
cat > "$STAGE/INSTALL.txt" <<TXT
EXR Quick Look $VERSION

1. Move "EXR Quick Look.app" to /Applications (or ~/Applications).
2. Open it once. macOS will block it because it is not notarised. Then either
   - System Settings > Privacy & Security > scroll down > Open Anyway, or
   - in Terminal: xattr -dr com.apple.quarantine "/Applications/EXR Quick Look.app"
3. System Settings > General > Login Items & Extensions > EXR Quick Look
   Extensions: switch on both Preview and Thumbnail.

Existing Finder thumbnails may not change until you open a folder you have not
viewed since installing, or log out and back in.

https://github.com/bizmar/exr-quicklook
TXT
# No extended attributes from this machine should travel in the archive.
xattr -cr "$STAGE"

say "Archiving"
# ditto keeps the bundle intact (symlinks, permissions, signature) where plain
# zip can break it.
ditto -c -k --keepParent "$STAGE" "dist/$NAME.zip"
( cd dist && shasum -a 256 "$NAME.zip" > "$NAME.zip.sha256" )
say "dist/$NAME.zip  ($(du -h "dist/$NAME.zip" | cut -f1))"
cat "dist/$NAME.zip.sha256"

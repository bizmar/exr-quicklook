#!/bin/bash
#
# Points the Homebrew cask (github.com/bizmar/homebrew-tap) at a published
# release: sets the version and the DMG's SHA-256, audits the cask against the
# real download, commits and pushes.
#
#   Tools/bump-tap.sh 0.3.3
#   DRY_RUN=1 Tools/bump-tap.sh 0.3.3    # everything but the commit and push
#
# Run only after the release is published (the cask must never point at a
# draft). The SHA-256 is read from the release's own .sha256 asset, then
# checked against the downloaded DMG by brew audit.
#
set -euo pipefail
VERSION="${1:?usage: Tools/bump-tap.sh <version>}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO=bizmar/exr-quicklook
TAP_REPO=bizmar/homebrew-tap
WORK="$ROOT/build/homebrew-tap-$VERSION-$(date +%H%M%S)"
CASK=Casks/exr-quicklook.rb
GIT=(git -c credential.helper= -c "credential.helper=!gh auth git-credential")

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }

[ "$(gh release view "v$VERSION" -R "$REPO" --json isDraft --jq .isDraft)" = false ] ||
  { echo "v$VERSION is not a published release." >&2; exit 1; }

say "SHA-256 from the v$VERSION release"
mkdir -p "$WORK"
gh release download "v$VERSION" -R "$REPO" -p "EXR-Quick-Look-$VERSION.dmg.sha256" -D "$WORK"
SHA=$(cut -d' ' -f1 "$WORK/EXR-Quick-Look-$VERSION.dmg.sha256")
[[ "$SHA" =~ ^[0-9a-f]{64}$ ]] || { echo "bad sha256: $SHA" >&2; exit 1; }
echo "    $SHA"

say "Updating $TAP_REPO"
"${GIT[@]}" clone -q "https://github.com/$TAP_REPO.git" "$WORK/tap"
cd "$WORK/tap"
sed -i '' -E "s/^  version \".*\"/  version \"$VERSION\"/; s/^  sha256 \".*\"/  sha256 \"$SHA\"/" "$CASK"
git diff --stat
git diff --quiet && { echo "    already at $VERSION"; exit 0; }

say "Auditing the cask against the real download"
TAP_NAME=bizmar/tapcheck
HOMEBREW_NO_AUTO_UPDATE=1 brew untap "$TAP_NAME" >/dev/null 2>&1 || true
git -c user.name=check -c user.email=check@localhost commit -qam check
HOMEBREW_NO_AUTO_UPDATE=1 brew tap "$TAP_NAME" "$WORK/tap" >/dev/null
trap 'HOMEBREW_NO_AUTO_UPDATE=1 brew untap "$TAP_NAME" >/dev/null 2>&1 || true' EXIT
HOMEBREW_NO_AUTO_UPDATE=1 brew audit --cask --online --strict "$TAP_NAME/exr-quicklook"
git reset -q --soft HEAD~1

[ -n "${DRY_RUN:-}" ] && { say "DRY_RUN: audited, not committed or pushed"; exit 0; }

say "Committing and pushing"
git -c user.name="Mark Bizilj" -c user.email=4053708+bizmar@users.noreply.github.com \
    commit -qam "EXR Quick Look $VERSION

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
"${GIT[@]}" push -q origin main
say "Cask now at $VERSION: brew install --cask bizmar/tap/exr-quicklook"

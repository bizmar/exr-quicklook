#!/bin/bash
#
# Fetches the official OpenEXR test corpus (plan §9, item 1).
#
# BSD-3-Clause, copyright ILM -- compatible with D10. GitHub reports
# "NOASSERTION" only because LICENSE is not a verbatim SPDX match.
#
# The full repository is ~234 MB, so this is a shallow, blob-filtered, sparse
# checkout of the directories that actually exercise our code, into Vendor/
# which is gitignored. Nothing large ever enters this repository; when the
# project becomes a git repo this should become a submodule as §9 intends.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/Vendor/openexr-images"
REPO=https://github.com/AcademySoftwareFoundation/openexr-images.git

# Chosen for what each exercises, not for coverage of the whole repo:
DIRS=(
  Chromaticities    # files declaring different primaries
  DisplayWindow     # overscan / display-vs-data window (D6)
  Damaged           # malformed -- must fail cleanly (§6.6)
  TestImages        # NaN, infinity, out-of-gamut
  LuminanceChroma   # Y/RY/BY -- must refuse loudly, we do not decode it
  MultiResolution   # tiled and mipmapped
  Beachball         # the singlepart/multipart pair of the same sequence
  v2                # multi-part v2 features
)

if [ -d "$DEST/.git" ]; then
  echo "==> Already present at $DEST"
else
  echo "==> Cloning (shallow, blob-filtered) into $DEST"
  git clone --depth 1 --filter=blob:none --sparse "$REPO" "$DEST"
fi

echo "==> Sparse checkout: ${DIRS[*]}"
git -C "$DEST" sparse-checkout set "${DIRS[@]}"

echo
echo "==> Fetched"
du -sh "$DEST" | sed 's/^/    /'
find "$DEST" -name '*.exr' | wc -l | sed 's/^/    EXR files: /'

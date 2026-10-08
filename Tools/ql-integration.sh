#!/bin/bash
# End-to-end check on a real Mac: Quick Look itself must produce thumbnails and
# previews through *our* extensions, for files macOS cannot read on its own.
#
#   Tools/ql-integration.sh [--enable]
#
# Expects the app installed by Tools/install.sh. --enable switches the
# extensions on with pluginkit: for CI machines only. On your own Mac, switch
# them on in System Settings instead.
#
# Proof that the extension did the work, not macOS: each rendered file must
# appear in the extensions' own log ("rendered <file>" from the thumbnailer,
# "prepared <file>" from the previewer). DWAA files are included because
# macOS's decoder cannot read them at all.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
IDS=(io.github.bizmar.exr-quicklook.Thumbnail io.github.bizmar.exr-quicklook.Preview)
FILES=(Tests/Fixtures/corpus/dwaa-multilayer-acescg.exr
       Tests/Fixtures/corpus/dwab-multilayer-acescg.exr
       Tests/Fixtures/corpus/no-chromaticities.exr
       Tests/Fixtures/real/LCF_05-01_01_X1_0039_AMaZE.exr)
LOG="$ROOT/build/ql-integration.log"
fail=0
say() { printf '\033[1m==>\033[0m %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }

say "System"
sw_vers | sed 's/^/    /'
printf '    arch: %s\n' "$(uname -m)"

say "Probes (built for this machine)"
mkdir -p build
for p in qlprobe qlpreviewprobe utitool; do
  xcrun swiftc -O -o "build/$p" "Tools/$p/main.swift" || { bad "cannot build $p"; exit 1; }
done

if [ "${1:-}" = "--enable" ]; then
  say "Enabling the extensions (CI only)"
  for id in "${IDS[@]}"; do pluginkit -e use -i "$id"; done
  sleep 2
fi

say "Registration"
for id in "${IDS[@]}"; do
  line=$(pluginkit -m -i "$id" 2>/dev/null)
  echo "    ${line:-$id: not registered}"
  # "+" is switched on by the user, blank is on by default, "-" is off.
  if [ -z "$line" ]; then bad "$id is not registered"
  elif [[ "$line" == -* ]]; then bad "$id is switched off"; fi
done
say "Type macOS assigns"
build/utitool "${FILES[0]}" | sed 's/^/    /'

say "Quick Look, watched through the extensions' log"
# A fresh copy in a new folder, so no cached thumbnail can answer instead.
work=$(mktemp -d "${TMPDIR:-/tmp}/ql-integration.XXXXXX")
for f in "${FILES[@]}"; do cp "$f" "$work/"; done
qlmanage -r >/dev/null 2>&1; qlmanage -r cache >/dev/null 2>&1
/usr/bin/log stream --info --style compact \
  --predicate 'subsystem == "io.github.bizmar.exr-quicklook"' > "$LOG" 2>&1 &
logger=$!
sleep 3

for f in "$work"/*.exr; do
  out=$(build/qlprobe "$f" 2>&1)
  echo "$out" | sed 's/^/    /'
  echo "$out" | grep -q "rendered" || bad "no thumbnail for $(basename "$f")"
done
build/qlpreviewprobe "$work"/*.exr 2>&1 | sed 's/^/    /'
sleep 3
kill "$logger" 2>/dev/null; wait "$logger" 2>/dev/null

say "What the extensions logged"
grep -E "rendered |prepared " "$LOG" | sed 's/^.*\] /    /' | sort -u
for f in "${FILES[@]}"; do
  n=$(basename "$f")
  grep -q "rendered $n" "$LOG" || bad "thumbnail extension never rendered $n"
  grep -q "prepared $n" "$LOG" || bad "preview extension never prepared $n"
done

if [ "$fail" -eq 0 ]; then say "Quick Look uses our extensions on this Mac"; else say "integration check FAILED"; fi
exit "$fail"

#!/bin/bash
#
# Phase 0 restart test. Run AFTER a logout+login or a reboot, with no
# reinstall and no re-registration first -- that is the whole point.
#
# Answers: does the extension registration survive a session restart?
#
# Writes a transcript to build/restart-test-<timestamp>.txt
#
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
STAMP=$(date +%Y%m%d-%H%M%S)
OUT="$ROOT/build/restart-test-$STAMP.txt"
LOG="$ROOT/build/restart-test-$STAMP.log"
mkdir -p "$ROOT/build"

exec > >(tee "$OUT") 2>&1

hr() { printf '\n=== %s ===\n' "$*"; }
PASS=0; FAIL=0
check() { # check <label> <condition-result 0/1>
  if [ "$2" = 0 ]; then printf '  PASS  %s\n' "$1"; PASS=$((PASS+1))
  else printf '  FAIL  %s\n' "$1"; FAIL=$((FAIL+1)); fi
}

for h in qlprobe qlpreviewprobe; do
  [ -x "$ROOT/build/$h" ] || { echo "Missing build/$h -- run Tools/build-spike.sh first."; exit 1; }
done

hr "Environment"
echo "  date:        $(date)"
echo "  uptime:      $(uptime | sed 's/^ *//')"
echo "  boot time:   $(sysctl -n kern.boottime | sed 's/.*} //')"
echo "  macOS:       $(sw_vers -productVersion) ($(sw_vers -buildVersion))"
echo "  app present: $([ -d "$HOME/Applications/EXR Quick Look.app" ] && echo yes || echo NO)"

hr "1. Registration survived?"
pluginkit -mAvvv 2>/dev/null | awk '
  NF <= 2 && $NF ~ /\(.*\)$/ { id = $NF }
  /SDK = com.apple.quicklook.thumbnail/ { print "  [thumbnail] " id }
  /SDK = com.apple.quicklook.preview/   { print "  [preview]   " id }
' | grep -i bizmar.exr-quicklook
REG=$?
check "both extensions still registered with PluginKit" $REG

hr "2. Enablement survived?"
pluginkit -mAv 2>/dev/null | grep -i bizmar.exr-quicklook | cut -c1-72
ENABLED=$(pluginkit -mAv 2>/dev/null | grep -ci '^+.*bizmar.exr-quicklook')
check "both extensions still enabled (found $ENABLED with '+')" \
      $([ "$ENABLED" = 2 ] && echo 0 || echo 1)

hr "3. Thumbnail extension still invoked for .exr?"
qlmanage -r cache >/dev/null 2>&1
killall -9 EXRThumbnail EXRQuickLook quicklookd 2>/dev/null
w=0; until [ $w -ge 3 ]; do sleep 1; w=$((w+1)); done
THUMB=$(./build/qlprobe Tests/Fixtures/spike/spike-acescg.exr 2>&1)
echo "$THUMB" | sed 's/^/  /'
echo "$THUMB" | grep -q "SPIKE RED"
check "thumbnail extension invoked for com.ilm.openexr-image" $?

hr "4. Preview extension still invoked for .exr?"
: > "$LOG"
/usr/bin/log stream --predicate 'subsystem == "io.github.bizmar.exr-quicklook"' \
  --info --debug --style compact > "$LOG" 2>&1 &
STREAM=$!
w=0; until [ $w -ge 4 ]; do sleep 1; w=$((w+1)); done
./build/qlpreviewprobe Tests/Fixtures/spike/spike-acescg.exr >/dev/null 2>&1
w=0; until [ $w -ge 3 ]; do sleep 1; w=$((w+1)); done
kill $STREAM 2>/dev/null; wait $STREAM 2>/dev/null
grep -o 'SPIKE-PREVIEW invoked file=[^ ]*' "$LOG" | sed 's|.*/|  → |' || echo "  (none)"
grep -q 'SPIKE-PREVIEW' "$LOG"
check "preview extension invoked for com.ilm.openexr-image" $?

hr "Result"
printf '  %d passed, %d failed\n' "$PASS" "$FAIL"
if [ "$FAIL" = 0 ]; then
  echo "  Registration SURVIVED the restart. Phase 0 checklist complete."
else
  echo "  Something did NOT survive. Details above; log at $LOG"
  echo "  Re-registering with Tools/install-spike.sh and retesting will show"
  echo "  whether it is a registration-persistence problem or something else."
fi
echo
echo "  Transcript: $OUT"
echo
echo "  Finder check (do this by eye): Tools/spike-finder-test.sh"
echo "  Expect four solid red squares; spacebar gives a solid blue panel."

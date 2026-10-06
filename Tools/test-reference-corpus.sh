#!/bin/bash
#
# Runs the official OpenEXR test corpus (plan §9 item 1) through EXRCLI.
#
# Expectations encode what the corpus's own READMEs say, not what we happen to
# do. Fetch it first with Tools/fetch-openexr-images.sh.
#
# macOS has no `timeout`; the deadline uses perl's alarm.
#
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
C="$ROOT/Vendor/openexr-images"
CLI="$ROOT/build/exrcli"
[ -x "$CLI" ] || { echo "Run Tools/build-cli.sh first." >&2; exit 1; }
[ -d "$C" ] || { echo "Run Tools/fetch-openexr-images.sh first." >&2; exit 1; }

run() { /usr/bin/perl -e 'alarm 15; exec @ARGV' "$CLI" --png /dev/null "$1" 2>&1; }
refused() { printf '%s' "$1" | grep -q -e REJECTED -e 'DECODE FAILED' -e 'none selectable'; }

pass=0; fail=0
note() { printf '  %-4s %-38s %s\n' "$1" "$2" "$3"; }

# $1 dir  $2 expect: render|refuse  $3 why
sweep() {
  local dir="$1" expect="$2" why="$3"
  local ok=0 ref=0 crash=0
  for f in $(find "$C/$dir" -name '*.exr' 2>/dev/null | sort); do
    out=$(run "$f"); rc=$?
    if [ "$rc" -ge 128 ]; then crash=$((crash+1))
    elif refused "$out"; then ref=$((ref+1))
    else ok=$((ok+1)); fi
  done
  if [ "$crash" -gt 0 ]; then
    note FAIL "$dir" "$crash crashed"; fail=$((fail+1)); return
  fi
  case "$expect" in
    render) if [ "$ref" -eq 0 ] && [ "$ok" -gt 0 ]; then
              note ok "$dir" "$ok rendered — $why"; pass=$((pass+1))
            else note FAIL "$dir" "$ref refused, expected all to render"; fail=$((fail+1)); fi ;;
    refuse) if [ "$ok" -eq 0 ] && [ "$ref" -gt 0 ]; then
              note ok "$dir" "$ref refused — $why"; pass=$((pass+1))
            else note FAIL "$dir" "$ok rendered, expected all to be refused"; fail=$((fail+1)); fi ;;
  esac
}

echo "--- valid imagery must render ---"
sweep TestImages      render "NaN, infinity, out-of-gamut"
sweep MultiResolution render "tiled and mipmapped"
sweep Beachball       render "singlepart/multipart pair"

echo
echo "--- display/data window handling (D6) ---"
# t09-t12 do NOT overlap; DisplayWindow/README.rst says the display window
# should be entirely background. That is a render, not an error.
dw_ok=0; dw_bad=0
for f in "$C"/DisplayWindow/*.exr; do
  out=$(run "$f")
  if refused "$out"; then dw_bad=$((dw_bad+1)); note FAIL "$(basename "$f")" "refused"
  else dw_ok=$((dw_ok+1)); fi
done
if [ "$dw_bad" -eq 0 ]; then
  note ok "DisplayWindow" "$dw_ok rendered incl. non-overlapping t09-t12"; pass=$((pass+1))
else fail=$((fail+1)); fi

echo
echo "--- deep and luminance-chroma must be refused, never faked ---"
# v2 holds deep scanline parts plus a flat composited.exr per view. The deep
# parts must be refused (non-goal); the composited flats must render. A blanket
# "refuse everything in v2" was tried and was wrong.
v2_deep_ok=0; v2_deep_bad=0; v2_flat_ok=0; v2_flat_bad=0
for f in $(find "$C/v2" -name '*.exr' | sort); do
  out=$(run "$f")
  case "$(basename "$f")" in
    composited.exr)
      if refused "$out"; then v2_flat_bad=$((v2_flat_bad+1))
      else v2_flat_ok=$((v2_flat_ok+1)); fi ;;
    *)
      if refused "$out"; then v2_deep_ok=$((v2_deep_ok+1))
      else v2_deep_bad=$((v2_deep_bad+1)); fi ;;
  esac
done
if [ "$v2_deep_bad" -eq 0 ] && [ "$v2_deep_ok" -gt 0 ]; then
  note ok "v2 deep parts" "$v2_deep_ok refused — deep is a non-goal"; pass=$((pass+1))
else note FAIL "v2 deep parts" "$v2_deep_bad deep files rendered"; fail=$((fail+1)); fi
if [ "$v2_flat_bad" -eq 0 ] && [ "$v2_flat_ok" -gt 0 ]; then
  note ok "v2 composited" "$v2_flat_ok flat composites rendered"; pass=$((pass+1))
else note FAIL "v2 composited" "$v2_flat_bad flat composites refused"; fail=$((fail+1)); fi

lc_ref=0
for f in "$C"/LuminanceChroma/*.exr "$C"/Chromaticities/*_YC.exr; do
  [ -f "$f" ] || continue
  out=$(run "$f")
  refused "$out" && lc_ref=$((lc_ref+1))
done
if [ "$lc_ref" -ge 6 ]; then
  note ok "LuminanceChroma" "$lc_ref Y/RY/BY files refused loudly"; pass=$((pass+1))
else note FAIL "LuminanceChroma" "only $lc_ref refused"; fail=$((fail+1)); fi

echo
echo "--- the ASAN / ClusterFuzz corpus must never crash or hang ---"
dc=0; dr=0; dref=0; dslow=0
for f in "$C"/Damaged/*; do
  case "$f" in *README*) continue;; esac
  out=$(run "$f"); rc=$?
  if [ "$rc" -eq 142 ]; then dslow=$((dslow+1))
  elif [ "$rc" -ge 128 ]; then dc=$((dc+1))
  elif refused "$out"; then dref=$((dref+1))
  else dr=$((dr+1)); fi
done
if [ "$dc" -eq 0 ] && [ "$dslow" -eq 0 ]; then
  note ok "Damaged" "$dref refused, $dr rendered, 0 crashes, 0 hangs"; pass=$((pass+1))
else note FAIL "Damaged" "$dc crashed, $dslow timed out"; fail=$((fail+1)); fi

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]

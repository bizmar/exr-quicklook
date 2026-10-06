#!/bin/bash
#
# Every file in the malformed corpus must be rejected cleanly: exit status 0 or
# 1 (a reported rejection), never a signal, and never longer than the deadline.
# A crash here is the CVE-2026-28977 failure mode reproducing in our own code.
#
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORPUS="$ROOT/Tests/Fixtures/malformed"
CLI="$ROOT/build/exrcli"
TIMEOUT=5

[ -x "$CLI" ] || { echo "Run Tools/build-cli.sh first." >&2; exit 1; }
[ -d "$CORPUS" ] || { echo "Run Tools/make-malformed-fixtures.py first." >&2; exit 1; }

pass=0; fail=0
for f in "$CORPUS"/*.exr; do
  name=$(basename "$f")
  start=$(date +%s)
  # --png exercises the decode path too, not just header parsing.
  out=$("$CLI" --png /dev/null "$f" 2>&1); status=$?
  elapsed=$(( $(date +%s) - start ))

  if [ $status -ge 128 ]; then
    printf '  CRASH   %-32s signal %d\n' "$name" $((status - 128)); fail=$((fail+1))
  elif [ $elapsed -ge $TIMEOUT ]; then
    printf '  SLOW    %-32s %ds\n' "$name" "$elapsed"; fail=$((fail+1))
  elif echo "$out" | grep -qE 'REJECTED|DECODE FAILED'; then
    reason=$(echo "$out" | grep -oE '(REJECTED|DECODE FAILED): .*' | head -1 | cut -c1-64)
    printf '  ok      %-32s %s\n' "$name" "$reason"; pass=$((pass+1))
  else
    printf '  ACCEPTED %-31s (should have been rejected)\n' "$name"; fail=$((fail+1))
  fi
done

echo
printf '  %d rejected cleanly, %d problems\n' "$pass" "$fail"
[ "$fail" -eq 0 ]

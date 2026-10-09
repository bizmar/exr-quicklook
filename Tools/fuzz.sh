#!/bin/bash
# Fuzzes EXRCore under AddressSanitizer + UndefinedBehaviorSanitizer.
#   Tools/fuzz.sh [seconds=300] [workers=4]
# Seeds: the fixture corpus plus small openexr-images files if fetched.
# Crashers (input + report) are kept in build/fuzz/crashes/; replay one, with
# the same sanitizers and suppressions, with build/fuzz/replay <file>.
# Links the sanitized OpenEXR from Tools/build-openexr-asan.sh when it exists,
# so faults inside OpenEXR are caught too, UB included; otherwise the release
# build, where only a segfault shows.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INS="$ROOT/Vendor/openexr/install-asan"
[ -f "$INS/lib/libOpenEXR-3_4.a" ] || INS="$ROOT/Vendor/openexr/install"
OUT="$ROOT/build/fuzz"
SECS="${1:-300}"; WORKERS="${2:-4}"
[ -f "$INS/lib/libOpenEXR-3_4.a" ] || { echo "Run Tools/build-openexr.sh first." >&2; exit 1; }
mkdir -p "$OUT/crashes"

c++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -fno-omit-frame-pointer -I "$ROOT/EXRCore/include" \
    -isystem "$INS/include" -isystem "$INS/include/OpenEXR" -isystem "$INS/include/Imath" \
    "$ROOT"/EXRCore/src/*.cpp "$ROOT/Tools/fuzz/main.cpp" \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRCore-3_4 -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 \
    -o "$OUT/fuzz"
# The same calls without mutation, to replay a crasher: build/fuzz/replay <file>
c++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -fno-omit-frame-pointer -I "$ROOT/EXRCore/include" \
    -isystem "$INS/include" -isystem "$INS/include/OpenEXR" -isystem "$INS/include/Imath" \
    "$ROOT"/EXRCore/src/*.cpp "$ROOT/Tools/fuzz/replay.cpp" \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRCore-3_4 -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 \
    -o "$OUT/replay"

seeds=()
while IFS= read -r f; do seeds+=("$f"); done < <(
  find "$ROOT/Tests/Fixtures/corpus" "$ROOT/Tests/Fixtures/spike" -name '*.exr' -size -2M
  find "$ROOT/Vendor/openexr-images" -name '*.exr' -size -300k 2>/dev/null | head -80)
echo "${#seeds[@]} seeds, $WORKERS workers, ${SECS}s, OpenEXR from $(basename "$INS")"

for w in $(seq 1 "$WORKERS"); do
  (
    end=$((SECONDS + SECS)); n=0
    while [ $SECONDS -lt $end ]; do
      if ! ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:suppressions="$ROOT/Tools/fuzz/ubsan-openexr.supp" \
           "$OUT/fuzz" "$OUT/work$w.exr" $((end - SECONDS)) "$RANDOM$w" "${seeds[@]}" \
           > "$OUT/log$w.txt" 2>&1; then
        n=$((n + 1))
        cp "$OUT/work$w.exr" "$OUT/crashes/w$w-$n.exr"
        grep -av "work$w.exr:" "$OUT/log$w.txt" > "$OUT/crashes/w$w-$n.log" || true
      fi
    done
    echo "worker $w: $(grep -ao 'iterations [0-9]*, opened [0-9]*' "$OUT/log$w.txt" || echo 'no summary'), $n crashes"
  ) &
done
wait
ls "$OUT/crashes" | grep -q . && { echo "crashers in $OUT/crashes"; exit 1; } || echo "no crashes"

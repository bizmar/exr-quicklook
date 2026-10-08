#!/bin/bash
# Everything, in dependency order. Safe to run from a clean checkout after
# Tools/build-openexr.sh has populated Vendor/.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

hr() { printf '\n\033[1m--- %s ---\033[0m\n' "$*"; }

hr "unit tests"
Tools/run-tests.sh

hr "corpus (real OpenEXR-written files)"
# Regenerated when any fixture added since the corpus was last written is missing.
[ -f Tests/Fixtures/corpus/pq-bands.exr ] || build/make-fixtures Tests/Fixtures/corpus
Tools/test-corpus.sh

hr "malformed corpus"
[ -f Tests/Fixtures/malformed/empty.exr ] || Tools/make-malformed-fixtures.py
Tools/test-malformed.sh

hr "official OpenEXR reference corpus"
if [ -d Vendor/openexr-images ]; then
  Tools/test-reference-corpus.sh
else
  echo "  skipped — run Tools/fetch-openexr-images.sh (not committed, ~380 MB)"
fi

hr "D7 — thumbnail and preview identity"
if [ -f build/lib/libEXRCore.a ]; then
  INS="$ROOT/Vendor/openexr/install"
  c++ -std=c++17 -O2 -I EXRCore/include EXRCore/tests/test_d7.cpp build/lib/libEXRCore.a \
      -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
      -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -o build/test_d7
  build/test_d7 Tests/Fixtures/corpus/dwaa-multilayer-acescg.exr
else
  echo "  skipped (run Tools/build-core.sh)"
fi

hr "input-colourspace override changes pixels"
INS="$ROOT/Vendor/openexr/install"
c++ -std=c++17 -O1 -I EXRCore/include EXRCore/tests/test_input_override.cpp build/lib/libEXRCore.a \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
    -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -o build/test_input_override
build/test_input_override Tests/Fixtures/corpus

hr "data passes render raw"
c++ -std=c++17 -O1 -I EXRCore/include EXRCore/tests/test_raw_view.cpp build/lib/libEXRCore.a \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
    -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -o build/test_raw_view
build/test_raw_view Tests/Fixtures/corpus

hr "colour tags: colorInteropID, arnold/color_space"
c++ -std=c++17 -O1 -I EXRCore/include EXRCore/tests/test_interop.cpp build/lib/libEXRCore.a \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
    -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -o build/test_interop
build/test_interop Tests/Fixtures/corpus

hr "PQ HDR masters"
c++ -std=c++17 -O1 -I EXRCore/include EXRCore/tests/test_pq.cpp build/lib/libEXRCore.a \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
    -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -o build/test_pq
build/test_pq Tests/Fixtures/corpus

hr "golden images (Tests/Golden)"
c++ -std=c++17 -O1 -I EXRCore/include EXRCore/tests/test_golden.cpp build/lib/libEXRCore.a \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
    -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -lz -o build/test_golden
# After a deliberate pixel change: add --update, then review the changed PNGs.
build/test_golden Tests/Golden Tests/Fixtures/corpus Tests/Fixtures/real Tests/Fixtures/spike

hr "update checker (host app)"
xcrun swiftc -parse-as-library -O EXRPreview/UpdateChecker.swift Tools/test-update-checker.swift \
    -o build/test_update_checker
build/test_update_checker

hr "window renders in every update state"
xcrun swiftc -parse-as-library -D SNAPSHOT EXRPreview/EXRPreviewApp.swift EXRPreview/UpdateChecker.swift \
    Tools/snapshot-window.swift -o build/snapshot-window
# ImageRenderer aborts inside Metal on Intel virtual machines (GitHub's Intel
# runners), though it reports a device there; the app itself is unaffected.
if [ "$(uname -m)" = x86_64 ] && [ "$(sysctl -n kern.hv_vmm_present 2>/dev/null)" = 1 ]; then
  echo "  skipped: Intel virtual machine (Metal cannot render offscreen here)"
else
  mkdir -p build/snapshots && build/snapshot-window build/snapshots
fi

hr "hostile input: file swap, forged info rows, layer flood"
mkdir -p build/hostile
c++ -std=c++17 -O1 -I EXRCore/include -I "$INS/include" -I "$INS/include/OpenEXR" -I "$INS/include/Imath" \
    EXRCore/tests/test_hostile.cpp build/lib/libEXRCore.a \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
    -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -o build/test_hostile
build/test_hostile build/hostile

hr "LUT provenance + runtime vs OCIO"
if [ -x "${PYOCIO:-/opt/homebrew/bin/python3.14}" ] && \
   "${PYOCIO:-/opt/homebrew/bin/python3.14}" -c "import PyOpenColorIO" 2>/dev/null; then
  Tools/verify-lut.sh
else
  echo "  skipped (no PyOpenColorIO -- build-time only, not needed to run the app)"
fi

hr "all suites passed"

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
      -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -lopenjph -o build/test_d7
  build/test_d7 Tests/Fixtures/corpus/dwaa-multilayer-acescg.exr
else
  echo "  skipped (run Tools/build-core.sh)"
fi

hr "input-colourspace override changes pixels"
INS="$ROOT/Vendor/openexr/install"
c++ -std=c++17 -O1 -I EXRCore/include EXRCore/tests/test_input_override.cpp build/lib/libEXRCore.a \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
    -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -lopenjph -o build/test_input_override
build/test_input_override Tests/Fixtures/corpus

hr "data passes render raw"
c++ -std=c++17 -O1 -I EXRCore/include EXRCore/tests/test_raw_view.cpp build/lib/libEXRCore.a \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
    -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -lopenjph -o build/test_raw_view
build/test_raw_view Tests/Fixtures/corpus

hr "colour tags: colorInteropID, arnold/color_space"
c++ -std=c++17 -O1 -I EXRCore/include EXRCore/tests/test_interop.cpp build/lib/libEXRCore.a \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
    -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -lopenjph -o build/test_interop
build/test_interop Tests/Fixtures/corpus

hr "PQ HDR masters"
c++ -std=c++17 -O1 -I EXRCore/include EXRCore/tests/test_pq.cpp build/lib/libEXRCore.a \
    -L "$INS/lib" -lOpenEXR-3_4 -lOpenEXRUtil-3_4 -lOpenEXRCore-3_4 \
    -lIlmThread-3_4 -lIex-3_4 -lImath-3_2 -lopenjph -o build/test_pq
build/test_pq Tests/Fixtures/corpus

hr "LUT provenance + runtime vs OCIO"
if [ -x "${PYOCIO:-/opt/homebrew/bin/python3.14}" ] && \
   "${PYOCIO:-/opt/homebrew/bin/python3.14}" -c "import PyOpenColorIO" 2>/dev/null; then
  Tools/verify-lut.sh
else
  echo "  skipped (no PyOpenColorIO -- build-time only, not needed to run the app)"
fi

hr "all suites passed"

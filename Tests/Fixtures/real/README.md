# Real-world fixtures

Production files, kept because synthetic fixtures do not exercise the same
paths. Small enough to keep in-tree; large additions should go to `Vendor/`
and be fetched instead.

| File | Why it is here |
|---|---|
| `LCF_05-01_01_X1_0039_AMaZE.exr` | `rawtoaces` output. DWAA level 15, ACES2065-1 (AP0) chromaticities, and genuine overscan — dataWindow 6022×4024 against a 6000×4000 displayWindow. The only fixture that exercises D6 with a real plate, and the reference for the §6.7 performance budget. |

The plate's camera and lens serial numbers and all capture date/time fields
were removed before publishing (2026-10-06). The header was rewritten with
`OutputFile::copyPixels`, which copies the compressed DWAA chunks verbatim, so
pixel data is bit-identical to the original and every test reading this file
is unaffected. Camera model, lens model and exposure settings remain.

# Testing: what is covered, and what is not

An honest account. This project was built with an AI coding assistant (see the
README disclaimer), so it matters to say exactly what has been checked and by
what means, rather than imply more confidence than exists.

**If you can fill a gap below, please
[open a test report](https://github.com/bizmar/exr-quicklook/issues/new?template=test-report.yml).**
"It worked on my files with macOS 15 on an Intel Mac" is useful. So is "it
showed the wrong layer for a Redshift render": attach the output of
`build/exrcli -v yourfile.exr` (header only, no pixels) if you can.

## Automated: `Tools/test-all.sh`

All of these pass as of 2026-10-06, on macOS 27.0.1, Apple silicon.

| Suite | What it checks | Size |
|---|---|---|
| Layer selection (unit) | Which layer and part becomes the default; that masks, depth, position, motion, normals and cryptomatte never do; Nuke/Blender/Arnold/V-Ray/Redshift naming; data-only files | 139 checks |
| Colour (unit) | Primaries → AP1 matrices and Bradford adaptation, against OCIO | 40 checks |
| Corpus | Files written with the real OpenEXR library: DWAA, DWAB, multi-part, deep, overscan, luminance-chroma, 32-bit float, 43-channel AOV stacks, data passes, plus one real camera plate (DWAA, AP0, overscan) | 33 checks |
| Malformed files | Truncated files, absurd and overflowing data/display windows, wrong magic — all must be rejected cleanly | 13 files |
| Official OpenEXR images | The ASWF `openexr-images` collection: valid images render, deep and Y/RY/BY are refused, display-window edge cases | 77 files |
| Damaged / fuzz corpus | The OpenEXR project's ASAN/ClusterFuzz crash files: must never crash or hang | 185 files, 0 crashes, 0 hangs |
| Thumbnail = preview (D7) | Both code paths produce identical pixels | byte comparison |
| Input colourspace override | The override changes the rendered pixels, on tagged and untagged files | 8 byte comparisons |
| Data passes / Raw view | Position, depth and motion render untransformed; greyscale is grey | 14 byte comparisons |
| `colorInteropID` | The OpenEXR 3.4 colour ID is honoured, outranked by `chromaticities`, and log/display IDs are ignored | 13 byte comparisons |
| ACES tables | The baked LUTs re-bake to the same hashes, and the shipped runtime matches OCIO within tolerance | hash + numeric |

Every render check compares **pixels**, not settings. That rule exists because
two real bugs (an override that changed nothing, and greyscale layers rendering
blue) once passed checks that only looked at settings or text.

## Checked by hand or with probes, on real Quick Look

- Thumbnails and previews served by our extensions, not Apple's, verified with
  `build/qlprobe` (thumbnails via `QLThumbnailGenerator`, the API Finder uses)
  and `build/qlpreviewprobe` / `build/qlpanelprobe` (a real preview view and the
  real spacebar panel).
- Registration surviving a reboot (4/4, macOS 26.6.2) and a log-out (macOS 27).
- Settings carrying over while arrowing through a sequence, including frames
  Quick Look built ahead of time. Confirmed by the author by hand.
- The comparison images in the README are real Quick Look captures.

## Not tested — help wanted

- **macOS 14 and 15.** The app declares macOS 14 as its minimum; only 26 and 27
  have been run.
- **Intel Macs.** The build is universal, but the x86_64 half has never run.
- **Spotlight, and Open/Save dialogs.** Whether they use the extensions at all.
- **Finder's column view and preview pane** on recent macOS. They use the same
  preview code, but have not been rechecked since early development.
- **Real renders from specific applications.** Layer and data-pass detection
  is tested against synthetic files named the way renderers name things, and
  one real Netflix render. Real Redshift, V-Ray, Karma, Arnold, Blender, Octane
  and RenderMan multi-layer files have not been tried. A layer-picking mistake
  here is the most likely real-world bug.
- **Non-ACES pipelines.** Untagged files are assumed ACEScg. How often that is
  wrong in practice (Blender's linear Rec.709, for example) is not known.
- **Very large files.** The performance budget (thumbnail < 300 ms, preview
  < 500 ms on a 4K DWAA frame) was met early on but has not been re-measured
  since recent changes. 8K and 16K frames are untested.
- **Network volumes and slow disks.** Behaviour against the 2-second decode
  deadline.
- **Multiple displays and wide-gamut or HDR monitors.** Output is SDR Display
  P3, converted by ColorSync; only Apple displays have been looked at.
- **Golden-image regression and continuous fuzzing.** Planned, not built. There
  is no CI yet: the suites run locally.

## Known not to work (by design)

Deep images, luminance-chroma (`Y`/`RY`/`BY`) files and cryptomatte-only files
keep the generic icon. HDR output is not implemented.

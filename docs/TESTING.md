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

All of these pass as of 2026-10-08, on macOS 27.0.1, Apple silicon.

| Suite | What it checks | Size |
|---|---|---|
| Layer selection (unit) | Which layer and part becomes the default; that masks, depth, position, motion, normals and cryptomatte never do; Nuke/Blender/Arnold/V-Ray/Redshift naming; data-only files | 139 checks |
| Real-world layer names | The layer rules run over the part/channel names of 291 openly licensed production and test files (Netflix, Psyop, Blender, Poly Haven, Gaffer, OpenImageIO…), with invariants and a full snapshot. See [LAYER-RULES.md](LAYER-RULES.md) | 1669 checks |
| Colour (unit) | Primaries → AP1 matrices and Bradford adaptation, against OCIO | 40 checks |
| Corpus | Files written with the real OpenEXR library: DWAA, DWAB, multi-part, deep, overscan, luminance-chroma, 32-bit float, 43-channel AOV stacks, data passes, plus one real camera plate (DWAA, AP0, overscan) | 33 checks |
| Malformed files | Truncated files, absurd and overflowing data/display windows, wrong magic, tile memory bombs — all must be rejected cleanly | 15 files |
| Official OpenEXR images | The ASWF `openexr-images` collection: valid images render, deep and Y/RY/BY are refused, display-window edge cases | 77 files |
| Damaged / fuzz corpus | The OpenEXR project's ASAN/ClusterFuzz crash files: must never crash or hang | 185 files, 0 crashes, 0 hangs |
| Thumbnail = preview (D7) | Both code paths produce identical pixels | byte comparison |
| Input colourspace override | The override changes the rendered pixels, on tagged and untagged files | 8 byte comparisons |
| Data passes / Raw view | Position, depth and motion render untransformed; greyscale is grey | 14 byte comparisons |
| PQ HDR masters | The P3-D65 / Rec.2100 PQ inputs reproduce OCIO's full chain (PQ → inverse ACES 2.0 HDR → SDR) within 2/255; exposure acts after the inverse; Raw is untouched. Also checked by eye on Netflix's Cosmos Laundromat and Nocturne | 12 byte comparisons |
| Colour tags | `colorInteropID` and `arnold/color_space` are honoured in the right order (aliases, Arnold's `linear`), and display or unknown names are ignored | 22 byte comparisons |
| Golden images | Every fixture and the real camera plate, rendered at 256 px in the default view and in Raw, against reference PNGs in `Tests/Golden` (1/255 tolerance). Any pixel change shows up as a reviewable image diff; files that must not render stay rejected | 72 renders |
| Update check | Version comparison, refusing links outside this repository's releases, drafts and pre-releases, garbage answers; one live call to GitHub; the app window rendered in every update state | 12 checks + live + 3 renders |
| Hostile input | A file swapped between header read and decode is refused (it once overflowed a buffer); text in a file cannot forge info-panel rows or reorder text; a 2,000-layer file gets a bounded menu. Found in the [adversarial review](phase1-status.md#adversarial-review-2026-10-08), which also ran a sanitizer fuzzer over 370,000 mutated files | 10 checks |
| ACES tables | The baked LUTs re-bake to the same hashes, and the shipped runtime matches OCIO within tolerance | hash + numeric |

Every render check compares **pixels**, not settings. That rule exists because
two real bugs (an override that changed nothing, and greyscale layers rendering
blue) once passed checks that only looked at settings or text.

## On other Macs: the compatibility matrix

Every push, `.github/workflows/compat.yml` builds the app once the way
releases are built, then on **macOS 14 (Apple silicon), 15 (Apple silicon),
15 (Intel) and 26 (Apple silicon)**:
- builds and runs the whole suite above natively, reference images included
  (Intel matches Apple silicon to 0.25/255 since EXRCore stopped fusing
  multiply-adds; it differed by 1.9/255 on extreme values before);
- installs that one app build, switches the extensions on, and has Quick Look
  render thumbnails and previews of DWAA, DWAB and the camera plate
  (`Tools/ql-integration.sh`). Each file must show up in the extensions' own
  log, so a render by macOS's own decoder cannot pass for ours.

First green run 2026-10-08: macOS 14.8.9, 15.7.9 (arm64 and x86_64), 26.6.2.

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

- **Intel Macs, by hand.** CI runs the x86_64 build on macOS 15 Intel (below),
  but nobody has used it on a real Intel Mac yet.
- **Spotlight, and Open/Save dialogs.** Whether they use the extensions at all.
- **Finder's column view and preview pane** on recent macOS. They use the same
  preview code, but have not been rechecked since early development.
- **Real renders from specific applications.** Layer detection is checked
  against real files from Arnold, V-Ray, Nuke and Blender, but only against the
  *documented* default names of Redshift, Karma, Octane, Cycles multilayer,
  Unreal, RenderMan and Corona: no openly licensed sample files exist. A
  layer-picking mistake on those is the most likely real-world bug. A header
  dump (`exrcli -v`) of one of your files is the most useful report.
- **Non-ACES pipelines.** Untagged files are assumed ACEScg. How often that is
  wrong in practice (Blender's linear Rec.709, for example) is not known.
- **Very large files and other Macs.** On an M2 Pro a 4K DWAA frame takes
  77 ms for a thumbnail and 101 ms to first preview paint (budget: 300 / 500 ms),
  and a 6K DWAA plate about 240-290 / 275-336 ms depending on load
  (`build/bench`). Base M1/M2 chips, Intel Macs, and 8K/16K frames are
  unmeasured.
- **Network volumes and slow disks.** Behaviour against the 2-second decode
  deadline.
- **Multiple displays and wide-gamut or HDR monitors.** Output is SDR Display
  P3, converted by ColorSync; only Apple displays have been looked at.
- **Continuous fuzzing in CI.** `Tools/fuzz.sh` runs locally; not in CI.

## Known not to work (by design)

Deep images, luminance-chroma (`Y`/`RY`/`BY`) files and cryptomatte-only files
keep the generic icon. HDR output is not implemented.

# Phase 1 status

Written unattended overnight, 2026-09-05 into 2026-09-06. Read before continuing.

## Assumptions made without asking

You were asleep; these are stated rather than agreed, and both are reversible.

1. **Script-based build, no `.xcodeproj`** — deviates from plan §5, which assumes
   one. Xcode was not installed and the CLT path is verified end to end
   (`docs/uti-findings.md` §5.6). You have since said you would install Xcode
   "just in case"; nothing here depends on it, and CI would use these scripts
   regardless.
2. **The ACES 2.0 output transform is deliberately absent** (§6.5 step 3). It
   needs the Phase 2 LUT, and CLAUDE.md forbids silently substituting an
   approximation. `EXRCLI --png` therefore applies a plainly-labelled sRGB
   transfer for eyeballing decode correctness, and says so on every run. That
   is a debug path, not a stand-in for the output transform.

## Working, tested

| Component | State |
|---|---|
| `Tools/build-openexr.sh` | Imath v3.2.2 + OpenEXR v3.4.16, pinned, universal static (was 3.4.5 until 2026-10-08) |
| `EXRCore/exr_limits.h` | Bounds + checked arithmetic (§6.6) |
| `EXRCore/exr_layers.*` | Full primary layer selection (§6.3) |
| `EXRCore/exr_reader.*` | Header inspection (§6.2) |
| `EXRCore/exr_decode.*` | Banded channel-selective decode, crop to display window, deadline (§6.4) |
| `EXRCore/exr_color.*` | Primaries → AP1 with Bradford adaptation (§6.5 steps 1–2) |
| `EXRCLI` | Inspect + `--png` debug render |
| `Tools/make-fixtures` | Writes the §9 corpus with the real OpenEXR writer |
| Tests | 91 unit assertions + 15 corpus checks + 13 malformed files. `Tools/test-all.sh` runs the lot. |

### Validated against real files, not just fixtures

- `LCF_05-01_01_X1_0039_AMaZE.exr` (32 MB, `~/Desktop/RAWtoEXRforMac/…`):
  **DWAA level 15** parsed, **ACES2065-1 (AP0)** identified from chromaticities,
  genuine overscan (dataWindow 6022×4024 vs displayWindow 6000×4000) correctly
  cropped, decoded and rendered in **1.6 s** including PNG encode of a 6000×4000
  image. This is the D6 case with a real file behind it.
- The Rec.709→AP1 matrix is checked against published ACES reference values to
  within 0.002, not against our own output.

### Corpus results (real OpenEXR-written files, not our Python writer)

All 15 expectations in `Tools/test-corpus.sh` pass. The ones that matter:

- DWAA and DWAB multi-layer files with `diffuse`, `CryptoObject00`, `mask` and
  `Z` present: the unprefixed beauty layer wins. Verified visually too — the
  render is the full-scale ramp (beauty, 1.0), not diffuse (0.5) or crypto (0.9).
- With no unprefixed layer, selection falls through to `diffuse`, never crypto.
- A layer named `beauty` beats an alphabetically-earlier layer.
- Multi-part: the part named `beauty` wins over an earlier AOV part.
- Deep scanline: reported, never selected, never crashes.
- Y/RY/BY is detected and then **refused at decode**, loudly.

### Performance

6000×4000 overscan DWAA (32 MB file), Apple M2 Pro. The plan's §6.7 budget is
"thumbnail, 4K DWAA, < 300 ms" — this is a 6K file:

| Request | Wall clock | Peak RSS |
|---|---|---|
| Full resolution | 2.12 s | 609 MB |
| `--max-edge 512` (preview) | **0.29 s** | **37 MB** |
| `--max-edge 128` (Finder icon) | 0.27 s | 32 MB |

Downsampling is box-filtered and integrated into the band walk, so the
full-resolution float image is never materialised. Full-resolution output is
byte-identical to the pre-downsampling implementation, so the fast path is a
pure addition rather than a change of behaviour.

Two things account for the speed: bands skip overscan rows the display window
never uses, and the destination is small enough to stay in cache.

### Build gotchas worth remembering

- Building OpenEXR's default `all` target fails at `bin/website_src`, which
  links OpenJPH. The script builds the five library targets explicitly.
- OpenEXR 3.4 links `OpenEXRCore` against **OpenJPH** for HTJ2K. With
  `OPENEXR_FORCE_INTERNAL_OPENJPH=ON` the archive is fetched but *not*
  installed, so the script copies `libopenjph.a` into the install tree. Without
  it every downstream link fails on `ojph::` symbols.

## Known gaps

- **Luminance-chroma (Y/RY/BY) decode is deliberately not implemented.**
  Agreed 2026-09-06 as too obscure to be worth the chroma-upsampling code.
  It is detected in the header and in layer selection, and `decode_layer` fails
  loudly with "not decoded yet" rather than rendering something wrong. A file
  that uses it falls back to the generic icon, which is the correct behaviour
  under §6.6. Revisit only if a real file turns up.
- **No mip or `preview`-attribute fast path** (§6.4). Both are detected but
  unused. Decode-to-target-size already gets thumbnails well inside budget, so
  this is now an optimisation rather than a requirement.

### Fixture corpus (`build/make-fixtures`)

19 files, written with the real OpenEXR writer:

DWAA/DWAB multi-layer with cryptomatte + mask + Z, AOV-only (no beauty),
named-beauty, ACES2065-1, no-chromaticities, float32, luminance-chroma,
overscan, multi-part, deep scanline, **NaN/Inf**, **out-of-gamut (to 12 stops,
including negatives)**, **45-channel AOV stack**, **nested layer names**,
**single-channel**, **preview attribute**, **tiled+mipmapped**.

Verified: NaN and Inf never survive decode; negatives *are* preserved, since
clamping them would break gamut mapping downstream.

- **No golden-image regression** (§9) yet.

## Suggested next step

Write `Tools/make-fixtures` against the now-available OpenEXR libraries to
produce the §9 corpus, then point `EXRCLI` at it. That closes the biggest
confidence gap: layer selection has good unit coverage but has never seen a
real multi-part or cryptomatte file.

## Commands

```bash
Tools/build-openexr.sh          # once; ~2 min
Tools/build-cli.sh              # build build/exrcli
Tools/run-tests.sh              # unit tests
Tools/make-malformed-fixtures.py && Tools/test-malformed.sh
build/exrcli -v FILE.exr        # inspect
build/exrcli --png OUT.png FILE.exr [--raw] [--exposure STOPS]
```

---

## Phase 2 — done 2026-09-06

- `Tools/bake-lut/bake.py` bakes three views from OCIO 2.5.1's built-in
  `cg-config-v4.0.0_aces-v2.0_ocio-v2.5`, emits the canonical `.lut` binaries,
  a generated `EXRCore/src/exr_lut_data.cpp`, and `docs/lut-provenance.md`.
- `EXRCore/exr_lut.*` is the runtime: ACEScct shaper, tetrahedral interpolation,
  own fp16 decoder so it links without OpenEXR. Tables are **compiled in** — an
  appex that cannot find a resource file has no fallback, a compiled-in table
  cannot go missing.
- `Tools/verify-lut.sh` re-bakes, checks hashes against the provenance doc, and
  compares the compiled runtime against OCIO. Measured: mean **0.146/255**,
  p99.9 8.95 for the P3 default. Wired into `Tools/test-all.sh`, skipped
  gracefully where PyOpenColorIO is absent.
- `EXRCLI` now renders through the real transform. `--view` selects, `--raw`
  bypasses, `--list-views` enumerates.

Bundle cost: 1.57 MB for the 65³ default plus 0.21 MB per 33³ alternate.

---

## Phase 3 — thumbnail, plain preview, proof-of-concept overlay (2026-09-06)

### Built

- **`EXRCore/exr_api.h` / `exr_api.cpp`** — C bridge so the Swift extensions
  never see C++. The whole pipeline sits behind one `exr_render` call, which is
  how D7 is *enforced* rather than merely intended: both extensions pass the
  same arguments to the same function.
- **`Shared/EXRRenderer.swift`** — compiled into both appexes. Turns the C
  result into a 16-bit **Display P3** `CGImage` (D5). The LUT already emits
  display-encoded values, so nothing further is applied.
- **`EXRThumbnail`** — renders at the requested pixel size, aspect-fit into the
  reply context. Always uses committed defaults; preview session overrides
  deliberately do not reach it (§8).
- **`EXRQuickLook`** — plain image preview, capped at 2048 px. Completes the
  handler *before* building the overlay, per §8's "do not block first paint".
- **`EXRQuickLook/OverlayPanel.swift`** — proof of concept only. Collapsed HUD
  button expands to two controls: **exposure** and **view transform**, plus
  Reset. Badges orange whenever an override is active (§8, non-negotiable).
- **`Tools/build-core.sh`** — universal `libEXRCore.a`.

### Verified

- Real ACES 2.0 thumbnails served through the actual Quick Look API for both a
  synthetic fixture and the 32 MB `rawtoaces` frame.
- **D7 is tested, not assumed** (`EXRCore/tests/test_d7.cpp`, in `test-all.sh`).
  The real risk was that the thumbnail spells the default view as `NULL` while
  the preview passes the explicit default id; if those ever resolved
  differently, D7 would break silently. They are byte-identical. The test also
  covers determinism, and that an override does change the output — otherwise
  the badge would be lying.
- Deep files fail cleanly in both extensions, so Quick Look falls back to the
  generic icon.

### Verified visually (2026-09-06, once Screen Recording was granted)

`Tools/qlpreviewprobe --hold N` parks a real `QLPreviewView` window at a known
position so it can be screen-captured. This is deliberately used instead of
driving Finder with synthetic keystrokes: `keystroke " "` goes to whatever has
focus, so a lost activation race types into an unrelated app. The probe hosts
the same `QLPreviewView` class the spacebar panel and preview pane use, so it
exercises identical extension code with no input injected into the session.

Confirmed by capture: image renders and letterboxes correctly, the HUD button
appears, and the panel expands with all three controls.

### Fixed after looking at it

- **Preview showed a magnified crop.** `NSImageView` takes its intrinsic content
  size from the image; at 2048 px Auto Layout refused to shrink the view below
  that, so a ~250 pt preview pane clipped it. Lowered compression resistance and
  hugging, and set `preferredContentSize` from the image aspect so the spacebar
  panel is shaped like the frame.
- **HUD was illegible over saturated content.** `NSVisualEffectView` takes its
  tone from what is behind it, so over a magenta frame the panel went bright
  pink. Added a 55 % black scrim under the controls so contrast is constant
  regardless of the image.
- **Two view-transform entries had identical labels.** The Rec.709 view is baked
  twice, for an sRGB display and for Rec.1886, so both read
  "ACES 2.0 - SDR 100 nits (Rec.709)" in the picker. Labels are now derived from
  the target display — "ACES 2.0 — Display P3", "— sRGB", "— Rec.1886 Rec.709" —
  which is unique and is what the user is actually choosing between.

### Fixed along the way

`Tools/build-spike.sh` did `rm -rf build/`, which destroyed `build/lib` from
`Tools/build-core.sh` and the diagnostic helpers. It now cleans only its own
outputs.

### Next

The remaining §8 controls (channel isolation, alpha over checkerboard, data vs
display window, input space override), the layer/part switcher, the metadata
panel, and session-sticky settings backed by an App Group.

---

## Phase 3, second pass — full overlay + metadata panel (2026-09-06)

### The slider was slow because every event re-decoded the file

`exr_render` did decode + transform in one call, so dragging exposure
re-decoded a 6K DWAA plate per event. Two fixes:

1. **Split decode from transform.** `exr_open` decodes once into scene-linear
   pixels held in an opaque `EXRSource`; `exr_source_render` re-applies only the
   matrix, exposure, channel view and LUT. The Swift preview holds a `Source`
   for the file's lifetime.
2. **Parallelised the transform.** It is per-pixel independent. Capped at
   `kDecodeThreads + 1` because the appex is sandboxed and memory-capped (§6.4).

Measured on the 6000×4000 DWAA plate:

| | per change |
|---|---|
| Before, one-shot `exr_render` | 471 ms |
| After, cached + serial | 192 ms |
| After, cached + parallel | **50 ms** |

`exr_open` costs 275 ms once, paid at first paint.

**Guarded against the obvious risk:** parallelising a pixel loop is where
determinism quietly breaks, so `test_d7` now also asserts the cached path is
byte-identical to the one-shot path. Both are in `Tools/test-all.sh`.

### The badge never turned orange

`contentTintColor` is not reliably applied to a bordered `NSButton`'s symbol
image, so the badge was invisible. Replaced with an explicit orange dot view
pinned to the button corner, with the tint kept as a secondary cue.

### Overlay now complete against §8

| Control | Notes |
|---|---|
| Exposure | ±6 stops, continuous, manual only (D8) |
| Channel isolation | RGB / R / G / B / A / luminance, applied in scene-linear before the transform so an isolated channel is tone-mapped as it would be in situ |
| View transform | The three baked LUTs |
| Input space override | **Disabled, with the reason shown, when the file states its own chromaticities** — D9 says a file that told the truth is never overridden, so the control says so rather than silently doing nothing |
| Alpha over checkerboard | Preview only; thumbnails never composite (§6.3) |
| Data window vs display window | The one setting that changes the decode, so it reopens the source |
| Reset to defaults | |

### Metadata panel

Second HUD button (`info.circle`) opens its own panel: dimensions, compression
with DWA level, named chromaticities, chosen layer and its exact channel names,
pixel type, channel and part counts, display window, data window when it differs
(flagged as overscan), pixel aspect when non-square, preview attribute, deep
flag, and every layer present so the panel also shows what was *not* chosen.

### UI automation: a real incident worth recording

While screenshotting the HUD, the screen locked between reading the probe
window's bounds and issuing a synthetic click. **The click landed on the macOS
login window.** Nothing was typed and nothing happened, but the lesson stands:
computing coordinates from live window bounds proves the window *exists*, not
that it is *frontmost and unobscured*. Any future click automation must assert
the target is frontmost and bail if `loginwindow` or a screensaver is on screen.
Prefer `Tools/qlpreviewprobe --hold` plus a capture, and avoid synthetic input
entirely where possible.

---

## Official ASWF reference corpus (2026-09-06)

`AcademySoftwareFoundation/openexr-images`, the corpus plan §9 names. **BSD-3-Clause**
(ILM's standard text; GitHub reports `NOASSERTION` only because LICENSE is not a
verbatim SPDX match), so it is compatible with D10.

`Tools/fetch-openexr-images.sh` does a shallow, blob-filtered, sparse checkout of
the eight relevant directories into `Vendor/openexr-images`, which is gitignored.
The full repo is ~234 MB packed, so nothing large enters this repository; when
the project becomes a git repo this should become a submodule as §9 intends.
`Tools/test-reference-corpus.sh` runs it, and is part of `Tools/test-all.sh`.

### Results — 8 groups, 0 failures

| Group | Result |
|---|---|
| `TestImages` | 11 rendered — NaN, infinity, out-of-gamut |
| `MultiResolution` | 14 rendered — tiled and mipmapped |
| `Beachball` | 16 rendered — the singlepart/multipart pair |
| `DisplayWindow` | 16 rendered, including the non-overlapping cases |
| `v2` deep parts | 12 refused — deep is a non-goal |
| `v2` composited | 2 flat composites rendered |
| `LuminanceChroma` + `Chromaticities/*_YC` | 6 refused loudly |
| `Damaged` | **182 refused, 3 rendered, 0 crashes, 0 hangs** |

`Damaged` is the ASAN and ClusterFuzz corpus — 185 files that historically
crashed OpenEXR itself, including heap overflows, OOM and null derefs. Zero
crashes and zero hangs is the §6.6 result we wanted, and it is now measured
rather than asserted.

### Two things the corpus corrected

Both were my assumptions, contradicted by the corpus's own documentation.

1. **I wrongly rejected non-overlapping display windows.** Two `Damaged` files
   have a display window disjoint from the data window; we clamp safely but emit
   an all-black frame, and I "fixed" that by rejecting. `DisplayWindow/README.rst`
   is explicit that this is legal and *must* render:
   "The display window and the data window do not overlap. The entire display
   window should be filled with the background color." Reverted, and the reason
   is now a comment in `exr_decode.cpp` so it does not get re-broken.
2. **I wrongly expected all of `v2` to be refused.** The deep parts are correctly
   refused, but each view also carries a flat `composited.exr` that must render.
   The test now distinguishes the two rather than asserting a blanket rule.

### A harness bug worth remembering

The first run reported "185 rendered, 0 refused" from the fuzz corpus, which
should have been implausible on its face. **macOS has no `timeout`**, so every
invocation returned 127 with a shell error and none of the output ever matched.
Deadlines in these scripts use `perl -e 'alarm N; exec @ARGV'` instead.
`Tools/test-malformed.sh` was never affected — it measures elapsed time with
`date` rather than shelling out to `timeout`.

---

## Overlay responsiveness (2026-09-06)

Reported: everything except the slider took about a second to respond, and gave
no acknowledgement of the click until the panel finally appeared.

Three separate causes, none of them the transform:

1. **Re-render ran synchronously on the main thread.** A popup could not open
   until the 50 ms transform finished, and clicks queued behind it. Rendering now
   happens on a `userInitiated` queue with rapid changes coalescing to the most
   recent settings, so the UI never blocks.
2. **`preferredContentSize` was assigned on every render.** That asks the Quick
   Look host to resize the panel, which is cross-process, and the aspect ratio
   almost never changes. Now assigned only when the size actually differs.
3. **Toggling a panel used `isHidden`**, which changes the intrinsic content
   size and forces a layout pass on a *remote* view — the extension's view is
   hosted out of process, so that is IPC on every click. Both panels now stay
   laid out permanently and are revealed by `alphaValue`, which costs a
   compositing change and nothing else.

Plus the acknowledgement the report actually asked for: the two HUD buttons are
`pushOnPushOff`, so they light up (in the system accent colour) the instant they
are clicked and stay lit while their panel is open.

### Consequences that had to be handled

- **Panels are mutually exclusive.** With both permanently laid out, stacking
  them vertically left the second floating below an invisible first. They now
  share one position and opening either closes the other.
- **`hitTest` is overridden** so a transparent panel does not swallow clicks
  meant for the image.
- **The panel needs an explicit height.** Removing the old bottom constraint
  collapsed it, and because AppKit stops hit-testing at a view's bounds while
  drawing does not, the buttons stayed visible but stopped responding entirely.
  The panel is now constrained to be at least as tall as the taller body, with
  low-priority hugging so it claims no more space over the image than it uses.
- **A data race was introduced and closed.** `source` is reassigned on the render
  queue, so reading `source?.hasChromaticities` from the main thread to build the
  overlay was unsafe. The flag is captured on the main thread at first decode.

## Next session — short plan

Ordered. Stop after 1 if it fixes the latency.

1. **Button latency (open).** The state change and the panel reveal arrive
   *together*, 1–2 s after the click. Our code sets `button.state` immediately,
   so what is delayed is the repaint, not the logic — the signature of the whole
   remote view being composited in one IPC round trip.
   - **Prime suspect: `NSVisualEffectView`.** Vibrancy in an out-of-process view
     makes the host composite a blur behind the panel, which is expensive and
     forces a full surface update. Swap both bodies for a plain layer-backed
     view with a dark translucent background and re-measure. Cheap to try, and
     the scrim already provides the contrast, so little is lost visually.
   - Second suspect: the 2048 px `NSImage` sharing the layer tree, so every
     surface update re-composites it. Test by rendering at the view's actual
     size rather than a fixed 2048 cap.
   - If neither: bracket click → repaint with `os_signpost` and measure rather
     than guess further.
2. **Host app preferences window.** D9 now has two committed preferences
   (assumed input colourspace, default view transform) and nowhere to set them.
   Needs the shared App Group so the extensions can read them.
3. **Session-sticky overlay settings** (§8): in-memory plus the App Group mirror,
   with the documented carry-over rules — view settings carry unconditionally,
   input-space override only onto files that are equally ambiguous, layer
   selection sticky by name.
4. **Layer/part switcher** in the preview, never the default view.

5. **Preview.app — investigated 2026-09-06, and the direct route does not exist.**

   Preview.app links both ImageIO and QuickLookUI, so it was worth testing rather
   than assuming. Opened the real plate in Preview with a log stream on our
   subsystem: **zero invocations.** Preview decodes images through ImageIO, which
   dispatches to `libAppleEXR.dylib`, and never consults a Quick Look extension.

   There is also no supported way to add a decoder to ImageIO: no third-party
   codec extension point exists (`pluginkit` lists only Photos-related image
   extension points, which are not codecs). So **we cannot make Preview.app
   itself render EXRs correctly.** That is an Apple-side limitation, not
   something our architecture can route around.

   What is achievable, in increasing scope:

   - **Document it.** "Why does Preview still look wrong?" will be a README FAQ.
     Finder thumbnails and Quick Look are ours; Preview is not.
   - **A minimal viewer app** that takes the double-click role via
     `CFBundleDocumentTypes`, reusing EXRCore and the preview's overlay. Plan §4's
     decision gate already contemplated exactly this as a fallback. It is small
     because all the logic exists — but it *is* a second product surface, and the
     non-goals are strict, so it needs sign-off before starting.
   - Claiming the default handler for `.exr` outright would replace Preview for
     EXR rather than fix it. Doing that without the user opting in would be
     rude; offer it, do not assume it.

Not blocking anything: Spotlight and Open-dialog behaviour, macOS 14/15
verification, and the Homebrew `--no-quarantine` question.

---

## Committed preferences do not reach the extensions (2026-09-06)

The preferences pane writes D9's two settings correctly. The extensions never
see them, and this is a platform limitation rather than a bug in our code.

### Measured

| | Host app | Thumbnail extension |
|---|---|---|
| App Group entitlement present | yes | yes |
| `containerURL(...)` returns a path | yes | yes |
| Writes reach the container | **yes** | no |
| Sandbox denials on the container | **0** | **9** |

The denials are attributed to **System Policy**, not to the extension's own
`quicklook-thumbnail` profile, and cover `file-read-data` *and*
`file-write-create` on every path tried: `Library/Preferences/`,
`Library/Application Support/` and the container root. The extension cannot even
read a file it wrote itself, because it cannot write one.

`UserDefaults(suiteName:)` and direct file access fail identically, so this is
not a matter of picking a different storage route.

### Two separate gates, and a correction

macOS also gates group containers behind a TCC prompt — *"EXRPreview.app would
like to access data from other apps"*. That prompt was pending, unanswered, for
part of this investigation, which produced two wrong intermediate conclusions:

- that `UserDefaults(suiteName:)` *hangs* inside the appex — it was blocking on
  the prompt;
- and then, when the prompt was granted, that the Team ID was irrelevant.

Granting it cleared the **app's** access. The extension was still denied. So both
gates are real and independent: TCC consent, and an entitlement the system will
only honour when it is backed by a Team ID.

### Decision

**Left as-is, deliberately.** Agreed 2026-09-06 that preferences reaching the
extensions is a nice-to-have rather than a requirement. The fallback is correct
and silent — extensions render the factory defaults — and D7 still holds,
because both extensions fall back identically.

The preferences pane detects the missing Team ID at runtime
(`EXRPreferences.canReachExtensions`) and says so, so a changed setting never
looks effective when it is not. That notice disappears by itself once the app is
signed, which is Phase 5.

---

## Transform and colourspace lists (2026-09-06)

### Source moved to the ACES 2.0 **Studio** config

`Tools/bake-lut/bake.py` now bakes from
`studio-config-v4.0.0_aces-v2.0_ocio-v2.5` rather than the CG config. Six view
transforms, all SDR:

| id | label |
|---|---|
| `aces2_p3d65_sdr100` | ACES 2.0 — Display P3 *(default, 65³)* |
| `aces2_srgb_sdr100` | ACES 2.0 — sRGB |
| `aces2_g22_709_sdr100` | ACES 2.0 — Gamma 2.2 Rec.709 |
| `aces2_rec709_sdr100` | ACES 2.0 — Rec.1886 Rec.709 |
| `aces2_p3d65_p3` | ACES 2.0 — P3-D65 (P3 D65 limited) |
| `aces2_p3d65_709` | ACES 2.0 — P3-D65 (Rec.709 limited) |

**Deliberately excluded**, and this is not an oversight:

- The 15 HDR views (PQ and HLG, 500–4000 nits). We emit SDR and tag the result
  Display P3, so a PQ-encoded render tagged SDR would look badly wrong. Adding
  them needs EDR output support — a scope change, not a bake change.
- SDR views on an HDR-*encoded* display (`Display P3 HDR`, `Rec.2100-PQ`,
  `ST2084-P3-D65`). The view is SDR but the display encoding is not, so the
  output would still be PQ.

Labels are derived from the target display, and where one display carries two
limiting gamuts the gamut is appended — otherwise the two `P3-D65` entries read
identically in the picker, which is the same ambiguity bug as before.

### Assumed-input list now matches what a Nuke or Resolve user expects

18 scene-linear spaces: ACES, the common display gamuts, then camera vendor
gamuts (ARRI WG3/WG4, RED, Sony S-Gamut3 and Venice variants, Panasonic V-Gamut,
Canon Cinema Gamut, DJI D-Gamut, Blackmagic, DaVinci).

**The chromaticities are derived, not transcribed.**
`Tools/bake-lut/gen_spaces.py` pushes each space's RGB basis and white through
OCIO into ACES2065-1, then into CIE XYZ using the AP0 primaries, which are
definitional. Typing published primaries by hand risks silently wrong colour,
which is precisely what this project exists to prevent.

Validated: for all 18 spaces, the matrix our code builds from the derived
chromaticities reproduces OCIO's own space→ACEScg matrix to **≤1.4e-06**.

Note the derived values are white-adapted (Rec.709 reads 0.64249/0.33036 rather
than the published 0.64/0.33) because OCIO's transform into AP0 includes a
Bradford adaptation. That is self-consistent — our matrix builder then adapts
D60→D60, a no-op — and the validation above is what confirms it.

Log encodings (ACEScct, S-Log3, LogC…) are **not** offered even though grading
applications list them: the override supplies chromaticities to a matrix, and
EXR pixel data is scene-linear, so a log space would be wrong here.

### Reference corpus

68 of 80 `openexr-images` files render. All 12 refusals are deep files
(`v2/Stereo`, `v2/LeftView`, `v2/LowResLeftView`), which the plan's non-goals
say to detect and refuse. **Zero unexplained failures.**

### The App Group is no longer touched by unsigned builds (2026-09-06)

macOS prompts *"EXRPreview.app would like to access data from other apps"* the
moment anything opens the group container. Unsigned, that access can never
succeed for the extensions, so the prompt was asking the user to grant a
capability the build cannot use — and it reappeared on every launch.

`EXRPreferences.store` now checks `canReachExtensions` once per process and only
opens the App Group when a Team ID is present; otherwise it uses the process's
own defaults. Verified: zero group-container accesses from the host app and both
extensions across a launch, a thumbnail render and a preview render.

The entitlement and the shared-store code stay in place. A fork signed with its
own Developer ID gets working shared preferences with no code change — which is
the point of keeping the plumbing.

---

## Overlay: session carry-over and the input override (2026-09-06)

### The extension process persists across files

Plan §8 asks to verify this empirically because it decides whether the App Group
mirror is the hot path or the cold one. Measured: previewing three files in
succession was handled by a **single `EXRQuickLook` pid**.

So in-memory session state survives arrowing through a folder, and the App Group
mirror is not needed for this feature at all — which matters, because the App
Group is unusable under ad-hoc signing anyway. `EXRQuickLook/EXRSession.swift`
holds the carried settings process-globally, with the §8 idle timeout.

### The input override now carries unconditionally

A deliberate deviation from plan §8 and the §10 checklist, agreed 2026-09-06.
Rationale and supersession recorded in CLAUDE.md D9.

The short version: the old rule assumed a file that states its chromaticities is
telling the truth. In a sequence with a misapplied profile every frame states the
same wrong thing, so standing down destroys the correction the moment the user
arrows to the next frame.

Kept intact: the override is badged whenever active, resets in one click, and
lapses on the idle timeout. Nothing became silent.

### Consequences

- The input picker is always enabled, and defaults to **the colourspace the file
  states** rather than the assumed default. Previously the disabled picker
  showed the assumed default while the metadata panel showed the file's real
  space — two controls naming different things.
- `exr_source_chromaticities_name()` was added so the picker can name what the
  file actually states.
- Reset returns to the file's stated space, not the global default.
- Channel isolation reduced to **RGB / Alpha**. The per-channel and luminance
  views remain in the C API for `EXRCLI`; six segments was more HUD than the
  control deserves.

---

## Overlay button latency: measured, and it is not ours (2026-09-06)

Chased across three hypotheses. The first two were wrong, and the way they were
measured was wrong too.

**Instrumented inside the extension**, free of any synthetic-click overhead:

| | |
|---|---|
| Action fired → frame composited | **0.1 – 3.1 ms** |

The app-side work is instantaneous. Everything the user perceives happens
*before* the action fires, in Quick Look's remote-view event delivery (and
likely extension-process wake-up), which is not something the extension can
influence.

### A correction to the earlier numbers

The "354→456 ms" and "215→319 ms" figures reported for the vibrancy and
layer-backed changes were largely **test-harness overhead**: an `osascript`
System Events click takes ~500 ms to dispatch, and the extension's action was
observed firing at ~375 ms — *before* osascript had even returned. Those
measurements could not have isolated the app's contribution, so the apparent
improvement was not evidence of one.

Both changes were kept anyway, on their own merits: no cross-process blur to
composite, and no CPU resample of a 2048 px image on every redraw. Neither is a
latency fix, and neither should be described as one.

`NSApp.currentEvent` is nil inside a remote view, so event-delivery latency
cannot be measured from our side at all. Three angles agree the remaining delay
is outside our code; there is nothing further to optimise here.

## Layer / part switcher: not built

Confirmed absent — there is no layer selection in the C API or the overlay. Plan
§8 specifies it ("a layer/part switcher, never the default view") and it remains
on the list. `EXRCore` already does the hard part: it selects the primary layer
deterministically and can enumerate every part and channel. What is missing is
an API to *override* that selection, and the picker to drive it.

Also still open from §8: sticky layer selection by name across files.

---

## Layer / part switcher (2026-09-06)

Plan §8's switcher, now built.

- `enumerate_layers()` in `EXRCore` lists every renderable layer across all
  parts. Deep parts are omitted — they cannot be rendered, so offering them
  would only produce a dead entry.
- Cryptomatte, depth, normals, masks and bare channels are **listed and
  flagged**, never chosen automatically, exactly as §6.3 requires.
- A stray channel is offered on its own. `Z` alongside an unprefixed RGB is the
  common case: grouping by prefix put it inside the default layer where it was
  unreachable, even though §6.3 says depth must stay listed. A test caught this.
- `exr_open` takes a layer id; an id absent from the file falls back to the
  automatic choice rather than failing, which is what makes the selection safe
  to carry across a sequence whose frames do not all match.
- Changing layer re-decodes, so it is handled like the data-window toggle rather
  than as a cheap re-transform.

Verified: the 45-channel AOV fixture enumerates 15 layers with `CryptoObject00`,
`CryptoMaterial00` and `normal` flagged; `multipart.exr` shows part-prefixed
names and correctly reports `beauty` as active.

**Sticky layer selection (§8) comes free.** The session already carries the whole
settings struct, which now includes the layer, and the fallback above makes an
absent layer harmless. That closes the last of §8's carry-over rules.

### Known deviation

`position` is not flagged as a data pass. §6.3's never-auto list names
cryptomatte, `Z`/depth/`ZBack`, normals, motion/velocity, `id` and mask/matte —
not `position`/`P`, though it is unambiguously a data pass. Left matching the
spec rather than silently widening it.

## App icon

`EXRPreview/AppIcon.icns`, built from `docs/images/EXRlogo_AImade.JPG` by
`sips` + `iconutil` (no asset catalog, so no Xcode needed) and referenced with
`CFBundleIconFile`.

**Trademark caveat, unresolved.** The artwork is a re-render of the ASWF OpenEXR
stacked-layers mark. Regenerating a logo does not change the trademark position:
protection covers confusingly similar marks, not only exact copies, and Linux
Foundation policy specifically forbids incorporating project marks into app
icons without written permission from trademarks@linuxfoundation.org.
Attribution does not cure it, because the issue is implied endorsement.
Raised and noted; the icon is a single-file swap if it needs to change.

---

## 2026-10-06 session

### macOS 27 re-verification

The machine moved to **macOS 27.0.1** (and Swift 6.4 / SDK 27.0) since the last
session. Re-checked rather than assumed: both extensions still registered and
enabled, `.exr` still binds to `com.ilm.openexr-image`, and the thumbnail was
rendered by our own `EXRThumbnail` process (observed via pgrep), not Apple's
decoder. Universal build clean on SDK 27. Phase 0's answer still holds.

### Real bug: the input-colourspace override did nothing on tagged files

The C transform applied `input_colorspace` only `if (!has_chroma)`. When D9 was
amended so the override carries onto files that state their own primaries, the
Swift side was updated — picker enabled, carried across files, badged — but the
C side was not, and nothing checked the pixels. On any tagged file the override
moved the picker and changed nothing.

`EXRCore/tests/test_input_override.cpp` now compares **rendered bytes** for every
precedence case; it failed 2/8 against the old code and passes 8/8 after the fix.
Precedence: explicit override → file's own chromaticities → `assumed_colorspace`
(new field, D9a) → ACEScg.

Fixing it exposed knock-on bugs, all fixed:

- `resolved()` filled the override field with the assumed default whenever it
  was nil. Under the corrected precedence that would have forced ACEScg onto every
  tagged file. It no longer touches that field.
- **False badges.** The overlay always wrote the popup's layer into
  `settings.layer` (including the automatic one) and compared the colourspace
  against ACEScg rather than the file's own space. Both now normalise "the choice
  that would apply anyway" back to `nil`, so only genuine overrides badge.
  `exr_source_auto_layer()` was added for this.
- **Carried overrides were not badged** until the first control change —
  `seed()` set the widgets but never adopted the settings or updated the badge.
  An invisible override, which §8 calls non-negotiable.
- **Reset did not clear the badge**, and reset the colourspace to the file's id
  rather than to "no override".

The colourspace picker's first entry is now the absence of an override, named by
what actually applies: "From file — ACES2065-1 (AP0)", "From file — custom
primaries", or "Assumed — ACEScg (AP1)".

### HUD unreadable in Light mode

All earlier screenshots were taken in Dark mode. In Light mode the overlay's
system label colours rendered dark grey on its dark panel. The panel now forces
`.darkAqua`, as Apple's own HUD panels do.

### Overlay button latency: found, and it is the platform

**A correction first.** Every latency figure from the earlier sessions was taken
with `osascript … click at`, which returns `button 1 of window …` — it resolves
the accessibility element and *presses* it. It never sends a mouse event, so it
measured the accessibility path, not what a user's click does. The conclusion
("not our code") happened to be right; the evidence was not.

Measured properly, with `Tools/realclick` posting genuine CGEvents and the
extension stamping receipt on the same `systemUptime` clock:

| | |
|---|---|
| post → extension `mouseDown`, short click | **1,165–1,169 ms**, every time |
| post → `mouseDown`, 2 s press | delivered **6 ms after release** |
| pointer arrival → `mouseEntered` (hover) | **6–8 ms** |
| extension main-thread heartbeat gaps | ≤ 51 ms on a 50 ms timer, throughout |
| action → frame composited (earlier) | 0.1–3 ms |

Quick Look's host withholds a click until `max(mouseUp, mouseDown + T)`, where T
is the system **double-click interval** — on this machine 1.1 s
(`com.apple.mouse.doubleClickThreshold`), versus the 0.5 s default. It is holding
the click in case a second one follows. The extension is idle and ready the whole
time; nothing it does can shorten the wait.

That also explains why the slider never felt slow: a drag cannot become a
double-click, so the host forwards it as soon as the pointer moves.

**Possible mitigation, not yet built:** hover events arrive in under 10 ms, so the
two HUD buttons could open their panels on hover instead of on click. In-panel
popups, checkboxes and Reset would still pay the double-click wait. Awaiting a
decision; it changes the interaction model.

### Host app

The window had phantom UI left over from removing the Rendering section: a double
divider with nothing between, a 560 pt minimum height sized for content that no
longer existed, and a tip reading "Changing these settings…" with no settings.
Rewritten: sized to content, app icon and version in the header, a "Turn it on"
section, and tips that teach the actual workflow (Space, the two HUD buttons,
carry-over and its 30-minute lapse, the thumbnail cache). The "Open Login Items &
Extensions" deep link was re-verified to land on the right pane on macOS 27.

### App icon v2

`docs/images/EXRlogo_v2.jpg` → `Tools/make-icon` → `EXRPreview/AppIcon.icns`.
Background removed with a flood fill **from the edges only**, so white highlights
inside the glass survive; a second, relaxed pass removes the floor reflection
but only in the lower part of the image, where it cannot leak into the top
slab's clear rim. Padded to ~82% of the canvas per the macOS icon grid.

This artwork is three plain stacked glass squares with no wave or circle motif,
so the trademark concern raised about the previous version — which re-rendered
the OpenEXR mark — no longer applies in the same way.

### Housekeeping

- OpenEXR 3.4 deprecation warnings cleared. `Header::dwaCompressionLevel()` was
  *not* a drop-in replacement: it returns the encoder default (45) when a file
  states no level, so the info panel would have reported a level the file does
  not contain. The attribute is read directly instead.
- The click guard and window helpers lived in the scratchpad and were lost when
  it was cleared. `Tools/uiguard` now lives in the repo and checks that **the
  topmost window at the exact click point** belongs to the target — a system
  permission dialog was on screen during this session, overlapping our window.

## Data passes: position, depth, motion (2026-10-06, later)

Asked for: position, depth and motion vectors viewable alongside the beauty.

- **Recognition.** `is_never_auto_layer` now also catches position and motion
  in the spellings renderers actually write — `P`, `Pref`, `Pworld`,
  `WorldPosition`, `motionvector`, `MotionVectors`, `VRayVelocity`, Nuke's
  `forward`/`backward`, Blender's `ViewLayer.Depth` and `ViewLayer.Vector`,
  `VRayZDepth`, `VRayNormals`, `PuzzleMatte`. Long words match anywhere in the
  name; short ones (`p`, `n`, `z`, `mv`, `uv`, `id`) only as a whole
  dot-component, or "p" would catch everything. A layer of x/y/z or u/v
  components is data whatever it is called. An unprefixed layer takes its
  **part** name, because multi-part renders often name the part after the pass.
  Blender's `<view layer>.Combined` is now a preferred beauty name.
- **Display.** x/y/z and u/v map to red/green/blue, as Nuke's viewer does; a
  two-component motion pass leaves blue at zero. Previously the `y` component
  was mistaken for luminance and shown alone as greyscale.
- **Raw view.** Plan §8 required "Raw / no transform"; it had never been built.
  It now exists as the last view-picker entry. A data pass renders raw **by
  default** — the view setting's nil now means *automatic*: committed default
  for imagery, raw for data. An explicit pick still wins, and picking what
  automatic would show is normalised back to nil, so it does not badge.
  Exposure still applies on raw, which is how depth beyond 1.0 is read.
- **Data-only files.** Separate-AOV renders write one pass per file. A
  depth-only or position-only EXR used to get the generic icon (and P-only
  showed only its `y` channel). It now shows its first data pass, raw, in both
  thumbnail and preview (D7 by construction — same C rule). **Cryptomatte-only
  files still get the generic icon**: the channels are hashes with no readable
  raw form. This extends §6.3, which did not address data-only files.
- Bare-channel entries were labelled `diffuse.diffuse.Z`; now `diffuse.Z`.

### Two older bugs found on the way

1. **Every greyscale layer rendered blue.** A grey selection names one channel
   for R, G and B. OpenEXR's `FrameBuffer` keeps one slice per name, so the
   second and third inserts replaced the first and only blue was filled. Hit
   every single-channel file and every bare channel (Z, …) in the picker since
   Phase 1. The corpus check only asserted `kind greyscale` in the CLI text,
   never the pixels. Now read once and reused; `test_raw_view` asserts r=g=b.
2. **Session carry-over never worked.** On 2026-09-06 the restore of
   `EXRSession.current` was inserted *above* an existing `settings = .default`,
   which immediately discarded it. The earlier "carried override shows no badge"
   fix was reasoned, not observed. Fixed, and the first decode now honours a
   carried layer and data-window toggle. **Needs a by-hand check**: it is the
   one path the headless probes cannot drive (it needs a control change).

Also: the band-buffer size in decode used an unchecked `dw_w * kBandHeight`
(bounded in practice, but the hardening rule says every one) — now
`checked_mul`. `Tools/build-cli.sh` now builds `build/make-fixtures`, which had
no build script. `exrcli -v` prints the layer switcher with data flags.

Tests: `test_layers` 131 checks (39 failed before the change);
`test_raw_view` 14 byte-level checks (4 failed before the greyscale fix);
5 new corpus checks. Real Quick Look: depth-only and single-channel thumbnails
come back grey from our extension; the preview opens data-only files with the
expected layer.

### Carry-over, second bug: Quick Look prepares neighbours ahead of time

User report after the fix above: AP1 → AP0 on one microwave frame, ↓, next
frame back on ACEScg. `Tools/qlpanelprobe` drives the real spacebar panel
(`QLPreviewPanel`) and the lifecycle log showed why:

```
prepare  ab9e microwave.-001.exr      opening frame 1 ...
prepare  d9bf microwave.-004.exr      ... prepares the previous (wrapping) ...
prepare  bdf7 microwave.-002.exr      ... and the next frame immediately
viewWillAppear bdf7 microwave.-002.exr   3.4 s later, on ↓: the prebuilt one
```

Reading the session at prepare time can never see a change made on frame 1
after frame 2 was built. Now `EXRSession` posts `didChange`; every other live
preview re-syncs 0.25 s later (debounced, so an exposure drag does not
re-render the neighbours per tick), and `viewWillAppear` re-syncs as a backstop.
`OverlayPanel.sync(to:)` re-seeds the controls, including the layer.

Real-world files from the user (`~/Downloads/microwave`, `LCF_12-08_02_X1`)
also showed Nuke's long channel names — `mattes.red/green/blue` — appearing as
three greyscale bare channels. `red/green/blue/alpha` now fold onto R/G/B/A.

## colorInteropID (2026-10-06)

Prompted by the user's Nuke comp (`LCF_12-08_02_X1`) stating no colourspace.
A full attribute dump confirmed we were reading it correctly: Nuke 17.0v1
writes only `nuke/*` bookkeeping, frame rate and timecode. Nuke adds
`chromaticities` only with "write ACES compliant EXR", which also forces
ACES2065-1. The microwave sequence states nothing either.

The same dump showed the real plate (written by OpenImageIO 3.1) carries
`colorInteropID = "lin_ap0_scene"` — a standard attribute since OpenEXR 3.4,
the version we pin. Now read (D9 amended, see CLAUDE.md):

- recognised scene-linear ID, no chromaticities → treated exactly as stated
  primaries: same pixels, "From file — …" in the picker, assumed default never
  applied, override still wins;
- chromaticities present → they win; the info panel notes a disagreeing ID;
- log/display IDs (`ocio:acescct_ap1_scene`, `srgb_rec709_display`) → ignored,
  and the info panel says so;
- `data` → every layer is a data pass, rendered Raw.

The ID table comes from OCIO 2.5.1's `getInteropID()` on the pinned studio
config; regenerating left every chromaticity value byte-identical. Also fixed:
P3 chromaticities were named "Display P3" while the picker says "P3-D65", so a
P3-tagged file's own space was not recognised in the picker.

`test_interop`: 13 byte-comparison checks (7 failed before the change).

## App renamed; Phase 0 control type removed (2026-10-06)

- `build/EXRPreview.app` → **`build/EXR Quick Look.app`**, installed to
  `~/Applications/EXR Quick Look.app`. `CFBundleDisplayName` set; the
  executable and source folder keep the internal name `EXRPreview`.
  `Tools/install-spike.sh` deregisters and removes a pre-rename install first,
  so two copies with one bundle id never compete in PluginKit;
  `Tools/uninstall-spike.sh` removes either name.
- The `.exrspike` positive control (`com.exrquicklook.spike-image`) is gone:
  no longer exported by the host app, claimed by the extensions, or generated.
  Verified absent from the LaunchServices database after reinstall.
  `docs/uti-findings.md` still describes it, as the record of Phase 0.
- Extensions re-registered from the new path and stayed enabled; real Quick
  Look thumbnails and preview confirmed from them afterwards.
- Bundle id is still the placeholder `com.exrquicklook.*` — pending the user's
  GitHub username for `io.github.<user>.exr-quicklook`.
- **Resolved the same day:** bundle id is now `io.github.bizmar.exr-quicklook`
  (extensions `.Thumbnail` and `.Preview`); log subsystem and the (unsigned,
  unused) App Group follow it. The old `com.exrquicklook.*` registrations were
  removed by the install script, which now deregisters an existing install
  before replacing it. New ids register as *undecided* in PluginKit, and Quick
  Look uses them in that state (verified: thumbnails and preview served).
  Log predicate is now `subsystem == "io.github.bizmar.exr-quicklook"`.

## App icon v3: a conforming tile (2026-10-06)

The user noticed the Dock icon looked small. Measured with
`NSWorkspace.icon(forFile:)`, which returns what IconServices actually shows:
macOS 26+ had put our free-floating art on its own grey tile and shrunk it to
~45% of the tile width. That is the documented Tahoe behaviour for icons whose
pixels do not fill the rounded-square shape. **Correction to v2 above:** "padded
to the macOS icon grid" was wrong in kind — the grid describes the *tile*, and
transparent art around it is exactly what triggers the grey box.

Now `Tools/make-icns.sh`: `make-icon-tile --photo` draws the logo photo (which
is on white) covering an 824/1024 rounded-square tile, corner radius 185.4,
then `iconutil`. Background removal is no longer used for the icon: at icon
sizes it turned the soft floor shadow into a ragged, sliced-looking edge.
Verified the same way: the system now shows the tile at full size, no grey box.

## Performance re-measured (2026-10-07)

`build/bench` (new, `Tools/bench/main.cpp`): median of 7 runs, M2 Pro, 3 decode
threads, through the shipping C API.

| File | Thumbnail (1024 px) | Preview first paint (2048 px) | Exposure drag |
|---|---|---|---|
| 4K DWAA level 45 (Netflix *Sparks* frame, re-encoded) | 77 ms | 101 ms | 36 ms |
| 4K uncompressed (*Sparks* original) | 78 ms | 104 ms | 38 ms |
| 6K DWAA level 15 (real camera plate, 6022×4024 data window) | 345 ms | 392 ms | 60 ms |

The §6.7 budget (4K DWAA: thumbnail < 300 ms, preview < 500 ms) is met with a
wide margin. The 6K plate is 15 % over the *4K* thumbnail figure, still far
inside the 2 s ceiling. Decode dominates there: the thumbnail is barely cheaper
than the preview, since every scanline must be decoded whatever the output size.
More decode threads would cut it. Plan §6.4 asks for 2–4, so that is a
decision, not a fix.

## PQ HDR masters as input entries (2026-10-07)

Netflix's *Cosmos Laundromat* and *Nocturne* EXRs hold PQ-encoded HDR with no
tag of any kind. Treated as scene-linear they render washed out and off-colour.
The input picker now offers P3-D65 PQ (1000 / 4000 nits) and Rec.2100 PQ
(1000 nits): OCIO's inverse of the matching ACES 2.0 HDR output transform,
baked to 65³ (PQ code values are a bounded domain, so no shaper), output
scene-linear ACEScg, then the normal SDR view. Measured against OCIO's full
chain on 30 000 random PQ colours: 0.07/255 mean, ~3/255 worst (33³ was
~10/255 worst). `test_pq` checks rendered greys against OCIO's values within
2/255. Checked by eye on mid-film frames (frame 0 of both films is a black
fade-in, which first made the test look broken).

Size: the three tables are +4.8 MB per architecture per binary. The host app
no longer links EXRCore at all (it never used it), which more than paid for
it in the app: 63 MB → 43 MB, DMG 14.8 → 19.7 MB. A framework shared by the
two extensions would hold the tables once (~5 MB more off the DMG) — not done.

### Exposure double-click reset — removed (2026-10-07)

Added 2026-10-06 (`ResettableSlider`), reported not working by the user: a
double-click on the slider opens the file in its default app instead. The
Quick Look host holds every click for the double-click interval (see the
latency table above) precisely to detect a double-click, and handles it itself
by opening the file; the extension's view never gets the second click. Nothing
on the extension side can claim it, so the feature is gone from the code, the
tooltip, the host app's tips and the README. "Reset to defaults" in the panel
still returns exposure to 0 along with everything else.

### Adversarial review (2026-10-08)

Asked for by the user before 0.2.0: look for bugs an attacker could use, from
angles the existing tests do not cover. The threat model is a hostile EXR that
reaches Finder (a download, mail attachment, network share or synced folder):
thumbnails are generated without the user opening anything. The extensions are
sandboxed with no network access, so the worst outcome of a memory-safety bug
is code execution inside that sandbox, with read access to the file being
previewed; the realistic outcomes are crashes and denial of service.

Every finding below was demonstrated before it was fixed; the proofs are now
regression tests.

| # | Finding | Severity | Status |
|---|---|---|---|
| 1 | **OpenEXR 3.4.5 was 29 security fixes behind.** Counting only advisories that apply to a 64-bit reader of flat images (not deep data, Python bindings, writers or the command-line tools), 29 were published Mar–Sep 2026, 13 rated high: heap out-of-bounds writes in the DWA, PIZ, B44 and HTJ2K decoders, and unbounded allocation in IDManifest parsing, which runs on every header read. 0.1.0 shipped with all of them. | High | Fixed: pinned to **3.4.16**, the newest 3.4 release. Every advisory that reaches our decode path is fixed in it. The remaining open ones affect OpenEXRUtil's checker, deep ZSTD (3.5 only) and command-line tools, none of which are linked or used. |
| 2 | **File swapped between header read and decode.** `inspect_file()` and `decode_layer()` opened the file separately; the decode sized its buffers from the first header and OpenEXR wrote wherever the slices pointed. Swapping a 16 px-wide file for a 4096 px one wrote ~11 KB of file-controlled pixel data past a 4 KB heap buffer (AddressSanitizer). The window is the milliseconds between two opens, so triggers are a hostile SMB/WebDAV server that serves different bytes on each open (deterministic), a local race, or by chance a renderer rewriting the frame at that moment. | High | Fixed: the decode refuses unless the re-read data window matches. `test_hostile`. |
| 3 | **OpenEXR's own allocations were outside the memory ceiling.** `kMaxBytes` only covered our buffers, and `kMaxTiles` was declared but never checked. A 1.8 MB file with one 65535 × 4577 tile took **7.2 GB** and 2.9 s; in the extension that is a jetsam kill, which also fails every other thumbnail the process was generating (a "poison file" for its folder). | Medium | Fixed: per-part bound on the decompressed chunk (all channels) and on the tiled row cache, 512 MiB (real files peak near 2 MB); tile count enforced; OpenEXR's `setMaxImageSize`/`setMaxTileSize` set as a second layer. Now rejected in 0.3 s using 2 MB. `tile-huge.exr`, `tile-count.exr` in the malformed corpus. |
| 4 | **Forged info-panel rows.** The panel is `label\tvalue\n` lines and file text was inserted raw, so a `colorInteropID` (or layer name) containing a newline added rows of its own, e.g. a second "Colour" line claiming Rec.709. Bidirectional-override characters could reorder text. | Low | Fixed: control characters become spaces, direction controls are removed, in the panel and the layer menu. `test_hostile`. |
| 5 | **Layer flood.** Within the part and channel limits a file can list 261,632 layers; the preview would build a menu that long on its main thread. | Low | Fixed: the menu keeps 1024 entries plus the shown and automatic layers. `test_hostile`. |
| 6 | **Undefined behaviour on `dwaCompressionLevel`.** A NaN or huge float was converted to `int`. | Low | Fixed: only finite, plausible values are shown. |
| 7 | **Build paths in the shipped binaries.** OpenJPH's `__FILE__` macros embedded 24 absolute paths with the builder's home directory. | Low (privacy) | Fixed: `-ffile-prefix-map` in all three build scripts. |

Checked and found sound: the LUT lookups clamp NaN and infinity before
indexing; degenerate chromaticities produce NaN pixels that the shaper maps
to black, never an out-of-range index; every size feeding our own allocations
is checked; `setGlobalThreadCount` is a no-op when the count is unchanged, so
concurrent thumbnails do not rebuild the pool; the session store is
in-process only and takes nothing from files; the extensions request no
network or file entitlements beyond Quick Look's own.

**Fuzzing.** A mutation fuzzer (`Tools/fuzz.sh`; no libFuzzer in the Command Line Tools) ran
the whole public API -- open, render in several views, layer switch, describe
-- under AddressSanitizer and UndefinedBehaviorSanitizer on EXRCore, seeded
with the fixture corpus and openexr-images: 4 workers x 5 minutes,
**370,650 mutated files, 59,225 of them valid enough to open and render, no
sanitizer report**. A short run, not a substitute for a standing fuzz target
in CI (still not built), but our code survived it after the fixes above.

**Not fixed here: the release process.** Each of these is a decision:
- ~~The published DMG is built on the maintainer's Mac; nothing ties it to
  this repository's source.~~ Done 2026-10-08: after 0.2.0, CI builds the DMG
  on a `v*` tag, attests it (`actions/attest`) and drafts the release; users
  can run `gh attestation verify`.
- ~~Actions pinned by tag; pip installs unpinned.~~ Done 2026-10-08: actions by
  commit SHA, pip by hash. The write permissions live in a separate release
  job that only downloads, attests and drafts.
- ~~There is no SECURITY.md, and private vulnerability reporting is not enabled
  on the repository.~~ Done 2026-10-08: `SECURITY.md`, reporting enabled.
- Unsigned by choice: users are told to clear the quarantine flag, which is
  also what a trojaned copy would ask. The SHA-256 in the release notes
  helps only if users check it.

### 6K plates under budget: taller read bands (2026-10-08)

The 6K DWAA thumbnail (347 ms against the 300 ms budget) was an open question
of whether to raise the decode threads past plan §6.4's 2-4. Measured first:

| Threads | 2 | 3 | 4 | 6 |
|---|---|---|---|---|
| 6K thumbnail, 64-line bands | 523 ms | 347 ms | 344 ms | 341 ms |

Past three threads nothing improved, so threads were not the limit. The decode
read 64 lines per call, and OpenEXR only parallelises the chunks within one
call; a DWAA chunk is 32 lines, so at most two were ever in flight. With
256-line bands:

| Band | 3 threads | 4 threads | Peak memory (3 threads) |
|---|---|---|---|
| 64 lines | 347 ms | 344 ms | 483 MB |
| 128 lines | 340 ms | 325 ms | 491 MB |
| 256 lines | **289 ms** | 278 ms | 500 MB |
| 512 lines | 288 ms | 256 ms | 527 MB |

Shipped: about 1.5 Mpixel per band, 32 to 256 lines (256 at 6K), threads kept
at 3. Thumbnail 290 ms, preview 336 ms (was 392). Renders are byte-identical
to the 64-line version (6K plate at full resolution and five fixtures): rows
are still accumulated in the same order. The thread count needs no decision now.

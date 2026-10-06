# EXR Quick Look for macOS — Implementation Plan

**Hand this to a Claude Code thread.** It contains the research, the decisions already made, and the build order. Decisions in §3 were reached deliberately — do not relitigate them without raising it first.

---

## 1. What we're building

A modern, open source macOS Quick Look extension that gives Finder correct thumbnails and previews for OpenEXR files, including the features the format has gained since Apple's support was written.

**Ship target:** a signed, notarised app containing two app extensions, distributed as a DMG from GitHub Releases and (later) a Homebrew cask. Open source, permissively licensed, free for anyone.

### Non-goals

Be strict about these. Scope creep here kills the project.

- Not an image editor, and never writes EXR files
- Not a sequence player or contact sheet
- No deep data (deep scanline/tiled) rendering — detect, report in metadata, fall back gracefully
- No OCIO dependency, no config files, no reliance on any other installed application
- No colour management UI beyond one fallback preference

---

## 2. Research findings (already done — don't redo)

### 2.1 The old plugin model is dead

The `.qlgenerator` bundle dropped into `~/Library/QuickLook` no longer exists as a mechanism. Deprecated in Catalina (10.15), and **macOS 15 Sequoia removed support for third-party qlgenerators entirely**. Apple's own generators in `/System/Library/QuickLook` still carry the extension, which misleads people, but all third-party support must now be app extensions.

Consequences that shape the whole design:

- App extensions can only ship inside an app bundle. A host app is mandatory, not a design choice.
- Extensions must be **signed and sandboxed**.
- `qlmanage` is now near-useless: its `-m` option lists only Apple's qlgenerators and cannot see third-party app extensions at all. Use `pluginkit` instead (see §8).
- Users enable/disable extensions in **Login Items & Extensions → Quick Look** in System Settings.

### 2.2 What macOS currently does with EXR

Apple does **not** use the OpenEXR reference implementation. It ships a proprietary decoder, `libAppleEXR.dylib`, shared across macOS, iOS, iPadOS and visionOS. ImageIO dispatches to it automatically for thumbnails, previews and media analysis, and **detects format by content, not file extension**.

Known problems:

- Long-standing reports of EXRs previewing as flat white or black in Finder, Quick Look and Preview.app depending on whether the file contains alpha — a broken alpha/display path, not a decode failure.
- No user control over the display transform. Scene-linear data gets whatever Apple decided.
- Closed and undocumented, so its behaviour is not something we can specify against or test against.
- Security history: **CVE-2026-28977**, an unchecked 64-bit integer overflow in allocation math in `CreateDecompressedLocations`, gave a zero-click heap overflow reachable through Finder, Messages, AirDrop, Mail and Safari. Fixed in macOS 26.5 / iOS 18.7.9. Root cause was unbounded attacker-controlled image dimensions feeding unchecked allocation size arithmetic. **This directly informs our hardening requirements in §6.**

### 2.3 Nothing exists in this niche

A curated list of ~50 modern `.appex` Quick Look extensions for Sequoia and later contains **zero** HDR or EXR image entries. The only project that ever claimed `.exr` was `qlImageSize`, a legacy qlgenerator, dead since Sequoia and thin on EXR anyway.

### 2.4 Prior art worth reading

| Repo | Why |
|---|---|
| `AcademySoftwareFoundation/openexr` | Reference implementation. Currently 3.4.5. |
| `AcademySoftwareFoundation/openexr-images` | Official test corpus. Includes singlepart/multipart pairs of the same sequence, NaN/infinity images, out-of-gamut colours, compression-artefact cases. Explicitly recommended as test cases for reading code. |
| `Marginal/QLVideo` | Closest analogue: adds Finder thumbnails/previews/metadata for formats macOS handles badly. **GPL-2 — read for approach, do not copy code.** |
| `smittytone/PreviewJson`, `PreviewYaml`, `PreviewCode` | Cleanest host-app + previewer appex + thumbnailer appex + prefs pattern. |
| `sbarex/SourceCodeSyntaxHighlight` | C++ core wrapped in a Swift appex — same shape as linking OpenEXR. |
| `wkjarosz/hdrview` | The UX model for channel/layer grouping and multi-part handling. |
| `afichet/openexr-thumbnailer` | The Linux equivalent of this project. |
| `dhoerl/QuickLook-Preview` | Minimal proof of how to debug a QL extension from Xcode. |
| `Oil3/PluginKits` | Extension management/debugging tool for macOS. |

---

## 3. Decisions already made

| # | Decision | Reason |
|---|---|---|
| D1 | App extension architecture (host app + thumbnail extension + preview extension) | Only mechanism that exists post-Sequoia |
| D2 | Link the **OpenEXR reference library**, not TinyEXR | TinyEXR does not support DWAA/DWAB and won't — flagged patent-encumbered, because the DreamWorks grant doesn't extend outside the OpenEXR standard. DWAA is a hard requirement. |
| D3 | **ACES 2.0 output transform**, baked to a 3D LUT at build time | Fixed, predictable, self-contained |
| D4 | **Zero runtime dependencies.** No OCIO, no `$OCIO`, no assumption that Nuke or any ACES-aware app is installed | Explicit requirement |
| D5 | Render to **Display P3**, tag the output, let ColorSync handle the monitor | Correct on modern Macs without us probing displays |
| D6 | Crop to the **display window** by default | Overscan must not shift framing |
| D7 | **Thumbnail and preview use the identical transform** by default. Bounded exception: a user-set preview override may diverge, and must be visibly badged when it does. | Silent divergence reads as a bug; explicit, visible divergence doesn't |
| D8 | **No auto-exposure, no per-image normalisation, ever** | Would give every frame of a sequence a different brightness |
| D9 | Exactly **one preference**: assumed input colourspace when the file carries no chromaticities. Default ACEScg. | Everything else is derived from the file |
| D10 | Permissive licence (BSD-3-Clause or MIT) | Matches OpenEXR; keeps it freely usable |

---

## 4. Phase 0 — Feasibility spike (BLOCKING, do this first)

**This is the make-or-break unknown. Do not build anything else until it's answered.**

`com.ilm.openexr-image` is a **system-supported UTI**. Apple DTS has confirmed that system-wide default UTIs are given priority over third-party declarations, and there is no supported way to override this. On iOS, App Store validation actively rejects `QLSupportedContentTypes` arrays containing system-supported types. There is anecdotal evidence that **thumbnail extensions may be honoured where preview extensions are not** — that split needs verifying on current macOS.

### The spike

Build the smallest possible thing:

1. Empty SwiftUI host app.
2. A `QLThumbnailProvider` extension declaring `com.ilm.openexr-image`, returning a solid red square.
3. A `QLPreviewingController` extension declaring the same, returning a solid blue view.
4. Sign locally, run each extension target from Xcode, enable in Login Items & Extensions.

### Record results separately for each

- [ ] Does the **thumbnail** extension get invoked for `.exr` in Finder icon view?
- [ ] Does the **preview** extension get invoked on spacebar?
- [ ] Does it survive `qlmanage -r`, a logout, and a reboot?
- [ ] Does behaviour differ between Finder, the preview pane, column view, Spotlight, and Open dialogs?
- [ ] Capture the relevant `log stream` output either way.

### Decision gate

| Outcome | Action |
|---|---|
| Both work | Proceed as planned |
| Thumbnail only | Proceed, but the preview pane becomes a documented limitation. Consider a companion viewer app opened by double-click. |
| Neither works | **Stop and report back before writing more code.** Fallbacks to evaluate: a custom exported UTI claiming `.exr`; shipping a standalone viewer app plus a Services/Finder-extension entry point; or a CLI tool. Each is a materially different product and needs sign-off. |

Write the findings into `docs/uti-findings.md` with dates and OS version. This is genuinely useful to the community regardless of outcome.

---

## 5. Architecture

```
exr-quicklook/
├── EXRPreview.xcodeproj
├── EXRPreview/                 Host app — minimal, registers extensions
├── EXRThumbnail/               QLThumbnailProvider appex
├── EXRQuickLook/               QLPreviewingController appex
├── EXRCore/                    Framework: all decode + colour logic. No UI.
│   ├── Reader/                 OpenEXR wrapper, header parse, layer selection
│   ├── Color/                  Chromaticities → AP1 → LUT → Display P3
│   └── Resources/              Baked ACES 2.0 LUT
├── EXRCLI/                     Debug harness: exr in, PNG out. Same code path.
├── Vendor/openexr/             Pinned OpenEXR + Imath, built universal static
├── Tools/bake-lut/             Build-time LUT generation (dev machine / CI only)
├── Tests/
│   ├── Fixtures/               Generated + openexr-images corpus
│   └── Golden/                 Reference PNGs
└── docs/
```

**Key principle:** `EXRCore` and `EXRCLI` mean almost everything is testable without touching Quick Look at all. Build and validate the whole pipeline through the CLI first; the extensions become thin wrappers. Given how painful QL extensions are to debug, this is not optional.

- **Deployment target:** macOS 14.0. Develop and test on 26.x.
- **Architectures:** universal (arm64 + x86_64).
- **Preferences:** shared App Group, host app writes, extensions read. Note that preview changes apply immediately but **thumbnail changes often don't appear until you open a folder not visited this session, or log out and back in** — document this, it's not a bug.

---

## 6. Phase 1 — EXRCore

### 6.1 Build OpenEXR

Pin a specific release (3.4.x). Build Imath + OpenEXR as universal static libraries via CMake. Script it in `Tools/build-openexr.sh` so CI reproduces it exactly. Static linking keeps the appex self-contained.

Enable `OPENEXR_BUILD_TOOLS=OFF`, `BUILD_TESTING=OFF`, examples off.

### 6.2 Header parsing and inspection

Extract, without decoding pixels:

- Part count, part names, part types (scanline / tiled / deep)
- Per-part channel list with pixel types (half / float / uint) and sampling
- `compression` (with DWAA/DWAB compression level if present)
- `dataWindow` and `displayWindow`
- `chromaticities` if present; ACES container flag if present
- `pixelAspectRatio`, `screenWindowWidth`, line order, tile description
- Whether a `preview` attribute exists
- Arbitrary string/custom attributes for the metadata panel

### 6.3 Primary layer selection

The most important logic in the project. Deterministic, documented, unit-tested.

**Part selection (multi-part files):** prefer the part whose name is empty, `rgba`, `beauty`, `main` or `composite`, and which contains R/G/B channels. Otherwise the first part containing R/G/B. Never a deep part.

**Layer selection within a part:** group channels by dot-separated prefix; empty prefix is the default layer. Preference order:

1. Unprefixed `R`,`G`,`B` (+`A`)
2. Layer named `beauty` / `rgba` / `main` / `composite`
3. Luminance-chroma: `Y`,`RY`,`BY` (handle subsampling)
4. First remaining layer resolving to three RGB channels
5. Single-channel layer → render greyscale
6. `Y` alone → greyscale

**Never auto-selected** (still listed in the UI, just never the default): cryptomatte layers (`CryptoObject*`, `CryptoMaterial*`, `CryptoAsset*`, and the `cryptomatte/*` metadata pattern), `Z` / `depth` / `ZBack`, normals, motion/velocity, `id`, and mask/matte layers.

**Alpha rules:**

- Alpha is only ever taken from the **same layer and part** as the chosen RGB. Never borrowed.
- EXR alpha is associated (premultiplied).
- **Default display ignores alpha entirely** — show RGB as the comp shows it. This is what Nuke's viewer does and it structurally prevents the white/black failure mode users hit with Apple's decoder.
- Preview offers a toggle to composite over a checkerboard. Thumbnails never do.

### 6.4 Pixel decode

- Request **only the channels needed** for the chosen layer via the FrameBuffer. On a 40-channel AOV file this is an enormous saving.
- If a `preview` attribute exists and the target size is small enough, use it and skip decode entirely.
- If tiled and mipmapped, read the smallest adequate mip level.
- Set a modest OpenEXR thread count (2–4). The appex is sandboxed and memory-capped; don't spawn per-core threads.
- Handle NaN and infinity explicitly — the corpus contains them deliberately. Clamp to a defined value, never propagate.

### 6.5 Colour pipeline

Fixed order, no branches beyond the chromaticities lookup:

1. Read `chromaticities`. If absent, use the preference default (ACEScg / AP1).
2. Build the 3×3 matrix from source primaries + white point to AP1, applying a Bradford chromatic adaptation when white points differ (D60 vs D65 matters here).
3. Apply a log shaper, then the baked **ACES 2.0 output transform** 3D LUT (65³, tetrahedral interpolation), targeting Display P3.
4. Emit 16-bit Display P3, tagged with the Display P3 ICC profile.
5. Fixed exposure offset of 0 stops. **No auto-exposure.**

### 6.6 Hardening — treat this as a requirement, not a nicety

CVE-2026-28977 in Apple's decoder was precisely an unchecked allocation-size multiplication with no upper bound on image dimensions, and it had one overflow check that guarded the wrong product. We are writing the same class of code.

- Use `__builtin_mul_overflow` / `__builtin_add_overflow` for **every** arithmetic operation feeding a buffer size. Not most. Every one.
- Enforce hard upper bounds on width, height, total pixel count, channel count, part count and tile counts. Reject rather than clamp.
- Validate `dataWindow` and `displayWindow` for sanity and consistency before allocating anything.
- Impose a decode deadline and a memory ceiling. On breach, **fail gracefully to the generic icon** — never crash, never hang. A crashing appex is worse than no appex.
- Treat every input as hostile. Files arrive from renders, shared drives and the internet.

### 6.7 Performance budget

| Case | Target |
|---|---|
| Thumbnail, 4K DWAA, Apple Silicon | < 300 ms |
| Preview, 4K DWAA | < 500 ms to first paint |
| Hard ceiling before graceful bail | 2 s |

---

## 7. Phase 2 — Baking the ACES 2.0 LUT

Runs on the dev machine and in CI, **never at runtime**.

1. Generate the AP1 → ACES 2.0 output transform → Display P3 LUT. Prefer OCIO's ACES 2.0 built-in transforms if the pinned OCIO version provides them; otherwise use the AMPAS reference implementation. **Verify which is current before writing this — don't assume.**
2. Output a 65³ LUT plus the shaper definition.
3. Commit the LUT to the repo with its SHA-256, the generation script, and a `docs/lut-provenance.md` recording the exact tool versions and transform names used.
4. Add a CI check that regenerating reproduces the committed hash.

The shipped extension links no colour-management library at all.

---

## 8. Phase 3 — Extensions

### Thumbnail extension

`QLThumbnailProvider`. Decode at the requested size, apply the pipeline, return a `QLThumbnailReply`. Fast path via the `preview` attribute or mip levels where available.

### Preview extension

`QLPreviewingController`. Shows the primary layer by default, plus a layer/part switcher (never the default view) and a metadata panel: compression and level, chromaticities (named where recognised — ACEScg, ACES2065-1, Rec.709), part count, channel list, data and display windows, pixel type, deep flag.

#### The floating overlay

A collapsed button in the upper right expands to a translucent HUD panel (`NSVisualEffectView`, HUD material). Precedent: BetterZip's Quick Look extension does the same thing with a settings button that reveals options in the pop-up and hides again.

Contents, ordered by expected frequency of use:

| Control | Cost | Notes |
|---|---|---|
| Exposure (stops) | Free — analytic, applied before the shaper | Manual and explicit, so no conflict with the no-auto-exposure rule (D8) |
| Channel isolation: R / G / B / A / luminance | Free | |
| View transform | One baked LUT each | ACES 2.0 → sRGB, → Display P3, → Rec.709, plus **raw / no transform** |
| Input space override | Free — matrix only | Only meaningful when the file is ambiguous; see below |
| Alpha over checkerboard | Free | |
| Data window vs display window | Free | |

**Raw / no transform is not optional.** Once the layer switcher lets you reach `Z`, normals or motion vectors, the ACES curve mangles them. Raw is the only sane way to inspect a data pass.

**LUT sizing:** 65³ for the ACES 2.0 default (~3.3 MB), 33³ for alternates (~430 KB). Indistinguishable at preview resolution and it keeps the bundle sane.

**Rendering context:** the same view controller is used by every `QLPreviewView` — the spacebar panel, the Finder preview pane, the column-view sidebar, Spotlight. Auto-hide the overlay below a width threshold, and never make it the only route to anything essential.

**Do not block first paint.** Quick Look spins until the completion handler fires. Render with the resolved settings, complete, then build the overlay.

#### Session-sticky settings

Overrides are transient — they never write to committed preferences unless the user picks "set as default" explicitly. But they must survive arrowing through a folder, which is the primary way these files get looked at.

**Storage, two tiers:**

1. In-memory session state in the extension process — the hot path.
2. Mirrored to a record in the shared App Group store, written on change (debounced), read on load. Extension processes get terminated on idle and under memory pressure; without this, settings vanish for no visible reason.

**Carry-over rules differ by setting class:**

- **View-side settings** — exposure, channel isolation, view transform, alpha and window toggles — carry **unconditionally**. They describe how the user is looking, not what the file is.
- **Input space override** carries **conditionally**: applies to the next file only if that file presents the same ambiguity (no `chromaticities` attribute). If the next file declares its primaries, the file wins and the override stands down. Silently overriding a file that told the truth is how someone ends up looking at wrong colour without knowing it.
- **Layer selection** is sticky **by name**. Remember the selected layer name; if the next file has a layer with that name, select it, otherwise fall back to the primary layer. This is the frame-sequence case and it's what a viewer is expected to do.

**Visibility, non-negotiable:** whenever any override is active, the collapsed overlay button shows a badge (and ideally the exposure value inline), with one-click reset to defaults. An invisible override is a bug report waiting to happen.

**TTL backstop:** session state lapses after a defined idle period, so a user doesn't return days later to everything at +2 stops with no memory of setting it. Tune the value once there's real usage; start at 30 minutes.

**Scope:** a single global session record. Per-directory keying is an option to evaluate later, but it grows unbounded and breaks when moving between sibling folders of the same shot.

**Thumbnails ignore all of this** and always render committed defaults. This is a deliberate, bounded exception to D7 — the user set the override themselves and the badge says so.

**Verify empirically:** whether the extension process actually survives between arrow presses. It changes nothing about the design, but determines whether the App Group read is the cold path or the hot one.

### Debugging (save yourself hours)

- `qlmanage` cannot see app extensions. Use `pluginkit -mAvvv -p com.apple.quicklook.preview` and `...thumbnail`.
- Run the extension target directly from Xcode and pick Finder as the host app.
- `log stream --predicate 'subsystem CONTAINS "quicklook"' --info --debug`, with log privacy disabled or half the messages are redacted.
- Reboot when things get stuck. It's not superstition here.

---

## 9. Phase 4 — Testing

### Corpus

1. **`openexr-images`** as a git submodule. The multipart/singlepart pairs, NaN/infinity, out-of-gamut and compression-artefact images are the point.
2. **Generated fixtures** — a script producing the cases that actually match production, since the official corpus doesn't cover them:
   - DWAA ACEScg, multi-layer, with mask layers and a cryptomatte
   - DWAB variant
   - ACES2065-1 (AP0) file
   - File with **no** chromaticities attribute
   - Overscan: dataWindow larger than displayWindow
   - Single-channel, luminance-chroma, and 32-bit float variants
   - A deep file (must degrade gracefully, not crash)
3. **Malformed corpus** — truncated files, absurd dataWindows, huge tile counts, and a regression case modelled on the CVE-2026-28977 geometry. All must fail cleanly.

### Test types

- Unit tests on layer selection — the most bug-prone logic, so cover it hardest
- Golden-image regression: CLI renders to PNG, compared against references within tolerance
- Property tests: never crash, never exceed the deadline, never exceed the memory cap
- A fuzz target over the header parser, run in CI

---

## 10. Phase 5 — Ship it

- **CI (GitHub Actions):** build universal, run all tests, verify LUT hash, upload artefacts
- **Release:** DMG, signed and notarised (leave the signing identity configurable; we'll cross that bridge when we get there), attached to GitHub Releases
- **Homebrew cask** once there's a stable release
- **README:** what it fixes and why, screenshots comparing against Apple's rendering, the Login Items & Extensions step, the thumbnail-cache caveat, supported feature matrix, honest limitations
- **`docs/uti-findings.md`** published — genuinely useful to others regardless of how Phase 0 goes

### Definition of done

- [ ] Phase 0 answered and documented
- [ ] DWAA and DWAB ACEScg files render correctly, matching a known-good reference
- [ ] Multi-layer files show the primary layer, never a mask or cryptomatte
- [ ] ACES2065-1 and ACEScg both handled; missing chromaticities falls back cleanly
- [ ] Thumbnail and preview are pixel-identical for the same file with no override active
- [ ] Overlay settings carry across files when arrowing through a folder, survive an extension process restart, and are badged whenever active
- [ ] Input space override stands down on files that declare their own chromaticities
- [ ] Sticky layer selection resolves by name and falls back cleanly when the name is absent
- [ ] Overscan files crop to the display window
- [ ] Malformed corpus fails gracefully, zero crashes
- [ ] Performance budgets met on 4K DWAA
- [ ] Universal binary, signed, notarised, installs and works from the DMG
- [ ] CI green, LUT reproducible

---

## 11. Raise before proceeding

- Phase 0 outcome if it's anything other than "both work"
- If the ACES 2.0 LUT can't be generated from a well-sourced reference — do not substitute an approximation silently
- If OpenEXR static linking blows the appex size or sandbox budget

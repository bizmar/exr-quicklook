# How EXR Quick Look picks what to show

A plain-language account of the rules, for troubleshooting and transparency.
If a file shows the wrong layer, or a pass looks wrong, this is where to look.
The code is `EXRCore/src/exr_layers.cpp`; the two must stay in step.

## In one paragraph

An EXR can hold many images: a **beauty** (the finished picture), lighting
passes (diffuse, specular…), and **data passes** (depth, position, normals,
motion vectors, IDs, masks, cryptomatte) that hold numbers rather than
pictures. The thumbnail and the default preview show the beauty. Everything
else is listed in the preview's layer menu. Data passes are marked **"· data"**
there, and shown **Raw** (values straight to the screen, no colour transform),
because the ACES view would squash numbers into something unreadable.

## 1. Which channels form a layer

Channel names are split at the last dot: `diffuse.R` is channel `R` of layer
`diffuse`; `char.diffuse.R` is layer `char.diffuse`. Channels with no dot
(`R`, `G`, `B`, `A`, `Z`) form the **default layer**.

Within a layer:

| Channels found | Shown as |
|---|---|
| `R` `G` `B` (+ `A`) | colour |
| `red` `green` `blue` (+ `alpha`) | colour, the same way. Nuke writes these long names for layers other than rgba |
| `x` `y` (+ `z`) or `u` `v` | the components as red, green, blue (blue = 0 if there are only two), as Nuke's viewer does. Always data. |
| `Y` with `RY` `BY` | luminance-chroma. Not decoded: such files keep the generic icon |
| one channel | greyscale |

Alpha only ever comes from the same layer as the colour; it is never borrowed
from another layer. Any channel a layer's view doesn't use (a `Z` beside `RGB`,
say) is also offered on its own, as a data entry.

## 2. Which layer is the default

Tried in order, first match wins:

1. A part whose name is a **beauty name** and that contains colour. If
   none, the first part containing colour. Deep parts are never chosen.
2. Within that part:
   1. the unprefixed `R` `G` `B`;
   2. a layer with a **beauty name**;
   3. luminance-chroma;
   4. the first remaining colour layer that isn't a data pass;
   5. the first single channel that isn't a data pass (greyscale).
3. **Only data passes in the whole file?** (Separate-AOV renders write one pass
   per file: a depth-only or position-only EXR.) Then the first data pass is
   shown, Raw, rather than the generic icon.
4. **Only cryptomatte?** Its numbered layers (`uCryptoObject00`…) hold ID
   hashes with no readable form, but renderers also write an un-numbered colour
   **preview** (`uCryptoObject`). That is shown if present; otherwise the file
   keeps the generic icon.

**Beauty names** (case-insensitive, matched against the last dot-component):
`rgba`, `rgb`, `beauty`, `main`, `composite`, `combined` (Blender), `ci`
(RenderMan), `c` (Karma, Mantra), `finalimage` (Unreal).

## 3. What counts as a data pass

A layer is a data pass if **any** of these holds. For an unprefixed layer in a
multi-part file, the **part** name is tested instead, because renderers often
name the part after the pass.

| Tier | Rule | Examples |
|---|---|---|
| 1 | **"crypto" anywhere** | `CryptoObject00`, `uCryptoAsset`, `crypto_material` |
| 2 | **A distinctive word anywhere**: depth, position, normal, motion, velocity, vector, matte, mask, rendertime, cputime, raycount, facingratio, dpdtime, volumez, samplerinfo, wirecolor | `VRayZDepth`, `MotionVectors`, `WorldPosition`, `PuzzleMatte`, `shadowMatte`, `facingRatio` |
| 3 | **A short name as a whole dot-component**: `z` `zback` `pz` · `n` `nn` `nw` `ng` `ngn` `nt` `tn` `vn` · `p` `po` `pc` `pow` `pref` `pw` `pworld` `wp` · `mv` `mvec` `forward` `backward` · `uv` `st` `uvw` · `id` `objectid` `materialid` `instanceid` · `indexob` `indexma` `mist` | `Z`, `P`, `N`, `Pz`, `ViewLayer.Mist`, `ViewLayer.IndexOB`, `uv` |
| 4 | **A word inside a compound name**: `uv` `uvw` `st` `id` `nw` `nworld` `mv` `mvec` `pref` `pworld` `zdepth` (words split at `_` `-` `.`, digits and camelCase) | `s_uv`, `instanceID`, `Op_Id`, `__Nworld`, `CGeometry_UvwMap` |
| 5 | **The `m_` prefix** many studios use for mattes | `m_chars`, `m_set` |
| 6 | **By content**: x/y/z or u/v channels, a file tagged `colorInteropID = data`, or a lone extra channel | `foo.x foo.y foo.z`, a `Z` beside RGB |

**Why single letters are restricted to tier 3.** As substrings, `p`, `n` or `z`
would catch nearly every name. Even as words they would misfire on light
groups such as `rim_n` or `key_p`. So they only count when they are the
*whole* component (`P`, `N.x`, `depth.Z`).

**What stays imagery** (and goes through the ACES view): lighting passes
(diffuse, specular, reflection, refraction, SSS, GI, emission, AO, volume,
shadows), filters and albedo, light groups, and anything not caught above.

## 4. Known ambiguities

Names that are studio conventions rather than standards, and are **not**
treated as data:

- `exitdir`, `lightVisibility`: seen in the Netflix Sole Mates renders. A
  direction is data, but the name gives no general rule.
- `s_*` attribute AOVs (`s_textcolors`, `s_head_face_nose`): could be colours
  or IDs.
- **The `m_` prefix** assumes a matte. A colour pass named `m_…` would be shown
  Raw. This is rare, and you can still pick a view by hand.

If one of these is wrong for your files, the layer is still in the menu, and
choosing a view transform by hand overrides Raw. Please
[report it](https://github.com/bizmar/exr-quicklook/issues/new?template=test-report.yml).

## 5. Colour, briefly

Which primaries a file is assumed to have, in order:

1. your override in the preview;
2. the file's `chromaticities`;
3. a scene-linear `colorInteropID` (OpenEXR 3.4);
4. **`arnold/color_space`**: Arnold records its working space by OCIO name.
   It's matched against each space's OCIO name and aliases, so older ACES
   config spellings ("ACES - ACEScg", "Utility - Linear - sRGB") work. Arnold's
   built-in name `linear` means linear sRGB / Rec.709, its default rendering
   space since Arnold 5. Display spaces and unknown names are ignored;
5. the default, **ACEScg**.

The info panel says which of these decided, and when a tag was ignored.

**HDR masters (PQ).** Some deliveries are EXRs holding PQ-encoded (SMPTE
ST 2084) HDR. That data is already tone-mapped and measured in absolute nits,
not scene-linear, so shown as-is it looks washed out and off-colour. The input
picker offers **P3-D65 PQ (1000 or 4000 nits)** and **Rec.2100 PQ (1000
nits)**. Each undoes the matching ACES 2.0 HDR output transform (baked from
OpenColorIO's inverse) and then shows the result through the normal view. Pick
the nit level the master was graded at. Nothing in these files' headers says
they are PQ, so this is never chosen automatically; choose it once and it
carries across the sequence.

**Real-world caveat:** of the 291 production and test files in the corpus
below, **none** carry `chromaticities` or `colorInteropID`. Not Netflix's ACES
camera footage, not its PQ-encoded HDR films, not Blender's renders, not Poly
Haven's HDRIs. A handful of Arnold files carry `arnold/color_space` (`ACEScg`,
`linear`). In practice the ACEScg default decides almost every file, which is
why the input-colourspace override carries across a sequence.

## 6. Evidence

**Real files.** `Tests/Fixtures/realworld/` holds the part and channel names
(names only, no pixels) of 291 openly licensed files from 8 sources: Netflix
Animation's Sole Mates renders (Arnold, Nuke), Psyop's Cryptomatte samples
(Arnold, V-Ray), Blender's Tears of Steel, Netflix Open Content, Poly Haven,
and the Gaffer, OpenImageIO and tlRender test suites. See `SOURCES.md` there.
`test_realworld` runs these rules over every file. It enforces three invariants:
- the default is never a data pass when imagery exists;
- cryptomatte is never the default except a preview in a cryptomatte-only file;
- every listed layer names real channels.

It also compares the full result with `expected.txt`, so any rule change shows
up as a reviewable diff of its effect on real files.

**Documentation only.** No openly licensed multi-layer files from Redshift,
Karma, Octane, Cycles, Unreal or Corona could be found. Their default AOV names
are taken from the vendors' documentation and checked in `test_layers.cpp`
(`test_documented_renderer_names`). Real files from these renderers are the
most useful thing you can contribute. Attach the output of
`build/exrcli -v file.exr`: it lists names only, no pixels.

## 7. Changing a rule

1. Edit `exr_layers.cpp`, and add a case to `test_layers.cpp`.
2. Run `Tools/run-tests.sh`. If `test_realworld` reports a changed
   classification, rerun it with `--update`.
3. Read the diff of `Tests/Fixtures/realworld/expected.txt`. Every changed line
   is a real file your change affects. Make sure each one is intended.
4. Update this document.

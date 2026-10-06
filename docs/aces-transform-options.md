# ACES 2.0 output transform — why bake it, and which source

Research for Phase 2 (plan §7). Measurements taken on this machine on
2026-09-06; versions verified rather than assumed, per CLAUDE.md.

> **DECIDED 2026-09-06 — Option A + D.** Bake a 65³ LUT from OCIO 2.5.x's
> built-in `cg-config-v4.0.0_aces-v2.0_ocio-v2.5`, default view
> **`ACES 2.0 - SDR 100 nits (P3 D65)`** on `Display P3 - Display`, plus
> OCIO-generated golden images so the interpolation error is measured on every
> CI run. Multi-display policy in §8.

---

## 1. Why bake a LUT at all

The short answer: because the ACES 2.0 Output Transform is not a curve, and we
are not allowed to ship the library that evaluates it.

### What ACES 2.0 actually does per pixel

ACES 2.0 replaced the ACES 1.x RRT+ODT with a appearance-model-based rendering.
Per the [ACES output transform documentation][acesdocs], the chain is:

```
ACES RGB → JMh  (Hellwig 2022 Colour Appearance Model, simplified)
          → Tonescale        (acts on J, lightness)
          → Chroma Compression (acts on M, colourfulness)
          → Gamut Compression  (acts on J and M together)
          → JMh → limiting-primary RGB
          → display encoding
```

Three properties of that chain matter to us:

1. **It is not closed-form end to end.** The tonescale is a closed-form rational
   function, but gamut compression needs the gamut boundary as a function of
   hue. The reference builds **gamut boundary tables with 360 hue entries**, and
   the AP1 cusp uses a Fourier approximation
   (`11.34072·cos(h) + 16.46899·cos(2h) + 7.88380·cos(3h) + 14.66441·s`).
2. **It is expensive.** Measured below: ~425 ns per pixel in OCIO's optimised
   C++ implementation.
3. **It is a lot of code to get exactly right.** A CAM forward and inverse, a
   chroma compression curve, and a gamut compressor with boundary search. Subtle
   divergence would be invisible in testing and wrong in output.

### What our constraints do to that

| Constraint | Consequence |
|---|---|
| **D4** — zero runtime dependencies | We cannot link OCIO into the appex. Whatever runs at runtime, we wrote or we baked. |
| **§6.7** — thumbnail < 300 ms, hard ceiling 2 s | See the measured per-pixel cost below. |
| **§6.6** — never hang, appex is memory-capped | A per-pixel iterative gamut search is exactly the kind of thing that has a bad tail. |
| **D7** — thumbnail and preview pixel-identical | Far easier to guarantee with one baked table than two code paths. |

Baking moves all of that to build time. The shipped runtime becomes: apply a log
shaper, do a tetrahedral lookup in a 3D table. That is a handful of arithmetic
operations per pixel, no library, no iteration, no tail latency, and it is
trivially identical between the thumbnail and preview extensions.

**The cost of baking is interpolation error**, quantified in §4. That is the
real trade and it is not free.

---

## 2. What is current (verified 2026-09-06)

| Thing | Status | How verified |
|---|---|---|
| ACES 2.0 | **Released 2025-04-04** | [`aces-aswf/aces-core` CHANGELOG][changelog] |
| Reference repo | `ampas/aces-dev` → **`aces-aswf/aces-core`** | Repo renamed at the 2.0 release; `aces-dev` keeps pre-2.0 history |
| OCIO ACES 2.0 support | Preview in 2.4.0, **"Preview" label removed in 2.4.2** | [OCIO 2.4 release notes][ocio24] |
| OCIO built-in ACES 2.0 configs | **Added in 2.5.0** | [OCIO 2.5 release notes][ocio25] |
| Latest OCIO | **2.5.2** (bug-fix + CVE-2026-42450) | [OCIO releases][ociorel] |
| Installed here | **OCIO 2.5.1** via Homebrew (2.5.2 available) | `brew info opencolorio` |

A detail worth knowing: the aces-core 2.0 changelog says the release involved
**"code refactoring to align with optimizations from OCIO 2.4.2"**. The reference
implementation and OCIO converged deliberately — so "OCIO" and "the reference"
are not two meaningfully different answers for ACES 2.0. That materially weakens
the plan's implied preference ordering in §7.

The ACES 2.0 configs are present in the OCIO already on this machine:

```
cg-config-v4.0.0_aces-v2.0_ocio-v2.5
studio-config-v4.0.0_aces-v2.0_ocio-v2.5
```

and the view we want for **D5 (render to Display P3)** exists:

```
display: "Display P3 - Display"
   view: "ACES 2.0 - SDR 100 nits (P3 D65)"
```

Also available, relevant to the §8 overlay's alternate view transforms:
`sRGB - Display`, `Rec.1886 Rec.709 - Display`, `Display P3 HDR - Display`
(`ACES 2.0 - HDR 1000 nits (P3 D65)`), plus `Un-tone-mapped` and `Raw`.

---

## 3. Measured cost of *not* baking

Direct evaluation through OCIO's optimised CPU processor, ACEScg → Display P3,
this machine (M2 Pro):

| Pixels | Time | Per pixel |
|---|---|---|
| 16,384 (128²) | 12.4 ms | 754 ns |
| 262,144 (512²) | 117 ms | 448 ns |
| 1,048,576 (1024²) | 445 ms | 425 ns |

Extrapolated to a full 6000×4000 frame: **~10 seconds**, five times over the
§6.7 hard ceiling.

**But note what changed since the plan was written.** EXRCore now downsamples
*during* decode, so the transform only ever sees the output resolution, never
the source resolution. That reframes the numbers:

| Realistic target | Pixels | Direct-eval cost |
|---|---|---|
| Finder icon, 128 px | ~11 k | ~8 ms |
| Thumbnail, 512 px | ~175 k | **~74 ms** |
| Preview panel, ~1400×900 | 1.26 M | **~535 ms** |

So direct evaluation is *not* obviously impossible any more. It fits the
thumbnail budget comfortably and sits just over the 500 ms preview first-paint
target. This is why the choice is genuinely open rather than settled.

---

## 4. Measured cost of baking: interpolation error

Baked ACEScg → Display P3 SDR 100 nits, ACEScct log shaper, **tetrahedral**
interpolation (as §8 specifies), compared against direct evaluation. Errors in
8-bit display code values (`/255`), 30 000 random samples per row.

| LUT | mid greys | HDR highlights (0–16) | saturated primaries | near-black |
|---|---|---|---|---|
| **33³** (0.21 MB fp16) | max 9.8 · p99.9 2.9 · mean 0.22 | max 38.2 · p99.9 8.1 · mean 0.34 | max 24.3 · **p99.9 22.8** · mean 0.85 | max 0.9 · mean 0.13 |
| **65³** (1.57 MB fp16) | max 5.0 · p99.9 1.0 · mean 0.06 | max 25.2 · p99.9 5.0 · mean 0.12 | max 19.5 · **p99.9 15.9** · mean 0.26 | max 0.4 · mean 0.03 |
| **129³** (12.3 MB fp16) | max 2.3 · p99.9 0.4 · mean 0.02 | max 20.1 · p99.9 2.2 · mean 0.03 | max 15.5 · **p99.9 12.4** · mean 0.13 | max 0.2 · mean 0.01 |

Bake time for 65³ is 0.13 s, so LUT size costs nothing at build time.

**Read the mean column and the saturated column differently.**

- On everything a normal photographic frame contains, 65³ is excellent: mean
  error 0.03–0.26 code values, p99.9 at or below 5. The plan's claim that this is
  "indistinguishable at preview resolution" holds.
- On **fully saturated, far-out-of-gamut colours** it does not hold, and — the
  important part — **it barely improves with LUT size**. Quadrupling the table
  from 33³ to 129³ (60× the bytes) only moves the p99.9 from 22.8 to 12.4 code
  values. That is not an interpolation-resolution problem; it is that ACES 2.0's
  gamut compression is genuinely near-discontinuous out there, and no practical
  3D table resolves it.

This affects synthetic and CG content (pure-primary emissives, saturated UI
elements, some render passes) far more than photographed footage. It is also
precisely what the `openexr-images` "out-of-gamut colours" set exercises, which
the plan calls out in §9. There is now a fixture for it:
`Tests/Fixtures/corpus/out-of-gamut.exr`.

For honesty: this limitation is not special to us. Any application applying an
ACES view transform through a baked 3D LUT has it. Applications that avoid it
evaluate the transform analytically, usually on the GPU.

---

## 5. The options

### Option A — Bake 65³ from OCIO 2.5.x built-in ACES 2.0 config
*(this is D3 as currently written)*

- Runtime: log shaper + tetrahedral lookup. Nanoseconds per pixel, no library.
- Bundle: 1.57 MB fp16 for the default, plus ~0.21 MB per 33³ alternate.
- Reproducible: pin the OCIO version and the config name; hash the output.
- **Accepts** ~16/255 p99.9 error on saturated out-of-gamut colour.

### Option B — Bake, but larger (129³)
- 12.3 MB fp16 for the default view alone. Plan §11 already flags appex bundle
  size as a thing to raise.
- Buys very little where it matters. **Poor trade; listed for completeness.**

### Option C — Implement ACES 2.0 analytically in EXRCore
- Exact. No interpolation error anywhere, including saturated colour.
- Now plausible on cost, *because decode already downsamples*: ~74 ms at
  thumbnail size, ~535 ms at preview size.
- Cost: Hellwig 2022 CAM forward and inverse, tonescale, chroma compression,
  gamut compression with 360-entry cusp tables. Realistically several hundred
  lines of exacting colour code, and the failure mode is silent wrongness.
- The reference is `aces-aswf/aces-core` v2.0 (CTL) and OCIO's C++ fixed-function
  implementation, which per the changelog agree with each other.

### Option D — Ship the LUT, keep an analytic oracle for tests
- Runtime is Option A.
- Build/test time additionally evaluates through OCIO to produce golden images
  (§9 asks for golden-image regression anyway).
- The LUT's error stops being an unknown: it is measured on every CI run, and a
  regression that pushes it past a threshold fails the build.

---

## 6. Recommendation

**Option A + D.** Keep D3 as written, source the bake from OCIO 2.5.x's built-in
`cg-config-v4.0.0_aces-v2.0_ocio-v2.5`, and add the OCIO-based golden-image check
so the interpolation error is continuously measured rather than assumed.

Reasoning:

- OCIO is a **build-time-only** dependency, so D4 is satisfied and OCIO's own
  CVE history (2.5.2 fixes CVE-2026-42450) never enters our shipped attack
  surface. That is a real advantage over vendoring colour code we maintain.
- aces-core 2.0 explicitly aligned with OCIO 2.4.2, so "use OCIO" and "use the
  reference" are the same answer, not a compromise.
- Option C's risk is asymmetric. Interpolation error is bounded, measurable and
  documented. A subtle CAM bug is unbounded and invisible.

If you want the saturated-colour case exact, the sane path is to ship A now and
add C later as an opt-in high-quality path for the preview extension only, with
the LUT retained for thumbnails. That preserves D7 by making the divergence a
deliberate, badged choice rather than an accident.

**What I need from you:** which option, and whether the default view should be
`ACES 2.0 - SDR 100 nits (P3 D65)` (matches D5) or something else.

---

## 7. Related: ICC profile for tagging the output

D5 says render to Display P3 and *tag* the output. Two ways:

1. **Ask macOS.** `CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3)` gives the
   system profile. Zero bytes shipped, zero dependencies, guaranteed to match
   what ColorSync expects. This is the obvious default for the extensions.
2. **Embed a known profile.** [Elle Stone's ICC profiles][elle] are the
   well-regarded reference set here — carefully documented, with explicitly
   stated primaries and transfer functions.

Option 1 is right for the shipped appex. Option 2 is genuinely useful for
**golden-image tests and the CLI**, where "whatever this machine's ColorSync
says" is exactly the wrong property — a golden image needs a profile that is
identical on every machine and in CI. Worth adopting for `EXRCLI --png` and the
§9 golden corpus.

**Verified 2026-09-06 — and the answer is no.** Elle Stone's repository splits
its licensing:

| Part | Licence |
|---|---|
| The ICC profiles | **CC-BY-SA 3.0** — attribution *and* share-alike |
| The profile-making code | **GPLv2** |

Neither is usable here. CLAUDE.md forbids copying from GPL sources outright, so
the generator is off-limits the same way `QLVideo` is. The profiles are not GPL,
but CC-BY-SA's share-alike sits badly with D10's BSD-3/MIT intent, and shipping
a copyleft data file inside a permissive project invites exactly the licensing
argument this project does not need.

**Use OCIO instead.** OpenColorIO is BSD-3-Clause — the same licence family as
OpenEXR, which we already vendor — and `ociobakelut --format icc` emits an ICC
profile from the very config we bake the LUTs from. That gives golden images a
profile that is deterministic, reproducible in CI, generated from a pinned
source, and licence-compatible. Elle Stone's profiles remain a good *reference*
to check our numbers against; they should not be committed.

[acesdocs]: https://docs.acescentral.com/system-components/output-transforms/technical-details/rendering-overview/
[changelog]: https://github.com/aces-aswf/aces-core/blob/main/CHANGELOG.md
[ocio24]: https://opencolorio.readthedocs.io/en/latest/releases/ocio_2_4.html
[ocio25]: https://opencolorio.readthedocs.io/en/latest/releases/ocio_2_5.html
[ociorel]: https://github.com/AcademySoftwareFoundation/OpenColorIO/releases
[elle]: https://github.com/ellelstone/elles_icc_profiles


---

## 8. Multi-display and external monitors

Raised when Option A was chosen: *P3 is fine as a reference that gets
transformed to the specific screen, but is it fine for all screens as-is?*

It is a fair question, because the two paths are not equivalent:

| | What happens to out-of-gamut colour |
|---|---|
| **Path A** — our design: render ACES→P3, tag P3, let ColorSync convert | ACES gamut-compresses **into P3** inside the appearance model, then ColorSync applies a **colorimetric clip** P3→display |
| **Path B** — render ACES→Rec.709 natively for an sRGB display | ACES gamut-compresses **directly into Rec.709**, once, inside the appearance model |

Path B is the better-designed operation. The question is how much it matters.

### Measured, on real files rather than synthetic colour

Difference between Path A and Path B, in 8-bit sRGB code values, 40 000 pixels
sampled from each file after decode and conversion to AP1:

| Source | mean | p99 | max | pixels > 2/255 | pixels clipping |
|---|---|---|---|---|---|
| `rawtoaces` 6K, ACES2065-1, photographed | **0.11** | 3.04 | 34.1 | **4.1 %** | 2.7 % |
| A saturated test frame, no chromaticities | **2.43** | 19.8 | 55.1 | **25.8 %** | 24.4 % |

97.7 % of the photographed frame is already inside sRGB after the P3 render;
only 75.6 % of the saturated frame is.

**Methodology note, because it changed the answer.** Measured first on uniformly
random AP1 triples, Path A and Path B differed on 34 % of "photographic" pixels.
That was an artefact: a uniform random cube in AP1 is far more saturated than
real imagery. Sampling actual decoded pixels dropped it to 4.1 %. The synthetic
numbers are not reported here because they are misleading.

### Conclusion

For photographed content the P3-reference design is fine as-is — a tenth of a
code value on average. For saturated or synthetic content it is visibly
different, and Path B would be better.

### The constraint that settles it

**Thumbnails cannot be display-aware.** Quick Look caches thumbnails in a
shared, display-independent cache; the same cached image is shown whichever
screen the Finder window is on, and it survives plugging a monitor in. So the
thumbnail must render to one fixed reference space regardless. Display P3 is the
right choice: it covers modern Apple displays natively, and ColorSync handles
the rest.

Given that, making the *preview* display-aware would break D7 (thumbnail and
preview pixel-identical by default) for no benefit the user asked for, and would
change appearance when a window is dragged between screens — the "silent
divergence reads as a bug" failure D7 exists to prevent.

### Recommended policy

1. **Thumbnail: always `ACES 2.0 - SDR 100 nits (P3 D65)`, tagged Display P3.**
   Not negotiable; the cache is display-independent.
2. **Preview: the same by default.** Preserves D7.
3. **The escape hatch already exists in the plan.** §8's overlay specifies a
   *View transform* control offering ACES 2.0 → sRGB, → Display P3, → Rec.709
   and raw. Someone working on a wide-gamut reference monitor, or on a plain
   sRGB panel who cares about saturated CG, picks the matching view. Because it
   is user-set it falls squarely under D7's badged-divergence exception.
4. **Ship the Rec.709 alternate as a 33³ LUT** — already planned in §8, and §4's
   error table shows 33³ is adequate for an alternate.

### External display cases

| Display | Behaviour under Path A |
|---|---|
| Apple built-in, Studio Display, XDR | P3 native. Near-identity conversion. Correct. |
| Generic sRGB external monitor | Colorimetric clip. Fine for photography (§8 table), visibly different for saturated CG. Overlay offers Rec.709. |
| Wide-gamut / calibrated reference monitor | Converted by ColorSync from a correctly tagged P3 source. Correct. |
| HDR display | We emit SDR 100 nits and tag it SDR, so it is shown as SDR. Conservative and correct. ACES 2.0 HDR views exist (`HDR 1000 nits (P3 D65)`) if HDR output is ever wanted — that is a scope change, not a default. |

### Open question for later

Whether the chosen view transform should become a **committed preference**
rather than only a transient overlay setting. "Which display do I work on" is a
stable property of a person's setup, not a per-file decision, so re-picking it
every session would be irritating. D9 currently allows exactly one committed
preference, so adding a second needs explicit sign-off. **Not doing it without
that.**

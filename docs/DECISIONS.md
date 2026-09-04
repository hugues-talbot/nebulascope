# NebulaScope — Design Decisions

Why things are the way they are. Newest entries at the bottom; don't delete
entries when circumstances change — strike through and note the successor.
(Companion to [ARCHITECTURE.md](ARCHITECTURE.md); this file records the *why*,
that one the *what*.)

## Core model

- **Everything promotes to Float32 on load.** One pipeline code path, NaN as
  the universal blank. Memory cost accepted (2× for 16-bit data) for the
  simplicity; original bit depth is recorded for save-time choices.
- **Display never touches data.** All stretch/adjust/colormap work happens in
  a render-only pipeline (StretchModel → DisplayRenderer). "Save Stretched
  As" is an explicit, separate bake.
- **Per-image state is keyed by path** (`m_*ByPath` maps in MainWindow), not
  per view. Views are dumb displays; a revision counter (`m_xformRev`)
  invalidates a cell's stashed pixels when the image's orientation changed
  elsewhere. Decision made after the "black border" bug family (see below).

## Histogram / stretch

- **Linear stretch = the window; nonlinear modes live inside it.** Log,
  Asinh, GHS all compose with the linear window and get the full widget
  width for their controls. Rationale: astronomical dynamic range makes the
  useful window a sliver of the data range — controls must operate on the
  window, not the range. *Refined (v0.96): the plot's axis is a zoomable
  view, not a constraint.* The fit-to-data/window remains the default
  framing, but handles live on a domain one full span beyond the data on
  each side (black below min, white above max, GHS SP/LP/HP outside the
  window) — the renderer's affine windowing was always well-defined there,
  and the GHS curve stays a valid monotone transfer since its slope
  function is positive everywhere. Motivation: the Siril GHS workflow
  (symmetry point on the histogram mode) was structurally impossible when
  the black point had clipped the mode out of the window.
- **RGB histograms share one pooled axis by default.** Per-channel
  normalisation (each channel over its own [min,max]) silently equalises
  the channels in the plot and hides exactly the offsets colour alignment
  needs to see. Common axis is the honest default; switching is
  display-invariant (handles are re-expressed on the new ranges). One
  consumer must NOT be pooled: the DisplayFunction import's far-white
  rebase reads t = 1 as the channel's own data maximum, so it runs on
  per-channel ranges and the pooling happens afterwards.
- **GHS follows the PixInsight formulation** (exponential-response D, focus
  b, SP symmetry point, LP/HP protection), windowed like Log/Asinh.
- **LUTs hold only the curve shape; windowing is per-pixel float — and every
  LUT consumer INTERPOLATES.** 4096-entry LUTs sampled over the *windowed*
  coordinate. Re-windowing inside the LUT posterizes narrow windows — this
  was rediscovered painfully twice; don't "optimize" it back. Third
  rediscovery (v0.96): the combine's "As displayed" bake read the LUT with
  NEAREST lookup while the display interpolated — through a steep GHS
  (slope ~100 around SP-on-the-mode) adjacent entries differ by slope/4096,
  so the baked DATA posterized to ~40 levels while the screen looked
  smooth. A steep transfer turns input quantization into output steps;
  interpolate, always, everywhere the LUT is read.
- **First view of an image = plain min→max linear ramp** (like Reset). The
  earlier percentile "boost" guessed wrong too often; Auto STF / Auto Linked
  are the explicit boosted options.
- **Post-stretch adjustments** (brightness/contrast/gamma/shadows/highlights/
  BP/WP + temp/tint/hue/sat/vibrance) apply to the stretched [0,1] values:
  tone ops compose into the LUTs (free), colour ops run per pixel only when
  non-identity. They are per-image state, persisted in annotation sidecars.

## Geometry

- **Orientation is a replayable history** (`m_xformByPath`), canonicalized:
  arbitrary rotations commute to the tail (angle negates across mirrors) and
  merge into at most ONE trailing rotation. Consequence: net-zero rotations
  restore exact original dimensions; expansion borders cannot accumulate.
  (Supersedes the earlier "record ops verbatim, never merge" rule, which
  caused permanent black borders.)
- **Arbitrary rotation is absolute**: always ONE resample from the pristine
  base, never rotation-of-rotation.
- **Every geometric op feeds four consumers**: pixels, annotations, WCS,
  view-link calibrations. Adding a new op means updating all four.

## Formats / interop

- **XISF float output is normalized to [0,1]** (NSSCALE/NSZERO keywords keep
  the inverse map): PixInsight assumes the [0,1] bounds convention and
  renders out-of-range floats as noise.
- ~~**XISF data blocks are written uncompressed by default.** The older gitea
  libXISF's compressed (shuffled) blocks decode as structured noise in
  PixInsight. Re-enable LZ4 only after a PI round-trip test with the
  installed libXISF.~~ *Superseded (v0.86): the "noise" was the
  un-normalized-float bug all along — libXISF ≥ 0.2.13 compressed output was
  verified spec-conformant and PI-round-tripped. Blocks are now compressed
  (byte-shuffled) with a save-time Zstd/Zlib/Uncompressed choice; Zstd falls
  back to Zlib where libXISF lacks it.*
- **FITS multi-HDU**: list entries expand per image HDU; "no 2-D image in
  primary HDU" is handled by scanning extensions.
- **Sidecars** (`<image>_annotation.json`) carry annotations + orientation +
  display adjustments. Manual *Load Annotations* re-maps shapes onto the
  current view (ignores the file's orientation as a view instruction);
  auto-load applies it.

## Tools

- **Colour transport (OT)** runs on the "as displayed" renditions, estimates
  distributions only from each view's *visible* pixels, excludes saturated
  pixels (≥0.98), and — for rotated sources — runs in the disk frame and
  adopts the orientation history afterwards (never bakes borders).
- **Star recomposition = screen blend** `1−(1−a)(1−k·b)` on displayed
  values; separate small dialog (Combine Stars), not a mode of Combine
  Channels (that dialog is mono-plane/role based).
- **Combine Channels "As displayed" prenorm** merges channels through their
  current view stretches — the user's prepared renditions are the input.

## UI / performance

- **Rendering is async and coalescing** (QtConcurrent): sliders never block;
  intermediate states are skipped. A frame is shown only if its source
  pixels are still current (pointer+size identity check). ImageData copies
  are DEEP — capture identity *before* copying.
- **Overlay panels are opaque by default**: any translucency defeats Qt's
  repaint clipping and slows zoom/pan. Opacity is a preference (50–100%).
- **Adjust sliders: click first, then wheel** — hover-wheel must not silently
  edit the display.
- **256→4096-level LUTs** killed visible banding; dithered 8-bit output
  handles the rest.
- **The display block is a specified, reference-implemented format.**
  The sidecar's `display` key is not private state: it has a schema
  version, names instead of enum integers, a written spec (TRANSPORT §6)
  mapping every field to an equation, and a standalone NumPy renderer
  (tools/render_sidecar.py) that CI checks against DisplayRenderer::
  renderFloat to a few float32 ULPs per pixel (tests/conformance). The
  point is scientific: a stretch becomes explainable and reproducible in
  other software, and a transport fit becomes a diffable object. Rule:
  any change to the transfer/adjustment math updates spec, reference,
  and schema version together — the conformance test is the tripwire.
- **Values in All Views = the link transforms made visible.** The per-cell
  readout + crosshair (V) is not new machinery: it fans the hovered pixel
  through the very `world` transforms calibrated linking navigates with, so
  the crosshair shows where the software BELIEVES the correspondence lies.
  That turns calibrated linking from a navigation convenience into a
  measurement instrument with a visible error — hover a star, see whether
  the other view's crosshair lands on it. Principle worth reusing: when a
  hidden state drives behaviour (a transform, a fit, a calibration), the
  cheapest high-value feature is usually to render that state, not to add
  a new one. (Prior art — DS9's crosshair lock, medical viewers — assumes
  a shared WCS or identical geometry; this works on unsolved frames of
  different scale/rotation aligned by eye, which is the comparison case
  that actually occurs in the field.)
- **Save dialogs: native on macOS via NSSavePanel accessory views.** Qt can
  host inline option rows only in its own non-native dialog — which is why
  the rich save dialogs were Qt-drawn at first. The platform's intended
  design for save-with-options is the accessory view (cf. Preview's export
  panel): MacSavePanel.mm reproduces the three behaviours that forced
  non-native (inline format-reactive options, every image type clickable,
  base-name-only adoption) with native controls, and enforces the
  chosen-format suffix in the delegate at OK time (no "name.xisf.png").
  Other platforms keep the Qt dialog: Windows' IFileDialogCustomize is not
  exposed by Qt.
- **Batch row removal runs signal-blocked, one display fix-up at the end.**
  Removing list rows one by one has a hidden cascade: whenever the row
  being deleted is the *current* one, Qt promotes its neighbour to current,
  and an unblocked `currentRowChanged` then synchronously decodes, stretches
  and renders that image — which is itself deleted next, promoting another.
  One full decode per closed row. The trap arms exactly when the current row
  sits at the top of the removal order, which is where a blink session
  leaves it (Space wraps to the top after the last image), so "close all"
  after culling hit it every time. Measured on 300 × 4 MB FITS blinked
  through then closed: +4.4 s with the cascade, +0.3 s with removals
  signal-blocked and a single explicit re-display at the end — and the cost
  scales with image size (tens of seconds for a session of full-size subs).
  Rule of thumb: any loop that mutates list rows must hold a
  `QSignalBlocker` and reconcile the display once, after the loop.

## Build / release

- **CI**: GitHub Actions build macOS (official Qt + macdeployqt), Linux
  (system Qt), Windows (vcpkg + FindCFITSIO stub + libXISF from gitea).
  All three jobs need the Qt image-formats module (TIFF/WebP plugins live
  there, not in qtbase). Tags `v*` publish a Release (zip ×3 + .deb).
- **Tests are framework-free CTest binaries** (`tests/nstest.h`) plus a
  scripted smoke run of the real app (`tests/smoke.nsc`, offscreen QPA) —
  the CI gate since v0.85. Failing ctest output is published to the job
  summary so failures are diagnosable from the run page. Test fixtures in
  `tests/testdata/` are explicitly exempted from the data-file gitignore
  rules (a `*.fits` ignore once silently swallowed the smoke fixture).
- **`src/app/AppInfo.h` is user-maintained** (version/copyright/About) — the
  assistant never rewrites it unasked (see CLAUDE.md). Version also lives in
  CMakeLists (CPACK), CITATION.cff and vcpkg.json; bump all four when
  tagging.
- **`-psn` arguments** (Finder) are ignored by the CLI parser.

## The visible region is a quad, not its bounding box (2026-08-30)

A calibrated Match legitimately puts a rotation into a view's navigation —
and `visibleImageRect()`, the bounding box of the rotated viewport quad,
silently became a *superset* of what the user saw. Three consumers
inherited the lie: OT's relevance masks sampled off-screen sky (the field
report: lossless transport of the pillars washed to khaki because the
reference distribution was dominated by surrounding nebulosity — reproduced
exactly by feeding OT a mismatched region), Export Zoomed Region wrote the
unrotated bbox ("not what I see"), and Crop to Visible cropped it. The fix
is one distinction, applied per consumer's needs: distribution *sampling*
(OT, crop) takes the largest rectangle INSIDE the visible quad — a subset
is a fair sample, a superset is contamination — while *export* renders
WYSIWYG through the view transform at ~1 image px per output px. Lesson:
any API returning "the visible region" as a QRect bakes in the assumption
that navigation is axis-aligned; every caller written before rotation
existed inherits that assumption invisibly.

## The hue knob that could never turn (2026-08-30)

Lossless colour transport washed out on STARLESS pairs and worked on starry
ones — the user isolated the variable in the field. The mathematics first:
stars pin all three channels at the bright end, dragging OT's map into the
per-channel family; a starless pair's transport is almost purely a
cross-channel rotation, which per-channel curves cannot express at all. The
colour-adjustment stage exists for exactly that — and it never worked,
because fitColorAdjust swept every field over [-1, 1] while hue is in
DEGREES: the one parameter able to rotate a palette was capped at one
degree. Fixed with per-field domains and a coarse scan before the golden
section (the hue objective is multimodal; descent from zero misses a
distant rotated minimum). Ground-truth unit test: a synthetic 60-degree
rotation must be recovered to <1 degree. Both lossless options now default
on (user call). Lesson: a fit that silently returns "no improvement" for a
whole parameter looks identical to a hard problem — sweep domains deserve
the same per-field scrutiny as the parameters themselves.

## The colour stage is a matrix, not four sliders (2026-08-30)

Even with hue freed, the lossless transport fit kept failing on starless
pairs — this time to grey, the MMSE escape of a SEQUENTIAL fit (curves
first against a rotated target, colour after on the curves' compromise).
Two changes. First, the fit alternates: each round re-fits the per-channel
curves against the colour-INVERTED target (the colour stage is invertible
in closed form), block coordinate descent instead of a one-shot pipeline.
Second, the colour stage itself became a full 3×3 mixer solved by weighted
least squares in closed form — temperature, tint, hue rotation and
saturation are all linear in RGB, so the matrix subsumes the slider family,
and its normal equations have no multimodality to trap a line search. The
display block spec moves to schema 2 (optional adjust.mix; written only
when present, so every existing sidecar and reader is untouched), with the
reference renderer and a conformance case updated in the same commit.
Lesson: when a fit keeps landing on desaturation, the family is too weak —
grey is what least squares does when the right map is outside the model.

## The histogram shows the result; the image waits for your hand (2026-08-31)

Two user calls from field practice with Siril's GHS, landed together
because each makes the other affordable. The histogram's filled shape is
now the OUTPUT distribution — the result of the current stretch — computed
as the measure pushforward of the input bins through the transfer curve:
no image pass, microseconds, so it tracks every drag tick. (Pushforward,
not point-mapping: mapping bin centres combs the plot wherever the curve's
slope exceeds one; each input bin's mass spreads over the output interval
of its edges.) The input histogram stays as a faint dotted outline, because
the grips live on its axis. And because that live feedback is free, the
image render — seconds per frame on a 60-Mpx master — is deferred entirely
until the grip or slider is released: one render, from the final state.
The old async-coalescing pipeline stays for everything non-interactive.
Lesson: when live feedback is demanded of something expensive, look for a
cheap statistic that answers the same question — the user was never
watching individual pixels during a drag; they were watching the
distribution.

## The PSF study comes home (2026-09-01)

The star_fwhm instrument — the linear stellar PSF measurement validated
against PixInsight in the Hubble study — is now Tools > Measure PSF, in
C++ inside astro_core. The user's design call shaped it twice. First, the
language: not a Julia (or any) sidecar runtime, whose "menu items if
available" would mean features existing on some machines and not others —
the numerics are 500 lines of C++ once the app's own detection, WCS and
annotation machinery is counted, and a native feature is testable in
ctest (an exact synthetic-Moffat recovery test, plus a field-recovery
test with ground truth). Second, the reporting: numbers alone do not show
a drift axis, so the report dialog drops ROTATED-ELLIPSE annotations on a
sample of fitted stars — the annotation model already had angleDeg
waiting — making the elongation pattern a visible overlay rather than a
table. One implementation lesson for the ages: a circularly-symmetric
initial guess zeroes the rotation gradient, and an LM solver that treats
the resulting singular system as fatal (rather than damping through it)
fails on every star — the second silently-pinned-parameter bug of this
project, found the same way (ground truth or it did not happen).

## Cache the decode, not the bytes (2026-09-01)

The user asked the right question precisely: app-level cache, or trust the
OS? The OS page cache spares only the disk read, and for a compressed
500 MB XISF the read is the cheap half — zlib inflation, byte-unshuffle,
Float32 promotion, debayer and statistics dominate, and they repeat on
every list switch. The decoded-image LRU (core/ImageCache) keeps the
POST-DEBAYER, DISK-FRAME decode plus its statistics; rotation replay and
per-image stretch memory sit on top exactly as on a fresh read, so cache
hits are semantically invisible. Correctness over speed at the boundary:
every hit re-checks the file's mtime+size (an external overwrite is never
masked), the auto-reload watcher evicts eagerly, and debayer changes —
per-image or global-algorithm — invalidate, because entries are
post-debayer. Measured: reopening a 495 MB master costs 0.3 s against a
13 s cold decode. The budget is a preference (default 4096 MB, 0 = off) —
the eviction boundary is honest and visible, unlike the page cache's.

## Starless deconvolution: kernel from the sibling, audit by proxy (2026-09-04)

Every star-generated artefact the deconvolution ever produced — the
square frames, the ringing moats, the wavelet halos — was the filter's
response to a source no Moffat describes: a clipped core, a contrast a
million times the sky. The protections built against them (round core
protection, star-neutral prior zones, apodized kernel) treat symptoms.
The principled cure is the user's own earlier design: apply the filter to
the STARLESS image. The blur model y = k * (n + s) is linear, so the
starless product obeys it minus the one component that violated it;
deconvolving it with the kernel measured on its starry sibling is valid
by linearity, and nebular contrast rings below the noise floor — no
protection, no neutral zones, the prior wanted everywhere. Two
commitments keep it honest, both enforced in code: the delivered PSF is
audited BY PROXY (the same filter on the starry sibling, its stars
re-fitted — exact for the pure filter by linearity, which a ctest asserts
as deconv(n + s) = deconv(n) + deconv(s); approximate under RED), and the
header states the chain (kernel source, proxy figure, and that the star
removal upstream is not part of the stated model). UI shape per the
user: a "Kernel from" selector INSIDE the existing dialog, not a new
menu; the sibling is measured on demand and the dialog returns with the
kernel shown; script `deconv … from <row>`. Live on a 2048-px crop of
the M16 master with its StarXTerminator sibling: delivered by proxy
1.96/1.92/1.95″ on a 1.90″ declaration (the starry control: 1.95/1.92/
1.95″), and at the twelve brightest star sites the annulus minima of the
starless product sit where the input's were (−0.3 to −3.9 noise sigmas)
while the starry control carries the physical moats (−7.6 and −12.7 σ at
the two saturated stars). One trap found on the way, now a guard: the
rc-astro CLI writes its FITS vertically flipped, and a flipped sibling
would have handed the filter a mirrored kernel — the starry-minus-
starless residual is the stars-only image, never strongly negative, so a
percent-level fraction of strongly negative pixels refuses the source.

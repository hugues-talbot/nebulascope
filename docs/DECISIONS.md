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

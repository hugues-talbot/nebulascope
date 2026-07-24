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
  window, not the range.
- **GHS follows the PixInsight formulation** (exponential-response D, focus
  b, SP symmetry point, LP/HP protection), windowed like Log/Asinh.
- **LUTs hold only the curve shape; windowing is per-pixel float.**
  4096-entry LUTs sampled over the *windowed* coordinate. Re-windowing inside
  the LUT posterizes narrow windows — this was rediscovered painfully twice;
  don't "optimize" it back.
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
- **XISF data blocks are written uncompressed by default.** The older gitea
  libXISF's compressed (shuffled) blocks decode as structured noise in
  PixInsight. Re-enable LZ4 only after a PI round-trip test with the
  installed libXISF.
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

## Build / release

- **CI**: GitHub Actions build macOS (official Qt + macdeployqt), Linux
  (system Qt), Windows (vcpkg + FindCFITSIO stub + libXISF from gitea).
  Tags `v*` publish a Release (zip ×3 + .deb).
- **`src/app/AppInfo.h` is user-maintained** (version/copyright/About) — the
  assistant never rewrites it unasked (see CLAUDE.md). Version also lives in
  CMakeLists (CPACK) and CITATION.cff; bump all three when tagging.
- **`-psn` arguments** (Finder) are ignored by the CLI parser.

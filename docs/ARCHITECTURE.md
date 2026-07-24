# NebulaScope — Architecture

Developer-facing map of the codebase (v0.83). The user-facing feature guide is
[MANUAL.md](MANUAL.md); design rationale lives in [DECISIONS.md](DECISIONS.md).

## Layers

```
core/    pure C++ (no widgets): pixel model, math, geometry
io/      format backends behind abstract reader/writer registries
render/  display state + rasterization (QtGui only)
ui/      Qt Widgets application
app/     entry point, theme, resources, user-maintained AppInfo.h
```

### core/
- **ImageData** — planar pixel container, native sample formats
  (UInt8…Float64); the loader promotes everything to Float32
  (`Convert`), so downstream code assumes one type. NaN = blank.
- **ImageHeader** — unified metadata (FITS cards + XISF properties).
- **ImageStats** — sampled min/max/median/MAD per channel (auto-stretch,
  normalized stretch-paste anchors).
- **Stretch** — transfer shapes: Linear+MTF midtone, Log, Asinh, and GHS
  (slope-integral formulation with SP/LP/HP). `buildLut()` returns the shape
  over the *windowed* coordinate t∈[0,1]; `windowCoord()` does the black/white
  windowing per pixel in float. This split is what prevents posterization for
  narrow windows — never re-window inside the LUT.
- **Colormap** — anchor-interpolated maps + composable `inv()` / `split(t)`
  modifiers applied at LUT build time.
- **Adjustments** — post-stretch display adjustments (header-only):
  `applyTone()` (monotone per-channel curve, composed into render LUTs) and
  `applyColor()` (WB/hue/sat/vibrance, per pixel when non-identity).
- **ColorTransport** — sliced optimal transport between displayed RGB
  distributions (ROI-restricted sampling, saturation cut).
- **ChannelCombine** — weighted mono→RGB merge engine (Qt-free).
- **Transform** — lossless 90°/flip ops; `rotateArbitrary()` (bilinear,
  bbox-expanded canvas, NaN corners, weight-renormalized sampling).
- **Wcs** — TAN projection from FITS keywords or PCL XISF properties;
  `transformed()` / `rotated()` rebase CRPIX + CD through any pixel remap.

### io/
Abstract `ImageReader`/`ImageWriter` registries (`readerForFile()` by magic +
extension). Backends: FITS (CCfits/CFITSIO; multi-HDU enumeration, tile
compression, BSCALE/BZERO), XISF (libXISF; properties incl. astrometric
solution), Qt image formats (JPEG/PNG/TIFF/WebP read; 16-bit TIFF write).

### render/
- **StretchModel** — the single source of truth for display params (function,
  per-channel window points, GHS, colormap, adjustments); emits `changed()`.
  Snapshots as `StretchModel::State` power per-image stretch memory and
  copy/paste.
- **DisplayRenderer** — ImageData + StretchModel → dithered 8-bit QImage
  through per-channel windowed LUTs (GHS shares one master curve); tone
  adjustments compose into the LUTs, colour adjustments run per pixel.
  `renderFloat()` is the same composition to Float32 planes (exports, OT,
  star screening). MainWindow drives it **asynchronously** (QtConcurrent):
  coalescing — a running render defers, the newest state renders next; a
  finished frame is shown only if its source pixels are still current.

### ui/
- **MainWindow** — owns the *active* image state (`m_image`, `m_currentPath`,
  `m_wcs`, `m_header`, stats) and all per-path maps: stretch memory
  (`m_stfByPath`), annotations (`m_annByPath`), orientation history
  (`m_xformByPath`), disk dimensions (`m_diskSizeByPath`).
- **ViewGrid / ViewCell** — the split main view (≤5×5). One active cell;
  activation swaps the whole current-image state in/out of the cell
  (`onCellSwap`), so every per-path mechanism works unchanged. Inactive cells
  keep their decoded image + rendered pixmap.
- **ImageView** — QGraphicsView canvas: rubber-band zoom, pans, wheel
  (Shift=fine), pixel hover, drawing tools, `viewNavigated` /
  `adoptNavigationCalibrated` for linked navigation.
- **AnnotationLayer** — vector overlay + RA/Dec grid; rebuilds from plain
  `Annotation` data. Ellipse rotation uses explicit QTransforms
  (QTBUG-22335: `setRotation` is unreliable inside item groups).
- **HistogramPanel/View, ColorBar, InfoPanel, CombineDialog,
  StarCombineDialog, RotateDialog, PreferencesDialog** — all drive/read the
  shared StretchModel or MainWindow state; no widget owns pixel data.

## Key invariants

1. **The orientation history is canonical.** `m_xformByPath[path]` replayed
   from the disk image reproduces the displayed pixels, and
   `canonicalXforms()` keeps it minimal: arbitrary rotations commute to the
   tail (angle negates across mirrors) and merge into ≤ 1 net rotation, so
   net-zero histories restore the exact original canvas and expansion
   borders never stack. `normalizeOrientation()` re-derives pixels whenever
   canonicalization shortens the history. Cells stash pixels under a
   per-path revision (`m_xformRev`); stale stashes re-derive on activation.
2. **One geometry map per operation.** Every pixel remap (90°, flip,
   arbitrary rotation, base-restore) applies the same forward affine to
   pixels, annotations, WCS (CD·J), and view-link calibrations
   (`W ← F⁻¹·W`). If you add a geometric op, feed all four consumers.
3. **Arbitrary rotation is absolute.** `rotateToAngle()` restores a stashed
   pristine base and applies ONE resample; undo/redo is an angle pair, not a
   snapshot. Annotations travel via exact inverse affines (vector data).
4. **Windowing is per-pixel float; LUTs hold only the shape** (see Stretch).
5. **Display is disposable.** Nothing downstream of DisplayRenderer feeds
   back into data; Save Data As writes `m_image` (post-orientation), exports
   write the rendered QImage.
6. **Linked navigation** — the shared-frame condition
   `W_dst⁻¹·V_dst == W_src⁻¹·V_src` (Qt left-first composition); same-size
   auto-links are the identity-world special case.

## Undo model

QUndoStack commands guard on `currentPath()` and mark themselves obsolete if
their image is no longer active. Annotation edits snapshot before/after
vectors; 90°/flips apply inverses; arbitrary rotation stores angle pairs.

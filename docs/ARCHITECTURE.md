# NebulaScope — Architecture

Developer-facing map of the codebase (v0.98). The user-facing feature guide is
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
  Parity and orientation are read from the solution: `mirroredOnScreen()`
  (sign of det CD, for the row-1-at-top display), `northUpDelta()`, and
  `skyOrientPlan()` — the flip and quarter turns (plus an optional
  residual rotation) that put north up and east left.
- **SexCatalog** — SExtractor ASCII_HEAD parser; name-keyed column access,
  order-independent (feeds the annotation importer).
- **Ds9Regions** — SAOImage DS9 `.reg` parser. Produces plain `Ds9Shape`
  records in image pixels; sky regions are laid out on the sphere (exact
  destination points) and carried through the Wcs vertex by vertex, so
  rotation and parity need no special case. Counts what it skips and why.
- **Debayer** — RCD / bilinear / superpixel demosaic; pattern from the
  header, forced, or sniffed from pixel statistics.
- **PsfMeasure** — elliptical Moffat fits over isolated stars; per-channel
  FWHM, eccentricity, field map.
- **Deconvolve** — deconvolution to a declared target PSF (MCS
  single-filter transform, optional starlet-RED prior, contract-first λ
  ladder); kernel measured on the image or taken from a starry sibling.
- **StarRemove** — analytic star removal, the first half of the MCS
  decomposition: each star is fitted as an elliptical Moffat and
  subtracted, brightest first, on the running residual.
- **GaiaQuery** — asynchronous Gaia DR3 cone searches against the ESA
  archive (`GaiaClient`, the one core module that uses the network), with
  proper motion propagated to the observation epoch.
- **ImageCache** — LRU of decoded images + statistics + header, keyed by
  list entry, budgeted in bytes, never served stale (file time and size
  checked on every hit).
- **Preferences** — QSettings-backed app options (recent lists, shortcuts
  location, UI defaults). Options that have a command-line form carry a
  per-run override next to the saved field (`runSkyOrient`,
  `runDs9RightDrag`) and are read through `effective…()` getters, so an
  override is never written back by a later save.

### io/
Abstract `ImageReader`/`ImageWriter` registries (`readerForFile()` by magic +
extension). Backends: FITS (CCfits/CFITSIO; multi-HDU enumeration, tile
compression, BSCALE/BZERO), XISF (libXISF; properties incl. astrometric
solution), Qt image formats (JPEG/PNG/TIFF/WebP read; 16-bit TIFF write).
Write path: FITS/XISF/16-bit TIFF. The XISF writer normalizes float data to
[0,1] (NSSCALE/NSZERO keywords keep the inverse map — PixInsight's bounds
convention) and compresses data blocks per `SaveOptions` (Zstd/Zlib/LZ4,
byte-shuffled; Zstd falls back to Zlib where libXISF was built without it).

**Threading rule for FITS.** CFITSIO is thread-safe only when built
reentrant, and the Windows (vcpkg) build is not. Every entry into
CFITSIO/CCfits — `FitsReader::load`, `listFitsImageHdus`,
`FitsWriter::save` — holds `io::fitsMutex()`. New code that calls the
library must do the same. The interface thread avoids waiting on that
lock: the multi-HDU probe stands aside while a worker decode is in flight.

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
  During an interactive display drag (contrast/bias) only the **visible
  region** is rendered, point-sampled at about twice screen resolution and
  shown as a patch over the last full frame; the full frame is rendered
  once on release. State serials keep a stale patch from covering a newer
  full frame.

### ui/
- **MainWindow** — owns the *active* image state (`m_image`, `m_currentPath`,
  `m_wcs`, `m_header`, stats) and all per-path maps: stretch memory
  (`m_stfByPath`), annotations (`m_annByPath`), orientation history
  (`m_xformByPath`), disk dimensions (`m_diskSizeByPath`). The decode for
  a list click, a blink or an open runs on a worker (`displayPathAsync` →
  `decodeEntry` → cache → `displayPath`); the previous image stays live
  meanwhile, the latest request wins, and neighbours are prefetched the
  same way. The SIMBAD server probe (main site, then the official
  mirrors, the choice kept for the session) and the Stellarium remote
  control live here.
- **ViewGrid / ViewCell** — the split main view (≤5×5). One active cell;
  activation swaps the whole current-image state in/out of the cell
  (`onCellSwap`), so every per-path mechanism works unchanged. Inactive cells
  keep their decoded image + rendered pixmap.
- **ImageView** — QGraphicsView canvas: rubber-band zoom, pans, wheel
  (Shift=fine), pixel hover, drawing tools, `viewNavigated` /
  `adoptNavigationCalibrated` for linked navigation; the DS9-style
  contrast/bias drag (it only reports pointer fractions — the mapping to
  Brightness/Contrast is in core `Adjustments`); the preview-patch overlay.
- **AnnotationLayer** — vector overlay + RA/Dec grid; rebuilds from plain
  `Annotation` data (ellipse, box, line, text). Ellipse and box rotation
  use explicit QTransforms (QTBUG-22335: `setRotation` is unreliable
  inside item groups).
- **HistogramPanel/View, ColorBar, InfoPanel, CombineDialog,
  StarCombineDialog, RotateDialog, PreferencesDialog** — all drive/read the
  shared StretchModel or MainWindow state; no widget owns pixel data.
- **ScriptRunner** — `--run <script>` batch mode: one command per line
  (open/stretch/transform/save/assert/screenshot/…), exit code = failed
  assertions. Drives the real MainWindow; powers the CI smoke test. Each
  command carries its own help (`--run list`, `--help <command>`). Under
  `--run`, `main()` redirects QSettings to a private temporary directory,
  so a script neither reads nor writes the user's settings.

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
7. **The first row of a file is drawn at the top.** Pixel index (x, y) is
   FITS (X−1, Y−1) with no flip — the convention of PixInsight and of
   capture software, not of DS9. On screen a frame is sky-true iff
   det(CD) > 0, so a standard FITS frame appears mirrored. Annotation
   angles are directions in pixel axes; DS9 sky angles are converted at
   import.
8. **Sky orientation is seeded, not decoded.** Opening "as the sky" (the
   preference, `--orient`, or Orient to Sky reaching the other list rows)
   appends `Wcs::skyOrientPlan()` to the image's orientation history at
   the first `displayPath` — the same record a manual flip leaves. The
   decode cache stays the file as stored, and invariants 1–2 carry
   everything else.
9. **A new behaviour is an option; the old one stays the default.**
   Behaviour borrowed from another tool arrives as a preference (off by
   default) with a per-run command-line override; see Preferences above.

## Undo model

QUndoStack commands guard on `currentPath()` and mark themselves obsolete if
their image is no longer active. Annotation edits snapshot before/after
vectors; 90°/flips apply inverses; arbitrary rotation stores angle pairs.

## Tests

`tests/` holds framework-free CTest units (`nstest.h`: NS_TEST/NS_CHECK,
exit code = failure count) — twelve binaries: core (stretch/adjust/
transform/stats invariants), io (format round-trips incl. XISF
normalization + per-codec compression, the image cache, FITS access from
two threads), wcs (incl. parity and the sky-orientation plan), catalog,
combine (+ colour transport), colormap, debayer, psf, deconv, starremove,
gaia, ds9reg — plus three tests that run the real app headless (offscreen
QPA) via ScriptRunner: `smoke` (`tests/smoke.nsc`), `settings_isolation`
(a scripted run writes no settings file; Unix) and `sidecar_conformance`
(the sidecar's display block, rendered by the NumPy reference in
`tools/render_sidecar.py`, matches the application's own rendering). All
run in the three CI jobs; failing output is published to the job summary.

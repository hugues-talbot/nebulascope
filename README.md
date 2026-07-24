# NebulaScope

A cross-platform **Qt6** desktop tool for **inspecting and annotating
astronomical images**, focused on the thing most tools do poorly: **precise,
draggable RGB-histogram control** — including a Generalised Hyperbolic Stretch
(GHS) for pulling faint galaxy signal out of a high-dynamic-range background
without blowing out cores and stars.

Reads **FITS** (incl. multi-HDU and tile-compressed), PixInsight **XISF**
(including PCL astrometric solutions), and ordinary **JPEG / PNG / TIFF /
WebP**. Everything downstream of loading consumes one `astro::ImageData` model
and never needs to know the source format.

> **Where it fits:** between viewers like SAOImage DS9 (regions, WCS readout,
> but dated stretch ergonomics) and processing suites like PixInsight/Siril
> (powerful stretches, but not fast annotation-centric inspectors). NebulaScope
> combines modern histogram manipulation, blink comparison with per-image
> stretch memory, and an editable, undoable vector annotation layer that can be
> fed from SExtractor catalogs — e.g. for building machine-learning training
> sets.

<!-- TODO: screenshots — suggested set:
     1. Main window: M81 with histogram dock + GHS curve
     2. Annotation layer: SExtractor import over a dense field + RA/Dec grid
     3. Combine Channels dialog (SHO palette)
     docs/img/*.png, referenced here. -->

## Features

- **Formats** — FITS (CFITSIO/CCfits, multi-HDU + compressed) and XISF
  (libXISF), read **and** write; JPEG/PNG/TIFF read (via Qt) + 16-bit TIFF
  write. Integer images are promoted to Float32 on load so the whole pipeline is
  single-type (FITS scaling via BSCALE/BZERO; XISF/picture integers normalized
  to [0,1]); NaN/Inf blanks are handled throughout.
- **Stretch functions** — Linear (windowing, with MTF midtone), Log, Asinh, and
  **GHS** (exponential-response strength D, focus b, draggable symmetry point
  SP, shadow/highlight protection LP/HP). Linear sets the black/white window;
  the nonlinear modes zoom the plot into it so their controls span the full
  widget. Editable numeric fields for every parameter (per-channel 3×3
  Black/Mid/White grid for RGB images). New images open with a plain min→max
  linear ramp; Auto STF / Auto Linked provide the boosted stretch on demand.
- **RGB histogram** — per-channel or linked Black/Mid/White handles, log/linear
  frequency axis toggle, live transfer curve and colorbar legend; one shared
  model keeps the curve and the image in lock-step.
- **False colour** — Gray, Heat, Viridis, Magma, Inferno, Cividis base maps
  with composable **Invert** and threshold **Split** modifiers, plus a
  colorbar legend spanning the display window.
- **Inspection** — drag-rectangle zoom, wheel zoom (Shift = fine), right-drag
  pan, hover pixel readout; **split views up to 5×5** with linked pan/zoom —
  automatic for same-size images, user-calibrated between differing ones
  (links survive rotation); right-click menu with copyable RA/Dec + pixel
  values and **Aladin / SIMBAD lookups**; an Info panel with data range,
  statistics, FITS structure and a searchable header.
- **Astrometry** — WCS from FITS keywords (TAN) *and* PixInsight's
  `PCL:AstrometricSolution` XISF properties; live RA/Dec hover readout,
  copyable coordinates, RA/Dec coordinate grid overlay with labelled lines,
  telescope-pointing fallback for unsolved frames.
- **Annotations** — a vector overlay (never rasterized into the data):
  ellipses, line segments, and text with per-annotation colour and size. Draw
  with toolbar tools, move/resize with grab-handles, double-click to edit,
  copy/paste, full undo/redo. Persisted as JSON sidecars
  (`<image>_annotation.json`, auto-loaded, orientation-aware), and importable
  from **SExtractor** ASCII catalogs (scaled A/B/THETA ellipses, FLAGS
  filtering, CLASS_STAR colouring) — handy for building NN training sets.
- **Channel combination** — up to 7 mono inputs (LRGB + SHO) merged through a
  linear-combination dialog with palette presets (SHO/Hubble, HOO, LRGB…),
  per-channel normalization (including **“As displayed”**: merge each channel
  through its current view stretch), and a live weight-responsive preview;
  results land in the first empty view.
- **Colour transport** — transfer a reference image’s palette onto the
  displayed image via **sliced optimal transport** (iterative distribution
  transfer) in display space, estimated from each view’s visible pixels only;
  no alignment required, result is a new display-ready entry.
- **Sessions** — multi-image list (append / remove / drag-reorder / export +
  import); **Space/Shift+Space** blink between images (looping), with per-image
  stretch memory and zoom held across same-size frames; copy/paste stretches
  between images (normalized or absolute).
- **Export** — *Save Data As* (FITS/XISF/16-bit TIFF), *Save Stretched As*
  (bake the display transfer into Float32 FITS/XISF/TIFF), and *Export View
  As* / *Export Zoomed Region As* (PNG/JPEG/TIFF/WebP, with JPEG quality and
  8/16-bit PNG/TIFF depth options).
- **Layout** — panels float translucently over the image by default (docked
  layout one keypress away), image-only mode (Tab), fullscreen,
  user-configurable shortcuts (`shortcuts.ini`), Preferences dialog (defaults,
  histories, shortcuts), recent images/annotations menus, undo/redo for
  annotations and image transforms.

## Download

Prebuilt binaries for every tagged version are on the
[**Releases page**](https://github.com/hugues-talbot/nebulascope/releases):

- **macOS (Apple Silicon)** — `NebulaScope-macos-arm64.zip`: self-contained
  `.app`. Unsigned, so on first launch right-click ▸ Open (or
  `xattr -cr NebulaScope.app`).
- **Windows (x64)** — `NebulaScope-windows-x64.zip`: unzip anywhere, run
  `nebulascope.exe` (Qt and all DLLs included).
- **Linux (x64)** — `NebulaScope-linux-x64.zip`: run `./run.sh`; needs system
  Qt6 / cfitsio / ccfits (one `apt install`, see the script's comment).
- **Ubuntu/Debian** — `nebulascope_<version>_amd64.deb`:
  `sudo apt install ./nebulascope_*.deb` (dependencies auto-resolved).

Every push to `main` also builds all three platforms (GitHub Actions); the
per-commit artifacts are on the Actions tab.

## Repository layout

```
CMakeLists.txt          # project + dependency discovery
src/
  CMakeLists.txt        # build targets (+ macOS .app bundle)
  core/                 # ImageData, Convert, ImageStats, Stretch (+ GHS), Colormap
  io/                   # ImageReader/Writer + FITS, XISF, Qt-image, TIFF backends
  render/               # StretchModel (shared state) + DisplayRenderer
  ui/                   # ImageView, Histogram*, ColorBar, InfoPanel, MainWindow
  app/                  # main.cpp (entry + theme), app.qrc, appicon.png
tests/                  # CTest units (core math, IO round-trips) + script-mode
                        # smoke test (smoke.nsc) + synthetic test image
icon/                   # iconset PNGs + make_icns.sh (-> NebulaScope.icns)
docs/
  BUILDING-macos.md     # Homebrew + libXISF, step by step
  ARCHITECTURE.md       # data flow, the shared-model design, IO details
```

## Quick build

Requires Qt6, CFITSIO + CCfits, and libXISF.

```sh
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build
```

## Tests

Two layers, both run by CI on every push (all three platforms):

```sh
ctest --test-dir build --output-on-failure     # unit tests: core math + IO round-trips
./build/src/nebulascope --run tests/smoke.nsc  # scripted end-to-end smoke test
```

Unit tests cover the stretch/LUT invariants (monotonicity, the
narrow-window anti-posterization rule), adjustments, lossless geometry
round-trips, statistics, and format round-trips (including the XISF
float-normalization required for PixInsight interop). The script mode
(`--run`, see the manual §13) drives the real application — open, stretch,
rotate, export — with assertions, against the committed synthetic FITS in
`tests/testdata/`; add `QT_QPA_PLATFORM=offscreen` to run it headless.

**Linux / Windows** — the binary is `build/src/nebulascope`:

```sh
./build/src/nebulascope                 # empty session
./build/src/nebulascope *.fits          # open a whole set at once
./build/src/nebulascope --list set.txt  # reload a saved image list
./build/src/nebulascope --help          # all options
```

**macOS** — generate the dock icon once, then build a proper `.app` bundle:

```sh
./icon/make_icns.sh                     # creates icon/NebulaScope.icns (needs iconutil)
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build
open build/src/NebulaScope.app          # dock icon + native app menu (About …)
```

The `.app` carries an Info.plist and the galaxy icon; the About entry appears
under the application menu. Command-line use still works via the inner binary
(`build/src/NebulaScope.app/Contents/MacOS/NebulaScope`). The icon step is
optional — the app builds without it, just without a custom dock icon.

Files passed on the command line are registered instantly; each is decoded only
when you view it. Walk the loaded set with **Space** (next) / **Shift+Space**
(previous), or click entries in the Open Images list.

macOS specifics (Homebrew, building libXISF, common link errors) are in
**[docs/BUILDING-macos.md](docs/BUILDING-macos.md)**; see also
**[docs/BUILDING-linux.md](docs/BUILDING-linux.md)** and
**[docs/BUILDING-windows.md](docs/BUILDING-windows.md)**.

## Documentation

- **[docs/MANUAL.md](docs/MANUAL.md)** — the complete user manual: every
  feature, shortcut, dialog, and file format (screenshot slots ready in
  `docs/screenshots/`).
- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** — developer map: layers,
  data flow, and the geometry/windowing invariants to preserve when extending.
- **[docs/DECISIONS.md](docs/DECISIONS.md)** — design-decision log: why the
  pipeline, geometry model, and format interop work the way they do.

## Citing

If NebulaScope is useful in your research, please cite it — citation metadata
is in [CITATION.cff](CITATION.cff) (GitHub renders a “Cite this repository”
button from it).

## License

TODO — pick one before publishing (MIT/BSD-3 are common for tools like this).
Note CFITSIO, CCfits, and libXISF carry their own licenses.

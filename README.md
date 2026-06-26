# NebulaScope

A cross-platform **Qt6** desktop viewer for astronomical images (**FITS** and
PixInsight **XISF**), focused on the thing most tools do poorly: **precise,
draggable RGB-histogram control** — including a Generalised Hyperbolic Stretch
(GHS) for pulling faint galaxy signal out of a high-dynamic-range background
without blowing out cores and stars.

Everything downstream of loading consumes one `astro::ImageData` model and never
needs to know whether a file was FITS or XISF.

## Features

- **Formats** — FITS (CFITSIO/CCfits) and XISF (libXISF), read **and** write.
  Integer images are promoted to Float32 on load so the whole pipeline is
  single-type (FITS scaling via BSCALE/BZERO; XISF integers normalized to [0,1]).
- **Stretch functions** — Linear (with MTF midtone), Log, Asinh, and **GHS**
  with draggable symmetry point (SP) and shadow/highlight protection (LP/HP).
- **RGB histogram** — per-channel or linked Black/Mid/White handles, log-scaled
  frequency axis, live transfer curve; one shared model keeps the curve and the
  image in lock-step.
- **Inspection** — drag-rectangle zoom, right-click/wheel zoom, pan, hover pixel
  readout; auto-stretch (STF) on open.
- **Layout** — dockable image-list and histogram panels (F2/F3), image-only
  mode (Tab), fullscreen (F11).

## Repository layout

```
CMakeLists.txt          # project + dependency discovery
src/
  CMakeLists.txt        # build targets
  core/                 # ImageData, Convert, ImageStats, Stretch (+ GHS math)
  io/                   # ImageReader/Writer + FITS & XISF backends
  render/               # StretchModel (shared state) + DisplayRenderer
  ui/                   # ImageView, HistogramView/Panel, MainWindow
  app/main.cpp          # entry point + dark theme
docs/
  BUILDING-macos.md     # Homebrew + libXISF, step by step
  ARCHITECTURE.md       # data flow, the shared-model design, IO details
```

## Quick build

Requires Qt6, CFITSIO + CCfits, and libXISF.

```sh
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build
./build/nebulascope
```

macOS specifics (Homebrew, building libXISF, common link errors) are in
**[docs/BUILDING-macos.md](docs/BUILDING-macos.md)**.

## License

TODO — pick one before publishing (MIT/BSD-3 are common for tools like this).
Note CFITSIO, CCfits, and libXISF carry their own licenses.

# NebulaScope

A cross-platform **Qt6** desktop viewer for astronomical images, focused on the
thing most tools do poorly: **precise, draggable RGB-histogram control** —
including a Generalised Hyperbolic Stretch (GHS) for pulling faint galaxy signal
out of a high-dynamic-range background without blowing out cores and stars.

Reads **FITS** (incl. multi-HDU and tile-compressed), PixInsight **XISF**, and
ordinary **JPEG / PNG / TIFF**. Everything downstream of loading consumes one
`astro::ImageData` model and never needs to know the source format.

## Features

- **Formats** — FITS (CFITSIO/CCfits, multi-HDU + compressed) and XISF
  (libXISF), read **and** write; JPEG/PNG/TIFF read (via Qt) + 16-bit TIFF
  write. Integer images are promoted to Float32 on load so the whole pipeline is
  single-type (FITS scaling via BSCALE/BZERO; XISF/picture integers normalized
  to [0,1]); NaN/Inf blanks are handled throughout.
- **Stretch functions** — Linear (windowing, with MTF midtone), Log, Asinh, and
  **GHS** (D/b, draggable symmetry point SP, shadow/highlight protection LP/HP).
  Linear sets the black/white window; the nonlinear modes zoom the plot into it
  so their controls span the full widget. Editable numeric fields for every
  parameter.
- **RGB histogram** — per-channel or linked Black/Mid/White handles, log/linear
  frequency axis toggle, live transfer curve and colorbar legend; one shared
  model keeps the curve and the image in lock-step.
- **False colour** — Gray, Heat, Viridis, Magma, Inferno, Cividis, inverted
  gray, and a threshold **Split** map (inverted below the break, normal above)
  for mono frames.
- **Inspection** — drag-rectangle zoom, wheel zoom, right-drag (or Shift/middle)
  pan, hover pixel readout; auto-stretch (STF) on open; an Info panel with data
  range, statistics, FITS structure and a searchable header.
- **Sessions** — multi-image list (append / remove / drag-reorder / export +
  import); **Space/Backspace** blink between images (looping), with per-image
  stretch memory and zoom held across same-size frames.
- **Export** — *Save Data As* (FITS/XISF/16-bit TIFF) and *Export View As* /
  *Export Zoomed Region As* (stretched + colormapped PNG/JPEG/TIFF).
- **Layout** — dockable image-list, info, and histogram panels (F2/F4/F3),
  image-only mode (Tab), fullscreen (F11).

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
when you view it. Walk the loaded set with **Space** (next) / **Backspace**
(previous), or click entries in the Open Images list.

macOS specifics (Homebrew, building libXISF, common link errors) are in
**[docs/BUILDING-macos.md](docs/BUILDING-macos.md)**; see also
**[docs/BUILDING-linux.md](docs/BUILDING-linux.md)** and
**[docs/BUILDING-windows.md](docs/BUILDING-windows.md)**.

## License

TODO — pick one before publishing (MIT/BSD-3 are common for tools like this).
Note CFITSIO, CCfits, and libXISF carry their own licenses.

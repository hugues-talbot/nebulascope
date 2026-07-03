# NebulaScope for iPad — proof of concept (Option A: SwiftUI + C++ core)

This is an **educational proof of concept** showing that the desktop app's
compute core (`Stretch`, `Colormap`, statistics, the render/window pipeline)
runs **unchanged** on iPadOS, with a native SwiftUI touch UI on top. It is a
separate tree from the desktop `src/` — the desktop app is untouched.

> Built and run in **Xcode on a Mac** — the source lives here; Claude cannot
> compile or sign iOS apps. Follow the steps below.

## What it demonstrates

- The **same C++ math** as the desktop (`cpp/Stretch.*`, `cpp/Colormap.*` are
  copied verbatim; only the `#include` prefix changed) driving the display.
- A Qt-free **RenderCore** (`cpp/RenderCore.*`) that replaces the desktop's
  Qt-dependent `DisplayRenderer`/`ImageStats`: synthetic galaxy, stats + STF
  auto-stretch, window→transfer→colormap render to an RGBA buffer, histogram.
- A thin **Objective-C++ bridge** (`NebulaBridge.h/.mm`) exposing two classes to
  Swift: `NBImage` (a loaded/synthetic frame) and `NBStretch` (the parameters).
- A **SwiftUI** UI: pinch-zoom/pan image canvas, and an interactive histogram
  with oversized draggable Black/Mid/White handles, stretch-function segments,
  channel chips, colormap swatches, Auto STF / Reset — all live.

The app **opens on a synthetic edge-on galaxy**, so it runs with zero external
dependencies and no files. FITS loading is wired but optional (see below).

## Build & run

Requirements: macOS + Xcode 15+, and [XcodeGen](https://github.com/yonaskolb/XcodeGen).

```sh
cd ios
brew install xcodegen        # once
xcodegen                     # writes NebulaScopeiOS.xcodeproj
open NebulaScopeiOS.xcodeproj
```

In Xcode: pick an **iPad** simulator (or your device), set your signing team,
and Run. You should see the galaxy with a live histogram; drag the B/M/W pucks
and watch the image restretch, switch Linear/Log/Asinh, and (in mono, if you
load a single-channel FITS) try the colormaps.

> No XcodeGen? Create a new **iOS App** (SwiftUI, Storyboard off) in Xcode, drag
> in the `NebulaScopeiOS/` folder, set **Build Settings ▸ Objective-C Bridging
> Header** to `NebulaScopeiOS/NebulaScope-Bridging-Header.h`, add `cpp/` to
> **Header Search Paths**, and set **C++ Language Dialect** to C++17.

## Enabling FITS loading (optional)

FITS is deferred by default because CFITSIO must be cross-compiled for iOS
(arm64 device + simulator). Once you have a CFITSIO build:

1. In `project.yml`, uncomment the target `settings:` block and point
   `HEADER_SEARCH_PATHS` / `LIBRARY_SEARCH_PATHS` at your CFITSIO, keeping
   `GCC_PREPROCESSOR_DEFINITIONS: NEBULA_HAVE_CFITSIO=1`.
2. `xcodegen` again.

`NBImage.imageFromFITSPath:error:` then reads the primary HDU as Float32
(mono, or 3-plane RGB). Without it, that method returns a clear error string and
the app stays on the synthetic image. **XISF is intentionally out of scope**
here — libXISF pulls in Qt, which defeats the point of a native iOS build; a
real port would use a Qt/QML target (Option B) if XISF on iPad is required.

## File map

```
ios/
  project.yml                       XcodeGen spec (bridging header, C++17, iPad)
  NebulaScopeiOS/
    NebulaScopeApp.swift            @main, dark scheme
    ContentView.swift               toolbar + canvas + panel + filmstrip
    ImageCanvasView.swift           pinch-zoom / pan / double-tap-fit
    HistogramPanel.swift            interactive histogram + handles + colormaps
    InspectorModel.swift            ObservableObject over NBImage + NBStretch
    NebulaBridge.h / .mm            Objective-C++ bridge to the C++ core
    NebulaScope-Bridging-Header.h   exposes the bridge to Swift
    cpp/
      Stretch.h / .cpp              (verbatim from desktop src/core)
      Colormap.h / .cpp             (verbatim from desktop src/core)
      RenderCore.hpp / .cpp         Qt-free galaxy + stats + render + histogram
```

## Honest limitations

- Proof of concept, not a product: single image (synthetic), no file picker /
  share sheet, no persistence, no GHS sliders in the UI yet (the GHS math is
  present in the core and reachable via `NBStretch`).
- The SwiftUI transfer **curve** is drawn with a local approximation of the
  shapes for responsiveness; the **image** itself always goes through the exact
  C++ `buildLut`/`windowCoord`.
- Full-frame re-render happens on the main thread per drag — fine for the
  1000×760 sample; a real build would render on a background queue or in Metal.

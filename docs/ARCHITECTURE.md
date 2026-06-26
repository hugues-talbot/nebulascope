# Architecture

## Data flow

```
file (.fits/.xisf)
  → io::loadImage()            sniff format, decode, promote to Float32
  → core::ImageData            native-planar pixel buffer + ImageHeader
  → core::computeStats()       per-channel min/max/median/MAD
  → render::StretchModel       shared display parameters (the source of truth)
  → render::DisplayRenderer    ImageData + model → 8-bit QImage (per-channel LUT)
  → ui::ImageView              shows the QImage; emits hovered pixel values
```

The histogram UI (`ui::HistogramView` / `HistogramPanel`) writes into the **same**
`StretchModel`; its `changed()` signal triggers a re-render. Nothing else holds
display state.

## The shared-model design

`StretchModel` holds the per-channel Black/Mid/White points, the active stretch
function, the GHS parameters, and each channel's display range `[lo,hi]`.

- `HistogramView` mutates it on every handle drag.
- `DisplayRenderer` reads it to repaint the image.
- `MainWindow` connects `StretchModel::changed` → `updateDisplay`.

Because both the plot and the picture talk to one object, they can never
disagree. Adding a second histogram presentation (e.g. the stacked per-channel
layout from the design mockup) is just another widget bound to the same model —
no new state, no synchronization code.

## Stretch math (`core/Stretch`)

- **Linear/Log/Asinh** share a base shape on the black/white-normalized input,
  then a PixInsight-style **MTF** midtone places the mid handle.
- **GHS** is the integral of a bell-shaped slope function centred at `SP`
  (max contrast there, falling off both directions), with linear
  shadow/highlight protection below `LP` / above `HP`. The result is normalized
  to (0,0)→(1,1) and cached as a LUT. `b` selects the regime: `b<0` logarithmic,
  `b≈0` exponential, `b≈1` harmonic, `b>1` hyperbolic.

All transfers are sampled into a lookup table once per render; pixel mapping is
then a table read.

---

# IO layer

Format-agnostic loading/saving. Everything downstream consumes one
`astro::ImageData` and never needs to know the source format.

## Loading

```cpp
#include "io/ImageReader.h"

astro::io::LoadResult res = astro::io::loadImage("/path/to/NGC4565.xisf");
if (!res.ok) { qWarning() << res.error; return; }

const astro::ImageData&  img = res.image;   // res.image.plane<float>(0), ...
const astro::ImageHeader& hdr = res.header; // hdr.valueOf("EXPTIME")
```

`loadImage` sniffs by extension and magic bytes (`SIMPLE` for FITS, `XISF0100`
for XISF), picks the matching reader, and decodes. Add a format by subclassing
`ImageReader` and registering it in `registeredReaders()` — no caller changes.

### Float32 promotion (default)

`LoadOptions::promoteToFloat` is `true` by default, so every image arrives as
`Float32`:

```cpp
auto res = astro::io::loadImage(path);                              // Float32
auto raw = astro::io::loadImage(path, { .promoteToFloat = false }); // native type
```

- **FITS** is read through CCfits as float, applying `BSCALE`/`BZERO` — so
  unsigned-16 (`BZERO=32768`) and scaled integer images come back as correct
  physical values (ADU).
- **XISF** integer samples are normalized to **[0,1]** by full-scale; floats
  pass through.

The two conventions differ in range (FITS ADU vs XISF [0,1]); that's fine
because the stretch pipeline drives off computed black/white points and image
statistics, not a fixed [0,1] assumption.

## Saving

`saveImage` mirrors `loadImage`: sniff the output extension, pick the
`ImageWriter`, serialize `ImageData` (+ `ImageHeader`). Whatever sample type the
image carries is written — the promoted Float32 workflow saves as `BITPIX=-32`
FITS or Float32 XISF.

```cpp
#include "io/ImageWriter.h"

astro::io::SaveOptions opt;
opt.xisfCompression = astro::io::SaveOptions::Compression::LZ4;  // XISF only

auto sr = astro::io::saveImage("/out/NGC4565.xisf", img, hdr, opt);
if (!sr.ok) qWarning() << sr.error;
```

- **FITS** — `SampleFormat` maps to BITPIX (`USHORT_IMG`/`ULONG_IMG` carry the
  BZERO offset so unsigned data round-trips); structural keywords are filtered
  before re-emitting `header.cards`. Existing files are overwritten.
- **XISF** — planar data copied straight in; embedded FITS keywords written from
  `header.cards`; data-block compression via `SaveOptions`. libXISF implements
  None/Zlib/LZ4/LZ4HC (no zstd — the `Zstd` option falls back to LZ4HC).

## Dependencies

- **CFITSIO + CCfits** — FITS. CFITSIO via pkg-config; CCfits on top.
- **libXISF** — XISF (XML header, planar blocks, zlib/LZ4 compression,
  checksums). https://gitea.nouspiro.space/nou/libXISF

## Known follow-ups

- **libXISF API drift.** Method/enum spellings vary across releases; the marked
  lines in `XisfReader.cpp` / `XisfWriter.cpp` may need adjusting. The
  abstraction boundary keeps any change local.
- **Multi-HDU / multi-image.** Both backends load the primary/first image only.
  Extend `LoadResult` to a list for all HDUs / XISF images.
- **Native-type round-trip.** The Float32 pipeline saves float. To write the
  original integer type, load with `promoteToFloat = false`. XISF has no
  signed-integer sample types — promote signed data to float before writing.
- **XISF property write-back.** The writer emits embedded FITS keywords only;
  mapping typed `header.properties` back to XISF `<Property>` elements is TODO.
- **Preview-while-dragging.** Full-res re-render on each drag is fine for
  moderate images; large mosaics want a downsampled preview during interaction.
- **Image-list switching.** The left dock lists opened files but doesn't yet
  switch the active image.

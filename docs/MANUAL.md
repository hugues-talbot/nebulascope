# NebulaScope — User Manual

*Version 0.85. This is the complete guide; for a quick start see the
[README](../README.md). Every keyboard shortcut named here is a default — all
of them are reconfigurable in **Preferences ▸ Shortcuts** (stored in
`shortcuts.ini`, whose location is shown in the dialog).*

> **Screenshot placeholders** look like the block below. Drop the named PNG
> into `docs/screenshots/` and the image appears; the caption suggests what to
> capture.

![Main window: overlay layout, image loaded, histogram panel open](screenshots/overview.png)
*Capture: whole window, a colour image loaded, overlay panels visible.*

---

## 1. Opening images

**Formats read:** FITS (`.fits .fit .fts .fz`, including multi-HDU files and
tile-compressed images; integer data are scaled via BSCALE/BZERO), PixInsight
XISF (`.xisf`, including image properties and astrometric solutions), and
JPEG / PNG / TIFF / WebP. Everything is promoted to 32-bit float on load, so
the full pipeline is one code path; NaN/Inf blanks are respected throughout.

Ways to open:
- **File ▸ Open…** or the toolbar **Open** button (multi-select works).
- **Drag & drop** onto the window — or onto the app icon (macOS; the bundle
  declares the file types, so Finder's *Open With* also lists NebulaScope).
- **File ▸ Open Recent** — the last 10 images and last 5 annotation files.
- **Command line** — see §13.
- **Multi-HDU FITS** appear in the image list as an expandable entry, one row
  per image HDU; click the HDU you want.

A newly opened image displays immediately — in the first *empty* view cell if
the view is split, otherwise in the active view. On first view it gets a plain
**min→max linear ramp** (predictable, no guessing); press **Auto STF** for a
boosted stretch (§3).

![Image list with a multi-HDU FITS entry expanded](screenshots/image-list-hdu.png)
*Capture: left panel, one multi-HDU file expanded showing its HDU rows.*

## 2. The display pipeline

Raw data are never modified by display operations. Each frame goes through:

1. **Window** — per-channel black/white points inside the data range.
2. **Transfer function** — Linear (with midtone), Log, Asinh, or GHS,
   applied at full float precision across the window (all 4096 LUT samples and
   all output levels span the window — no posterization however narrow it is).
3. **Adjustments** — post-stretch tone & colour controls (§4).
4. **Colormap** (mono images) — false-colour lookup.
5. **Dithered 8-bit conversion** — removes banding in smooth gradients.

Rendering is asynchronous: dragging any control stays fluid, the image
catches up at render rate (intermediate positions are skipped, never queued).

## 3. Histogram & stretch control

The Histogram panel (toggle **F3**) is the heart of the tool:

![Histogram panel: Linear mode, RGB image, per-channel lines](screenshots/histogram-linear.png)
*Capture: right panel, Linear tab, an RGB image with visibly separated
per-channel B/M/W lines.*

- **Stretch functions** — tabs for **Linear / Log / Asinh / GHS**.
  - *Linear* acts as the windowing stage: drag **B** (black), **M** (midtone),
    **W** (white) directly on the plot. RGB images additionally show each
    channel's own B/M/W as thin coloured lines — grab one **in the plot body**
    to move that channel alone; the labelled grips on the top strip move all
    three together.
  - *Log* and *Asinh* compose with the linear window; the plot zooms into the
    window so their controls use the full widget width.
  - *GHS* (Generalised Hyperbolic Stretch): **D** (strength) and **b** (local
    focus) sliders, plus draggable **SP** (symmetry point — where contrast is
    concentrated) and **LP/HP** (shadow/highlight protection) handles, all
    defined inside the window like the other nonlinear modes.
- **Channels** — RGB (linked) or individual R / G / B chips.
- **Editable value boxes** — exact numeric entry; RGB images get a full
  **3×3 grid** (R/G/B × Black/Mid/White) in raw data units.
- **Log axis** button toggles logarithmic frequency scaling of the histogram.
- **Auto STF** — per-channel automatic stretch (background → ~0.25).
- **Auto STF (linked)** — one shared stretch from pooled statistics;
  preserves colour balance (use for colour-calibrated data).
- **Reset** — back to the plain linear window (also clears adjustments).
- The **colorbar** legend shows the current transfer over the window, with
  ticks in real data units; it follows the active colormap *and* the
  adjustments — what the bar shows is what a pixel of that value looks like.

**Copy/paste stretch** (Stretch menu; ⌘⌥C / ⌘⌥V): copies the full stretch.
Pasting **Normalized** re-anchors the window on the target's own
median/MAD statistics (right for comparing different exposures/filters);
**Absolute** carries the exact data-unit window.

**Per-image stretch memory** — every image remembers its last stretch (and
adjustments) and re-applies them when you return to it.

## 4. Adjustments (post-stretch)

The **ADJUST** section sits under the stretch controls — always visible, in
every stretch mode, with its own **Reset** (stretch untouched). All twelve
apply to the stretched display values, so they compose identically with
Linear/Log/Asinh/GHS:

![ADJUST sliders, a temperature/saturation edit in progress](screenshots/adjust-panel.png)
*Capture: ADJUST section with a few sliders off-centre and a visible colour
shift in the image behind.*

| Left column | Right column |
|---|---|
| Bright | Contrast |
| Highlights | Shadows |
| White pt | Black pt |
| Gamma | Temp |
| Tint | Hue |
| Saturation | Vibrance |

- **Tone** (Bright…Gamma): per-channel curves, pinned at black/white ends
  where sensible; reflected live in the transfer-curve overlay.
- **Colour** (Temp…Vibrance): RGB images only. **Vibrance** is saturation
  weighted toward muted pixels — it boosts nebulosity without clipping star
  colour.
- **Click a slider, then use the mouse wheel** for fine steps (hover alone
  never edits).
- Adjustments are **per image** — reset on first visit, remembered per image
  in-session, and **persisted in the annotation sidecar** (§9); a sidecar
  with adjustments restores them on the next session's first view.

## 5. Colormaps (mono images)

Gray, Heat, Viridis, Magma, Inferno, Cividis — selected in the toolbar —
plus two composable **modifiers** that work with *every* map:

- **inv()** — full inversion.
- **split(t)** — below threshold *t* the map runs inverted, above it normal;
  excellent for perceiving faint background structure. Threshold adjustable.

RGB images ignore the colormap (the combo is disabled).

![Split colormap on a galaxy field, colorbar visible](screenshots/colormap-split.png)
*Capture: mono image with split(t) active, the colorbar showing the fold.*

## 6. Inspecting

Mouse (in any view):
- **Left-drag** — rubber-band zoom to the dragged region.
- **Wheel** — zoom at the cursor (**Shift+wheel** = 5× finer steps).
- **Right-drag / middle-drag / Shift+left-drag** — pan.
- **Right-click** — context menu (§10).
- **Hover** — status bar shows (x, y), raw channel values, and RA/Dec when a
  plate solution exists.

Zoom commands: **Zoom to Fit** and **1:1** (key **1**) in the View menu and
toolbar.

The **Info panel** (**P** or F4) shows dimensions, pixel format, per-channel
min / max / median / MAD, the FITS HDU structure, and the full header (FITS
cards or XISF properties) in a filterable, copyable table.

## 7. Sessions, blinking & the image list

- **Space** / **Shift+Space** — next / previous image, looping. Zoom and pan
  are preserved across same-size images, so you can blink a small region.
- **L** toggles the image list; **C** closes the current image (closing the
  last image empties all views).
- List management: **+** append, **−** / context menu remove, drag to
  reorder, export (**⤓**) and **File ▸ Import Image List…** re-load a saved
  list (one path per line, `#` comments, relative paths resolve against the
  list file). `--list` does the same from the command line.
- In-memory results (combine, transport) are marked in the list; **Save Data
  As…** turns the entry into the saved file (name, sidecars, and per-image
  state follow).

## 8. Geometry: rotate & flip

Image menu / toolbar:
- **Rotate 90° CW / CCW** ( `]` / `[` ) and **Flip Horizontal / Vertical**
  (⌘H / ⌘J) — lossless, exact.
- **Rotate by Angle…** (⌘R) — the rotation dialog: a draggable **angle dial**
  (Shift = fine, wheel = ±1°, double-click = 0°), a precision spinbox, and a
  live preview thumbnail. **Apply** rotates without closing (for hunting).
  The angle is *absolute*: re-rotating always resamples **once** from the
  original data, so trying many angles never degrades the image. Bilinear
  resampling; uncovered corners become blank (NaN).
- **⬆ North Up** (in the dialog, when plate-solved) — one click sets the
  angle that puts celestial north up / the central Dec line horizontal.

![Rotation dialog with dial, preview and North Up](screenshots/rotate-dialog.png)
*Capture: the dialog over a plate-solved image, dial at a non-zero angle.*

Everything follows the pixels through every transform: annotations, the
astrometric solution (reference pixel + CD matrix), and view-link
calibrations. Orientation histories are **normalized** — rotating back always
restores the exact original canvas (expansion borders never accumulate). The
orientation is recorded per image (and in sidecars), so an image re-opens the
way you left it. Note: after an *arbitrary* rotation, **Save Data As** writes
resampled pixels — do photometry on unrotated data.

## 9. Annotations

A pure **vector overlay** — never rasterized into the data.

![Annotated field: ellipses, a labelled segment, the RA/Dec grid](screenshots/annotations.png)
*Capture: a few ellipses + one labelled line + text, grid on, one shape
selected showing its grab handles.*

- **Draw** — toolbar tools: ellipse (drag), line segment (drag; the label
  sits beyond the start point, never crossing the segment), text (click).
- **Edit** — click to select (grab handles: axis/endpoint resize, drag body
  to move), **double-click** to edit text & colour, **Delete** to remove,
  **⌘⇧C / ⌘⇧V** copy / paste-at-cursor, full **undo/redo**.
- **Show/hide** — key **A** (grid overlay is separate). Loading or importing
  annotations always makes them visible.
- **Invert contrast** — right-click menu, for bright fields.
- **Persistence** — JSON sidecars (`<image>_annotation.json`), auto-loaded on
  open (Preferences toggle). *Save* overwrites silently; *Save As…* asks.
  Sidecars also carry the image **orientation** and the display
  **adjustments** (§4); saving works with adjustments alone (no shapes
  needed). Unsaved annotations warn on quit.
- **SExtractor import** — Tools ▸ Import SExtractor Catalog… reads ASCII
  catalogs (needs `X_IMAGE`/`Y_IMAGE`; uses `A/B/THETA_IMAGE` ellipses when
  present), with ellipse scale factor, `FLAGS` filtering, `CLASS_STAR`
  colouring, and `NUMBER`/`MAG_AUTO` labels. Detections map correctly onto
  rotated views.

## 10. Astrometry

Solutions are read from FITS WCS keywords (TAN) and from PixInsight's
`PCL:AstrometricSolution` XISF properties; unsolved frames fall back to
telescope-pointing keywords for approximate coordinates.

- Hover readout: RA/Dec of the pixel under the cursor.
- **RA/Dec grid overlay** with labelled, axis-aligned coordinate text
  (density set in Preferences).
- **Right-click menu**, grouped readout / annotations / lookup / zoom:
  copy RA/Dec, copy pixel value, annotate here, paste annotation,
  **Look up in Aladin** (opens Aladin Lite framed ~10× the clicked
  annotation), **Identify in SIMBAD** (cone search scaled to the annotation).

## 11. Combining images

### Combine Channels (Tools ▸ Combine Channels…)

Merges up to **7 mono inputs** — R, G, B, S(II), H(α), O(III), L — into a
colour image via a linear-combination matrix:

![Combine Channels dialog, SHO preset, preview visible](screenshots/combine-channels.png)
*Capture: the dialog with three narrowband inputs assigned and the preview
showing a Hubble-palette result.*

- **Palette presets**: SHO/Hubble, HOO, HSO, LRGB, plain RGB, bicolor.
- Per-channel **pre-normalization**: median / background-pedestal / min-max /
  none / **As displayed** — the last merges each channel *through its current
  view stretch*, i.e. exactly what you see is what combines.
- Both **luminance modes** (proper lightness transfer or linear add).
- Inputs must share dimensions (clear error otherwise).
- Large **live preview** responding to weight changes; per-channel toggles.
- The dialog **remembers its settings**; **Reset** restores defaults.
- The result lands in the first empty view (or the active one), auto-named
  from the palette; save it with **File ▸ Save Data As…**.

### Combine Stars (Tools ▸ Combine Stars (screen)…)

Recomposes a **starless** image with a **stars-only** image via screen
blending `1 − (1−starless)(1−k·stars)` — additive-like in the dark,
saturation-safe in the bright:

- Both images RGB (or both mono), same dimensions; picked from the list
  (auto-guessed from names containing "starless"/"star").
- **Star amount** slider (0–150%) scales the stars before screening.
- Runs on each image's **as-displayed** rendition; live preview; remembered
  pairing.

### Transport Colors (Tools ▸ Transport Colors from Reference…)

Recolours the current image to match a reference's colour distribution
(sliced optimal transport on the displayed values):

- Distributions are estimated **only from the pixels visible in each view**
  — zoom both views onto the object first; off-screen field never steers the
  match. Saturated pixels are excluded (star cores can't and shouldn't
  match).
- Works across modalities (e.g. borrow an RGB rendition's palette for an
  SHO image). Rotated images are handled in the disk frame — no borders are
  baked. The result is a new display-ready list entry; undoable.

![Before/after of a colour transport in a 1×2 split](screenshots/transport.png)
*Capture: source and reference side by side, the `_ct` result in a third view
or after.*

## 12. Split views & linked navigation

**View ▸ Split View…** — one dialog with rows × columns spinners (max 5×5).

![2×2 split comparing renditions, one active cell](screenshots/split-views.png)
*Capture: 2×2 grid, same object in different renditions, active cell's blue
border visible.*

- One cell is **active** (blue border) — the histogram, info panel, tools,
  and rotation act on it. Click any cell to activate; then click a list
  entry to load it there. Each cell keeps its own decoded image, so
  comparisons don't re-decode (unlike blinking large files).
- **Automatic linking** — cells with images of identical dimensions share
  zoom/pan. The **⇄** button on each cell opts out.
- **Calibrated linking** (different sizes) — align the two views manually
  (zoom/pan/rotate until features match), then tick **⇄** on the second
  image: the current correspondence becomes the calibration, and from then
  on the views navigate together, each at its own pixel scale. Calibrations
  survive rotations and flips of either image.
- `--split RxC` sets the grid from the command line (§13).

## 13. Command line

```
nebulascope [options] [files...]

  files                  Images to open (shell globs, or NebulaScope expands
                         unexpanded * ? [ patterns itself).
  -l, --list <file>      Load a saved image list.
      --split <RxC>      Split the view (max 5x5) and assign the first R*C
                         images to the cells in raster order.
      --run <script>     Execute a command script and exit with the number of
                         failed assertions (testing/batch; headless with
                         QT_QPA_PLATFORM=offscreen). Commands: open, show,
                         next/prev, split, fn, autostf [linked], reset,
                         adjust <name> <v>, rot90, flip, rotate, export, save,
                         assert size|channels|pixel|range, sleep, quit — one
                         per line, #-comments; see tests/smoke.nsc.
  -h, --help             This help.
```

Examples: `nebulascope *.fits` · `nebulascope --list tonight.txt` ·
`nebulascope --split 1x2 lum.fits ha.fits`.
(macOS: invoke the binary inside the bundle, or symlink it onto your PATH —
see docs/BUILDING-macos.md. Finder-injected `-psn` arguments are ignored.)

## 14. Export

- **File ▸ Save Data As…** — the *data* (Float32, current orientation):
  FITS, XISF, or 16-bit TIFF. Saving an in-memory result renames its list
  entry to the file. **XISF interop:** float data are normalized to [0,1]
  (PixInsight's convention; `NSSCALE`/`NSZERO` keywords record the original
  range) and data blocks are uncompressed for maximum compatibility.
- **File ▸ Save Stretched As…** — bakes the current display transfer
  (stretch + adjustments) into Float32 FITS/XISF/TIFF.
- **File ▸ Export View As…** (⌘E) — the *displayed* image (stretched,
  colormapped): PNG / JPEG / TIFF / WebP. JPEG asks for **quality**;
  PNG/TIFF ask for **8- or 16-bit** depth (16-bit is built from the float
  render — band-free gradients).
- **File ▸ Export Zoomed Region As…** (⌘⇧E) — same, but only the visible
  region.
- **File ▸ Export / Import Image List…** — session round-trip (§7).
- Annotations and adjustments export via their JSON sidecars (§9).

## 15. Layout, preferences & customization

- **Overlay layout** (default): the image list/info and histogram float
  translucently over the canvas. **O** switches to the classic docked
  layout and back. Panel opacity is a preference — 100% (opaque) is fastest.
- **Tab** — image-only mode (all panels hidden; Esc exits). **Fullscreen**
  on its own shortcut. **H** hides the scrollbars in every view for a clean
  canvas (pans still work).
- **Preferences…** (application menu on macOS):
  - **General** — default annotation colour, text size, line thickness;
    RA/Dec grid density; sidecar auto-load; overlay panel opacity; recent
    files list sizes.
  - **Shortcuts** — every action's binding, editable; stored in
    `shortcuts.ini` (empty value disables; stale clashes revert).
- **About** — version and copyright come from `src/app/AppInfo.h`
  (user-maintained).

## 16. Troubleshooting

- *"No 2-D image in primary HDU"* — the image lives in an extension; pick
  the HDU from the image list entry.
- *Washed-out or black view* — Reset, then Auto STF; check the window
  handles aren't collapsed.
- *Adjustment sliders seem inert* — colour sliders (Temp…Vibrance) are
  disabled for mono images; tone sliders work everywhere.
- *NebulaScope XISF looks like noise in PixInsight* — fixed in v0.83+
  (normalized floats, uncompressed blocks); re-save the file.
- *RA/Dec missing on an XISF* — confirm the file carries
  `PCL:AstrometricSolution:*` properties (Info panel filter: `Astrometric`).
- *Annotations misplaced after import* — they map through the recorded
  orientation; if the sidecar predates v0.12, re-orient and re-save once.
- *Slow zoom/pan with overlay panels* — set overlay opacity to 100% in
  Preferences (translucency costs repaints).
- Build issues — see `docs/BUILDING-{macos,linux,windows}.md`.

# NebulaScope — User Manual

*Version 0.70. This is the complete guide; for a quick start see the
[README](../README.md). Every keyboard shortcut named here is a default — all
of them are reconfigurable in **Preferences ▸ Shortcuts** (stored in
`shortcuts.ini`, whose location is shown in the dialog).*

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
- **Command line** — see §12.
- **Multi-HDU FITS** appear in the image list as an expandable entry, one row
  per image HDU; click the HDU you want.

Files register in the **Open Images** list instantly and are decoded only when
first viewed. On first view an automatic stretch is applied (a gentle
percentile-based linear window; see §3).

## 2. The display pipeline

Raw data are never modified by display operations. Each frame goes through:

1. **Window** — per-channel black/white points inside the data range.
2. **Transfer function** — Linear (with midtone), Log, Asinh, or GHS,
   applied at full float precision across the window (all 4096 LUT samples and
   all output levels span the window — no posterization however narrow it is).
3. **Colormap** (mono images) — false-colour lookup.
4. **Dithered 8-bit conversion** — removes banding in smooth gradients.

## 3. Histogram & stretch control

The Histogram dock (toggle **F3**) is the heart of the tool:

- **Stretch functions** — tabs for **Linear / Log / Asinh / GHS**.
  - *Linear* acts as the windowing stage: drag **B** (black), **M** (midtone),
    **W** (white) directly on the plot.
  - *Log* and *Asinh* compose with the linear window; the plot zooms into the
    window so their controls use the full widget width.
  - *GHS* (Generalised Hyperbolic Stretch): **D** (strength) and **b** (local
    focus) sliders, plus draggable **SP** (symmetry point — where contrast is
    concentrated) and **LP/HP** (shadow/highlight protection) handles, all
    defined inside the window like the other nonlinear modes.
- **Channels** — RGB (linked: dragging one handle moves all three) or
  individual R / G / B chips for per-channel adjustment.
- **Editable value boxes** under the plot let you type exact parameters.
- **Log axis** button toggles logarithmic frequency scaling of the histogram.
- **Auto STF** — per-channel automatic stretch (background → ~0.25).
- **Auto STF (linked)** — one shared stretch computed from pooled statistics;
  preserves colour balance (use for colour-calibrated data).
- **Reset** — back to the plain linear window.
- The **colorbar** legend below shows the current transfer over the window,
  with ticks in real data units; it follows the active colormap.

**Copy/paste stretch** (Stretch menu; ⌘⌥C / ⌘⌥V): copies the full stretch.
Pasting **Normalized** re-anchors the window on the target's own
median/MAD statistics (right for comparing different exposures/filters);
**Absolute** carries the exact data-unit window.

## 4. Colormaps (mono images)

Gray, Heat, Viridis, Magma, Inferno, Cividis — selected in the toolbar —
plus two composable **modifiers** that work with *every* map:

- **inv()** — full inversion.
- **split(t)** — below threshold *t* the map runs inverted, above it normal;
  excellent for perceiving faint background structure. The threshold is
  adjustable.

RGB images ignore the colormap (the combo is disabled).

## 5. Inspecting

Mouse (in any view):
- **Left-drag** — rubber-band zoom to the dragged region.
- **Wheel** — zoom at the cursor (**Shift+wheel** = 5× finer steps).
- **Right-drag / middle-drag / Shift+left-drag** — pan.
- **Right-click** — context menu (§9).
- **Hover** — status bar shows (x, y), raw channel values, and RA/Dec when a
  plate solution exists.

Zoom commands: **Zoom to Fit** and **1:1** (key **1**) in the View menu and
toolbar.

The **Info panel** (F4) shows dimensions, pixel format, per-channel min /
max / median / MAD, the FITS HDU structure, and the full header (FITS cards or
XISF properties) in a filterable, copyable table.

## 6. Sessions, blinking & the image list

- **Space** / **Shift+Space** — next / previous image, looping. The zoom and
  pan are preserved across same-size images, so you can blink a small region.
- **Per-image stretch memory** — every image remembers the last stretch you
  gave it and re-applies it when you return.
- List management: **+** append, **−** / Delete remove, drag to reorder,
  export (**⤓**) and **File ▸ Import Image List…** re-load a saved list
  (one path per line, `#` comments, relative paths resolve against the list
  file). `--list` does the same from the command line.

## 7. Geometry: rotate & flip

Image menu / toolbar:
- **Rotate 90° CW / CCW** ( `]` / `[` ) and **Flip Horizontal / Vertical**
  (⌘H / ⌘J) — lossless, exact.
- **Rotate by Angle…** (⌘R) — the rotation dialog: a draggable **angle dial**
  (Shift = fine, wheel = ±1°, double-click = 0°), a precision spinbox, and a
  live preview thumbnail. **Apply** rotates without closing (for hunting).
  The angle is *absolute*: re-rotating always resamples **once** from the
  original data, so trying many angles never degrades the image. Bilinear
  resampling; uncovered corners become blank (NaN).
- **⬆ North Up** (in the dialog, when plate-solved) — one click sets the angle
  that puts celestial north up / the central Dec line horizontal.

Everything follows the pixels through every transform: annotations, the
astrometric solution (reference pixel + CD matrix), and view-link calibrations.
The orientation is recorded per image (and in annotation sidecars), so an
image re-opens the way you left it. Note: after an *arbitrary* rotation,
**Save Data As** writes resampled pixels — do photometry on unrotated data.

## 8. Annotations

A pure **vector overlay** — never rasterized into the data.

- **Draw** — toolbar tools: ellipse (drag), line segment (drag; the label sits
  beyond the start point, never crossing the segment), text (click). Each
  prompts for an optional label.
- **Edit** — click to select (grab handles appear: axis/endpoint resize,
  drag body to move), **double-click** to edit text & colour, **Delete** to
  remove, **⌘⇧C / ⌘⇧V** copy / paste-at-cursor (fast repeated labelling),
  full **undo/redo** for every operation.
- **Show/hide** — key **A** (grid overlay is separate).
- **Invert contrast** — right-click menu, for bright fields.
- **Persistence** — JSON sidecars (`<image>_annotation.json`), auto-loaded on
  open (Preferences toggle). *Save* overwrites silently; *Save As…* asks.
  The file records the image orientation; a manual **Load Annotations…**
  ignores that as a view instruction and re-maps the shapes onto the current
  view, whatever its rotation. Unsaved annotations warn on quit.
- **SExtractor import** — Tools ▸ Import SExtractor Catalog… reads ASCII
  catalogs (needs `X_IMAGE`/`Y_IMAGE`; uses `A/B/THETA_IMAGE` ellipses when
  present), with ellipse scale factor, `FLAGS` filtering, `CLASS_STAR`
  colouring, and `NUMBER`/`MAG_AUTO` labels — e.g. for building
  machine-learning training sets. Detections map correctly onto rotated views.

## 9. Astrometry

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

## 10. Channel combination

**Tools ▸ Combine Channels…** merges up to **7 mono inputs** — R, G, B, S(II),
H(α), O(III), L — into a colour image via a linear-combination matrix:

- **Palette presets**: SHO/Hubble, HOO, HSO, LRGB, plain RGB, bicolor.
- Per-channel **pre-normalization** (median / background-pedestal / min-max /
  none) and both **luminance modes** (proper lightness transfer or linear add).
- Inputs must share dimensions (clear error otherwise).
- A large **live preview** with a linked auto-stretch that responds to weight
  changes; per-channel on/off toggles.
- The dialog **remembers its settings**; **Reset** restores defaults.
- The result lands in the image list as a new entry (auto-stretched), named
  after the palette; save it with **File ▸ Save Data As…**.

## 11. Split views & linked navigation

**View ▸ Split View** — Single, 1×2, 2×1, 2×2 or Custom… up to **5×5**.

- One cell is **active** (blue border) — the histogram, info panel, tools,
  and rotation act on it. Click any cell to activate; then click an entry in
  the image list to load it there. Each cell keeps its own decoded image, so
  comparisons don't re-decode (unlike blinking large files).
- **Automatic linking** — cells with images of identical dimensions share
  zoom/pan. The **⇄** button on each cell opts out.
- **Calibrated linking** (different sizes) — align the two views manually
  (zoom/pan/rotate until features match), then tick **⇄** on the second
  image: the current correspondence becomes the calibration, and from then on
  the views navigate together, each at its own pixel scale. Calibrations
  survive rotations and flips of either image. Shrinking the grid hides cells
  without dropping their images.

## 12. Command line

```
nebulascope [options] [files...]

  files                  Images to open (shell globs, or NebulaScope expands
                         unexpanded * ? [ patterns itself).
  -l, --list <file>      Load a saved image list.
      --split <RxC>      Split the view (max 5x5) and assign the first R*C
                         images to the cells in raster order.
  -h, --help             This help.
```

Examples: `nebulascope *.fits` · `nebulascope --list tonight.txt` ·
`nebulascope --split 1x2 lum.fits ha.fits`.
(macOS: invoke the binary inside the bundle, or symlink it onto your PATH —
see docs/BUILDING-macos.md. Finder-injected `-psn` arguments are ignored.)

## 13. Export

- **File ▸ Save Data As…** — the *data* (Float32, current orientation):
  FITS, XISF, or 16-bit TIFF.
- **File ▸ Export View As…** (⌘E) — the *displayed* image (stretched,
  colormapped): PNG / JPEG / TIFF.
- **File ▸ Export Zoomed Region As…** (⌘⇧E) — same, but only the visible
  region.
- **File ▸ Export / Import Image List…** — session round-trip (§6).
- Annotations export via their JSON sidecars (§8).

## 14. Preferences & customization

**Preferences…** (application menu on macOS):
- **General** — default annotation colour, text size, line thickness;
  RA/Dec grid density; sidecar auto-load.
- **Shortcuts** — every action's key binding, editable; stored in
  `shortcuts.ini` (an empty value disables a shortcut; stale entries that
  clash with new defaults revert automatically).

**View** — image list (F2), histogram (F3), info (F4) docks; image-only mode
(Tab, Esc exits); fullscreen; annotation (A) and grid overlays; split views.

**About** — version and copyright come from `src/app/AppInfo.h`
(user-maintained).

## 15. Troubleshooting

- *"No 2-D image in primary HDU"* — the image lives in an extension; pick the
  HDU from the image list entry.
- *Washed-out or black view* — Reset, then Auto STF; check the window handles
  aren't collapsed.
- *RA/Dec missing on an XISF* — confirm the file carries
  `PCL:AstrometricSolution:*` properties (Info panel filter: `Astrometric`).
- *Annotations misplaced after import* — they map through the recorded
  orientation; if the sidecar predates v0.12, re-orient and re-save once.
- Build issues — see `docs/BUILDING-{macos,linux,windows}.md`.

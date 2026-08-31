# Release notes

Generated from the annotated `v*` tags (`docs/make-releases.sh`).

## v0.96 — 2026-08-31

v0.96 — the histogram shows the result: live output distribution, sky-patch neutral black, identity restarts

Highlights:

- **The histogram shows the OUTPUT distribution** — the filled shape is
  now the result of the current stretch (the input stays as a faint
  dotted outline; the transfer curve overlays both), computed by pushing
  the data histogram through the curve — no image pass, so it tracks
  every drag tick live. And because that feedback is free, the image
  render is **deferred until the control is released**: a 60-megapixel
  master drags fluidly and renders once, from the final position.
- **Neutral Black from Sky Patch** — neutralising the background is a
  per-filter-pedestal problem, and the estimate is the risky part: so you
  point. Drag a small rectangle over truly empty sky and each channel's
  black point becomes that patch's median in that channel — exactly three
  simultaneous B-drags (midtone as a ratio, white untouched),
  non-destructive, one undo step. It lives in the B/W windows, so it
  survives into Log/Asinh and GHS. Scripts: `blackpatch x y w h`.
- **Identity restarts for the composed stretch** — set up Linear, then
  neutralize before shaping: **M → identity** (Log/Asinh) resets every
  channel's midtone to the window midpoint; **Shape → identity** (GHS)
  sets D to zero with b/SP/LP/HP keeping their anchors. The manual now
  states who is per-channel (Linear: B/M/W; Log/Asinh: window and
  midtone; GHS: one shared shape anchored through each channel's own
  window — and the midtone is NOT part of the GHS composition).
- **Lossless colour transport can turn a palette** — the "as stretch"
  fit's colour stage is now a fitted 3×3 mixer (closed-form weighted
  least squares), alternated with the per-channel curves so the two
  stages solve a joint problem. Starless pairs — whose transport is
  mostly a cross-channel rotation — now match instead of washing out
  grey. The display block gains schema 2 (optional `adjust.mix`, written
  only when present; reference renderer and conformance case updated),
  and both lossless options are on by default.
- **Reset Orientation is forceful** — rotation state lives in three
  places (data histories, calibrated-link transforms, view navigation)
  and a calibrated Match legitimately puts rotation into the viewport;
  Reset Orientation now vacates all of it, for every image and every
  view, redisplaying everything as freshly read. No confirmation: the
  result is the well-defined just-opened state.
- **The visible region is the quad, not its bounding box** — with a
  rotated navigation, Export Zoomed Region is now WYSIWYG (resampled
  through the view transform; scripts: `export region <path>`), and data
  crops and the colour-transport relevance masks use the largest
  rectangle INSIDE what is shown — off-screen sky no longer steers a
  transport (the "khaki wash" field report).
- **Zoom to Width (W)** — fill the viewport width; a portrait image
  fills the screen and scrolls vertically, keeping your position.
- **Appendix: Measuring a telescope against Hubble** — the PSF study
  (star-free kernels against HST ground truth, the stellar/nebulosity
  asymmetry of neural deconvolution, and a full-frame calibrated
  deconvolution with a stated, audited model) joins the book as an
  appendix, with its four instruments under `tools/psf_study/`.

Also: the ADJUST header says what it does (all channels — the selector
scopes the histogram view, not the sliders), and the colour-fit stage's
hue search domain is fixed (it was capped at one degree; a starless
palette rotation could never be expressed).

## v0.95 — 2026-08-19

v0.95 — comparison as an instrument: Match, Values in All Views, the open display format

Highlights:

- **Match views (M)** — calibrated linking by computation or by two
  clicks instead of by eye. With two plate-solved views, **M** computes
  the correspondence from the WCS (pixel→sky→pixel over the overlap,
  affine-fitted; the status bar reports the residual) — no picks at all.
  Otherwise: rough-align, **M**, click a star in one view and the same
  star in the other — one pair snaps the translation exactly; **Shift+M**
  and a second star solve scale + rotation + translation in closed form.
  Verified against ground truth to 0.001 px (features) and 0.02 px (WCS).
- **Values in All Views (V)** — hovering the active view shows the
  coordinates and pixel value(s) under the pointer in EVERY cell, each
  read from its own data at the corresponding pixel (through the
  calibration for matched views), with a **crosshair** in each cell at the
  measured pixel. The link transforms made visible: hover a star and see
  whether the other view's crosshair lands on it.
- **The display block is an open, reproducible format** — Save
  Annotations & Display stores the full appearance (transfer function,
  per-channel windowing, GHS, colormap, adjustments) in the sidecar under
  a versioned `display` key. A written specification (Colour Transport
  chapter §6) maps every field to its equation, and a standalone NumPy
  reference renderer (`tools/render_sidecar.py`) reproduces NebulaScope's
  rendering — verified in CI to a few float32 ULPs per pixel on macOS,
  Linux and Windows. A stretch becomes explainable; a non-destructive
  transport fit becomes a file you can hand to a collaborator.
- **Native save panels on macOS** — Export View As… and Save Data As… are
  real NSSavePanels with the format / depth / compression / quality
  controls as a native accessory row (sidebar favourites, iCloud, folder
  behaviours). Clicking any image still adopts its base name; the saved
  extension always follows the chosen format.

Fixes: in the docked layout, clicking between split cells no longer
shifts the grid (the hidden Info tab's statistics table was resizing the
shared dock area); a non-destructive transport fit saved in a sidecar now
reloads exactly (the sidecar carried only the adjustment layer, not the
stretch); shortcuts.ini records each entry's default, so renamed default
keys follow the release while user customisations survive.

Illustrated in the manual by the Eastern Veil, two nights: a friend's 5 h
on a Vixen 102/900 matched to ~1 h on a TS 80/380, colours transported
non-destructively — what remains different is signal, not processing.

## v0.94 — 2026-08-14

v0.94 — selection-aware culling, instant batch close, metadata-less CFA, Stellarium

Highlights:

- **Selection-aware image list** — the culling keep-checks understand
  multi-selection: **B** group-toggles the highlighted rows (checks all;
  unchecks only when all are already checked), clicking a checkbox inside
  a multi-row selection retags the whole selection, and **C** closes every
  highlighted image. The context-menu entry is now **Close & Remove from
  List**, and it means it: decoded pixels, per-view copies, and all
  per-image state are freed on close. In-app results (Combine, transport,
  crop) carry the keep-check too.
- **Instant batch close** — closing hundreds of blinked frames used to
  freeze the app for tens of seconds: each removed row triggered a full
  decode of its neighbour. Row removals now run signal-blocked with a
  single display fix-up, so clearing a 400-sub session is instantaneous.
- **Annotation-safe closing** — when closed images carry unsaved
  annotation edits, one prompt covers the whole batch: **Save
  Annotations** writes every affected image's default sidecar, **Ignore
  and Close** discards, **Cancel** aborts untouched.
- **Metadata-less CFA (mosaic sniffer)** — planetary/solar capture tools
  can dump raw Bayer mosaics into plain grayscale PNG/TIFF with no
  pattern keyword anywhere. NebulaScope now detects the mosaic
  statistically on open and names the two candidate patterns in the
  status bar; **Image ▸ Debayer ▸ Apply Choice to All in List** (script:
  `debayer bggr rcd all`) stamps a forced pattern onto a whole capture
  stream. Born from — and illustrated in the manual by — the total solar
  eclipse of August 12, 2026.
- **Point Stellarium Here** — the sky context menu can point a running
  Stellarium (Remote Control plugin) at the clicked J2000 coordinates,
  field of view matched: where Aladin/SIMBAD answer "what is this",
  Stellarium answers "where is it in tonight's sky".
- **Clean canvas** — **H** now also hides the active-cell border and link
  buttons; with fullscreen (⌥F) the display is pure image, e.g. to
  preview a wallpaper.

Also: HISTORY.md, a development chronicle of how this program came to be;
design notes on the removal cascade in DECISIONS; French translations for
everything above.

## v0.93 — 2026-08-06

v0.93 — PixInsight display interop, stretch history, DS9 palettes

Highlights:

- **PixInsight display interop** — a PI-saved XISF now opens LOOKING as
  PI showed it, with far-out STF white points rebased onto the data
  range in closed form (histogram controls stay fully usable, SPCC
  channel balance preserved exactly; derivation in the book's new
  Mobius-rebase appendix): the embedded display function (STF) applies on first
  view, embedded ICC profiles are honoured, and on macOS the window is
  colour-managed for wide-gamut (P3) panels. The transfer LUT adapts
  its resolution to the occupied window and the histogram's curve
  overlay is evaluated exactly — imported stretches with far-out white
  points render smoothly.
- **Stretch undo history** — every stretch/adjustment gesture is one
  undo step (drags coalesce): ⌘Z walks back to the as-loaded state,
  through Auto STFs, pastes, transport fits. **Reload Original**
  (⌘⇧R) re-decodes from disk and re-runs the first-view rules, itself
  undoable.
- **DS9 classic palettes** — a, b, bb, he, cool, rainbow, standard, and
  the stepped i8, aips0, sls, reproduced from SAOImage DS9's reference
  control points (renditions match DS9 exactly); compose with the
  invert/split modifiers. Requested by a user.
- **Save ergonomics** — an in-memory result's list entry takes the
  saved file's name on every save path (Save Data As, Save Stretched
  As, script save); clicking any image in a save dialog adopts its
  base name, the extension following the chosen format.
- **Build provenance** — the About headline and --version carry the
  exact git build id, so test binaries identify their commit.

An imported UNLINKED display function (the channel-equalizing STF that
visually cancels an SPCC calibration) is flagged in the status bar with
the remedy: Shift+U preserves calibrated colour. The import pipeline is
regression-tested end to end via synthetic XISF fixtures and the new
`assert stretch` script assertion.

Fixes: linked histogram drags are rigid (per-channel clamps no longer
ratchet the channels' offsets apart on repeated drags); the
panel-resize cursor no longer sticks over overlay panel contents; the
script cmap command actually applies the selected map.

## v0.92 — 2026-08-04

v0.92 — list & session ergonomics, inline export options, display sampling

Highlights:

- **Large-list workflows** — drag a list row onto any view cell to show
  it there; the list holds each image once (re-opening selects the
  existing row); batch opens fill the empty cells in the order given;
  **Clear List & Close All** (⌥C) sweeps the session in one stroke.
  Together with Apply to All and blink culling, per-filter inspection of
  a full night's acquisitions is now a tight loop.
- **Inline save options** — pixel depth (PNG/TIFF), quality (JPEG, and
  now WebP), and XISF compression are options INSIDE the save dialogs,
  enabled by the selected format — no follow-up prompts.
- **Display sampling** — bilinear when zoomed out (kills the moiré that
  nearest-neighbour subsampling beats out of CMOS fixed-pattern noise),
  crisp nearest-neighbour at 1:1 and beyond.
- **Window & dialogs** — ⌥F is the green button on a key (native full
  screen on macOS; maximize elsewhere); every file dialog starts in the
  current image's directory.
- **Fixes** — Zoom to Fit/1:1 acted on the wrong cell in split views
  (stale receiver); overlay panels no longer hide behind newly created
  cells after a split; Finder open events carry status-bar feedback and
  Console logging; proper French plural forms for counts.
- **Manual** — §12 gains a real-data showcase: NGC 7331 with SN 2025rbs,
  a C11 beside a 180mm camera lens in linked views. Release notes now
  live in the annotated tag, rendered identically on the release page,
  in the manual appendix, and in both PDFs.

## v0.91 — 2026-08-01

v0.91 — French localization, blink culling, crop tool, transport stretch fit

Highlights:

- **Français** — the full UI is localized (system-language default,
  Preferences ▸ Language override, `--lang` CLI flag), alongside a French
  user manual and colour-transport chapter: on the manual site, and as a
  second PDF on every release. The `.nsc` script language and CLI output
  stay English as a stable, locale-independent API.
- **Colour transport, stage two** — the non-destructive stretch fit
  (matches the transported look with zero data loss — cannot posterize,
  never amplifies noise) gains intensity weighting and an optional
  cross-channel colour-adjust stage for hue-rotation targets. Undoable;
  derivations in the new Colour Transport chapter.
- **Blink culling** — keep-checks on every list row (toggle with B while
  blinking), then sort / move / remove checked frames; scriptable via
  tag/tagsort/tagmove/tagremove.
- **Crop to Visible Region** (Shift+C) — full-bit-depth crop into a new
  list entry; the astrometric solution survives exactly (CRPIX rebase),
  annotations and stretch follow.
- **Keyboard zoom** — > / < (10%) and . / , (3%), configurable; arrow
  keys reserved for panning.
- **Manual online** — the documentation book is published at
  https://hugues-talbot.github.io/nebulascope/ (English) and /fr/
  (français), rebuilt on every docs change.

Fixes: large TIFF re-open (Qt decode allocation limit), XISF→FITS
metadata preservation (typed keyword values; standard cards synthesized
from PixInsight properties), Save Annotations enabled for
orientation-only state.

## v0.90 — 2026-07-30

v0.90: OSC debayering (RCD validated against Siril, undoable), auto-reload interop, fast batch opens, shared STF, stretch-function shortcuts


## v0.89 — 2026-07-28

v0.89: script-command CLI reference (--run list / --help <cmd>), Quarto documentation book, PDF attached to releases


## v0.88 — 2026-07-28

v0.88: scripted documentation complete, dialog automation, transport CLI, macOS open fix


## v0.87 — 2026-07-28

v0.87: testing + docs consolidation — 7 CTest suites on 3 platforms, PI interop fixtures, script-driven screenshots, zstd/Finder-open/split fixes


## v0.86 — 2026-07-26

v0.86: XISF block compression re-enabled, save-time codec choice (Zstd/Zlib/Uncompressed); CI: Qt imageformats + Windows test DLL paths


## v0.85 — 2026-07-24

Unit test with a cheap command language.


## v0.84 — 2026-07-24

New features described in the manual, more extensive testing.


## v0.83 — 2026-07-24

Rotation adding border persistent bug removed


## v0.82 — 2026-07-24

Optimal transport colour match. Amazing feature.


## v0.81 — 2026-07-23

General improvements to the user interface


## v0.80 — 2026-07-23

This version sets up a workable distribution system on Github


## v0.73 — 2026-07-23

This version is deployed on github


## v0.72 — 2026-07-23

This version compiles on Windows, Linux and Macos


## v0.71 — 2026-07-22

Version with side panels


## v0.70 — 2026-07-21

New version with linked view. Totally cool


## v0.52 — 2026-07-20

Now we have annotations, pretty solid and consistent.


## v0.6 — 2026-07-21

This version now reads and displays sextractor catalogs as annotations.


## v0.5 — 2026-07-19

Smooth display pipeline: dithered, interpolated rendering


## v0.0 — 2026-06-26

v0.0: project inception — FITS/XISF inspector skeleton with histogram stretch



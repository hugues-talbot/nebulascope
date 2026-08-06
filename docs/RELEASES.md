# Release notes

Generated from the annotated `v*` tags (`docs/make-releases.sh`).

## v0.93 — 2026-08-06

v0.93 — PixInsight display interop, stretch history, DS9 palettes

Highlights:

- **PixInsight display interop** — a PI-saved XISF now opens LOOKING as
  PI showed it: the embedded display function (STF) applies on first
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

Fixes: the panel-resize cursor no longer sticks over overlay panel
contents; the script cmap command actually applies the selected map.

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



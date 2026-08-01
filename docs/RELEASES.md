# Release notes

Generated from the annotated `v*` tags (`docs/make-releases.sh`).

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



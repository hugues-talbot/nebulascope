# NebulaScope — video presentation storyboard

Two cuts from the same material:

- **Cut A — overview** (3–4 min): scenes 1–5, tight narration, for the club
  and YouTube.
- **Cut B — methods** (10–15 min): scene 5 expanded into the PSF-study
  walkthrough (the appendix as a screencast), for the technically curious.

## Production model

Every scene is driven by a `.nsc` script — the app's script engine is the
demo automation system. **Record each scene separately** and edit together:
a fluffed take costs one re-run, not a whole session. The scripts end
*without* `quit`, leaving the app live at the scene's final state for
manual beats (marked ▶ below — the moments that should visibly be a human
hand).

Run every scene **from the image data folder**:

```bash
cd ~/Projects/Claude/Data/Nebulascope
/Applications/NebulaScope.app/Contents/MacOS/NebulaScope --run <repo>/demo/scene1-open-and-look.nsc
```

Recording notes: fixed window size across scenes (record the window, not
the full screen); French or English narration — the app follows the system
language, so set it per cut; `sleep` pacing in the scripts is tuned for
narration at a calm pace — stretch them if a beat needs more air; kill the
first-launch font-cache delay by opening the app once before recording.

---

## Scene 1 — Open and look (~40 s) — `scene1-open-and-look.nsc`

**Screen:** M81 opens instantly with PixInsight's saved screen stretch
honoured; two more images; blink through the list; then a grayscale PNG of
the August 2026 eclipse — the status bar flags it as an undecoded Bayer
mosaic and names the candidate patterns; forcing BGGR turns it into the
colour Sun.

**Narration beats:**
- "Everything opens as Float32, and an image processed elsewhere opens
  looking the way its producer saw it."
- "Blinking is instant — this is a culling tool as much as a viewer."
- "This PNG came straight from a solar capture stream. No header, no
  metadata — but the pixels betray the mosaic, and NebulaScope says so.
  One click, and there's the eclipse."

## Scene 2 — The histogram is an instrument (~70 s) — `scene2-histogram-instrument.nsc`

**Screen:** the Veil (TS80). Auto STF; the white grip drags — the **output
histogram** reshapes live while the 9-megapixel frame waits; on release,
one render. Sky-patch neutral black: three black points jump to the three
channel medians in one gesture. GHS with SP snapped to the mode.

**Narration beats:**
- "The filled shape is the *result* of the stretch — the distribution of
  what you're seeing. It costs microseconds, so it follows the drag; the
  image itself renders once, from where your hand stops."
- "Backgrounds differ per filter. Point at empty sky, and the black points
  align to it — per channel, non-destructively, one undo."
- "GHS rides on top of that balance, symmetry point on the mode — the
  Siril workflow, with the feedback built in."

▶ **Manual:** drag the D slider slowly (watch the output histogram
equalize); switch to Asinh and press **M → identity**; hover the
"ADJUST — ALL CHANNELS" header.

## Scene 3 — Comparison as an instrument (~60 s) — `scene3-comparison.nsc`

**Screen:** the same Veil field from two telescopes — 5 h on a Vixen 102
against 1 h on a TS80 — side by side. Values in All Views: the pointer's
crosshair fans out to both cells, each reading its own data.

**Narration beats:**
- "Same sky, two instruments, five hours against one. Which detail is
  real? Put the pointer on it and both images answer at once."

▶ **Manual:** press **M**, click a star left, the same star right — the
views snap together; **Shift+M** and a second star, and rotation and scale
are solved. Pan one view; both follow, through the calibration.

## Scene 4 — Narrowband to colour (~35 s) — `scene4-combine.nsc`

**Screen:** three narrowband masters; the Combine dialog assigns S/H/O
roles, the SHO preset weights them, Create Image produces the palette
image, Auto STF.

**Narration beats:**
- "Three filters, three files. Roles, preset, create — and the combination
  is baked through interpolated transfer tables, so a steep stretch never
  posterizes it."

## Scene 5 — Against Hubble (~90 s) — `scene5-showpiece.nsc`

**Screen:** the presenter's TEC 140 pillars beside the Hubble Heritage
rendition. Lossless colour transport pulls the amateur palette to
Hubble's — no pixel written. Then Tools ▸ Measure PSF: the report, and
gold rotated ellipses on the fitted stars.

**Narration beats:**
- "The reference doesn't have to come from your telescope — or your
  planet. Transport matches distributions; what remains different after
  the match is optics and orbit, not processing."
- "And that difference is measurable. Every star fitted with an elliptical
  Moffat: FWHM in arcseconds from the plate solution, eccentricity, and
  its direction — drift or optics, the field map tells you which."
- Closer: "The manual's appendix takes this all the way: the telescope's
  PSF measured against Hubble itself, and an audit of what neural
  deconvolution really does. NebulaScope is an inspector — it doesn't
  just show you the image; it lets you interrogate it."

▶ **Manual:** click **Annotate stars** in the report; zoom into the
ellipses along the drift axis.

---

## Cut B additions (methods walkthrough)

Scene 5 expands using `tools/psf_study/` on camera: the display-space vs
linear distinction, the star-free kernel against the HST mosaics, the
BXT stars-vs-nebulosity asymmetry table, and the calibrated deconvolution
with its delivered-PSF verification. Structure follows docs/PSF-STUDY.md
section by section; the appendix *is* the script.

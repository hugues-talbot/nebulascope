# Appendix: measuring a telescope against Hubble {#sec-psf-study}

*A case study in comparison as an instrument — the PSF of a TEC 140
refractor measured star-free against HST ground truth, an audit of what
neural deconvolution does to stars versus nebulosity, and a full-frame
calibrated deconvolution whose every pixel has a stated model. The scripts
live in [`tools/psf_study/`](https://github.com/hugues-talbot/nebulascope/tree/main/tools/psf_study);
this appendix records the method, the numbers, and — deliberately — the
mistakes, because most of what the study taught was learned by being wrong
in public and checking.*

## The question

The *Colour transport* chapter closes on a comparison: ten hours of M16
through a 140 mm apochromat beside the Hubble Heritage image of the same
pillars. The natural quantitative question follows: **what is the true
point-spread function of the amateur image — measured not from its stars,
but from the extended structure itself, using Hubble as ground truth?**
Stars are the standard PSF probe, but they are also exactly what modern
neural deconvolution (BlurXTerminator, "BXT") treats best; a star-based
figure flatters it. The Hubble overlap allows the star-free measurement:
if $t$ is the amateur image and $h$ the Hubble image registered onto its
grid, the kernel $k$ minimizing $\lVert h \ast k - t \rVert^2$ *is* the
amateur PSF — nebulosity and all — obtained in closed form as a Wiener
quotient of spectra.

The study grew into four instruments, each validating or correcting the
others:

| Instrument | Space | Measures |
| --- | --- | --- |
| `psf_pipeline.py` | display (stretched exports) | relative PSF across processing variants |
| `star_fwhm.py` | linear | stellar PSF: elliptical Moffat fits, field maps |
| `linear_deconv.py` | linear | extended-structure PSF vs HST; held-out fidelity |
| `full_deconv.py` | linear | full-frame calibrated deconvolution + verification |

## Registration, the load-bearing wall

Every measurement rests on registering two images that share nothing but
sky: different samplings (0.04″ to 0.76″ per pixel), arbitrary rotation,
sometimes opposite **parity** (the HST mosaic's CD matrix has negative
determinant; a similarity transform cannot represent a reflection, so the
mirror state is tried both ways and decided by correlation). The machinery
that proved robust, after star-matching alone repeatedly converged to
false minima under large unknown rotations:

1. **Exhaustive similarity search** — band-passed cross-correlation over a
   rotation × scale grid (phase shift recovered from the correlation peak),
   parallelized over the scale axis. No correspondences exist to be fooled;
   the true peak dominates by construction. Bright cores are first
   compressed with `asinh` so a saturated star cannot own the spectrum.
2. **Star refinement** — unique nearest-neighbour pairs (greedy by
   distance), similarity fitted by complex least squares
   $q = a p + b,\ a = s e^{i\theta}$, iterated with a shrinking tolerance.
   Residuals of 0.1–0.4 px were routine once seeded correctly.
3. **Verification by eye, always** — a checkerboard blend of the warped
   reference against the target. Two of the study's wrong turns were caught
   only because a human looked at this image.

## The scale lesson

The first arcsecond figures hung on a chain: comparison-image pixels →
master pixels (a template match) → arcseconds (the plate solve). The
template-match link was **wrong by a factor 1.69**, and its supposed
corroboration — an assumed Hubble field size — was 30 %-loose confirmation
bias. It was falsified only when a clean export was registered *directly
against the plate-solved master*: same-source stars, 16 pairs, 0.12 px
residual, scale 0.9999 — the exports were at native sampling. Every
earlier absolute number moved by 1.69×; every relative conclusion
survived untouched.

**Lesson.** In any calibration chain, the error lives in the one link
never verified against ground truth. "Consistent with an assumed field
size" is not a check; a same-source registration with sub-pixel residuals
is.

## Display space inflates; linear space decides

The first campaign measured stretched 8-bit exports, because that is what
one has at hand. A nonlinear stretch is concave: it widens every profile
at half maximum. Simulation predicted 1.5–2× inflation for stellar
contrast; the linear re-measurement then pinned it empirically at
**2.0–2.6×, varying with each row's stretch** — so a display-space table
cannot even be rescaled into truth. It retains exactly one use: relative
comparison of variants that share a stretch.

The linear stellar measurement (`star_fwhm.py`: elliptical Moffat (Moffat 1969) fits over ~2 700 stars per channel, cross-validated within
3 % of PixInsight's FWHMEccentricity) gave the study its reference table
for the four processing variants of the same data — full stack versus
best-15 % "lucky" selection, before and after BXT:

| Variant | S II | Hα | O III |
| --- | --- | --- | --- |
| Full stack, raw | 1.96″ | 1.94″ | 2.27″ |
| Full + BXT | 1.38″ | 1.30″ | 1.37″ |
| Lucky, raw | 1.82″ | 1.77″ | 1.92″ |
| Lucky + BXT | 1.25″ | 1.18″ | 1.19″ |

A caveat the study only sharpened later belongs up front: **stellar
analysis is measurement only on raw linear data and after calibrated
deconvolution.** A neural model's stellar profiles are largely drawn
from its prior — ML5's defaults deliver 0.97–1.04″ against a 0.99″
diffraction limit, which no measurement of this atmosphere could yield —
so the BXT rows above describe a *rendering* of stars, essentially
cosmetic, and are kept for completeness rather than inference. The rows
that carry physics are the raw ones; the deconvolution's stars carry a
different kind of meaning — a *declared* PSF whose delivery is
re-measured (the audit later in this appendix).

Three physical findings fell out — all from the **raw** rows. The
**optics are diffraction-limited**: 0.99″ diffraction for 140 mm at Hα,
and field-uniform FWHM to ~5 % across a 2° field — not something a
deficient objective could produce. (An earlier draft also cited BXT
reaching near the limit as corroboration; by the caveat above, that
argument is retracted — a prior-drawn star proves nothing about the
glass.) The **eccentricity
(0.35–0.56) is one-axis drift, not optics**: its position angle is uniform
across the entire field in every channel (tilt or collimation would rotate
toward the corners), indicting guiding/flexure along a single axis — worth
~0.2″ of real resolution, the cheapest gain in the system. And **sub
selection buys little** once the stacker's quality weighting has already
suppressed poor frames: hard-culling 85 % of subs improved the stack by
only ~0.15″.

## What BXT does to nebulosity

The star-free question needed *linear* ground truth: the Hubble Heritage
WFC3/UVIS mosaics of M16 (F673N/F657N/F502N — filter-matched to S II, Hα,
O III), registered onto the linear masters and compared through a Wiener
kernel estimated on the **west half** of the overlap only, with the **east
half** held out for validation. Two traps cost a day each: linear images
concentrate power at DC, so the Wiener quotient must be band-passed or its
regularizer flattens everything into an arcseconds-wide pseudo-kernel; and
the mosaic's rotated footprint requires all statistics to be confined to
fully-covered regions.

Results (extended-structure kernel FWHM, raw → BXT): S II 1.92″ → 1.55″,
Hα 3.08″ → 2.62″, O III 2.49″ → 1.39″. The S II raw kernel matching the
stellar 1.96″ validates the method; the Hα kernel is inflated by [N II]
emission present in F657N but excluded by the narrow Hα filter — a
spectral, not optical, mismatch. The headline: **in the clean channels BXT
sharpens extended structure by 15–19 %, versus 30–33 % on stars** — the
star-based figure of merit roughly doubles the real gain on the
astrophysically interesting content. On *fidelity*, however, BXT is
honest at these scales: scored against Hubble degraded to a common 1.3″
on the held-out half (star profiles masked), its nebulosity moves toward
the truth in S II and Hα and is neutral in O III — no evidence of
hallucination at 1.3″ scales, merely conservatism.

### The ML5 revision, audited

When RC Astro released the ML5 model, the framework above turned a
release note into a measurement — deliberately confined to **the part of
the nebula covered by Hubble**: stars are exactly what neural models
handle by prior, so a model revision is judged here on extended
structure alone (the stellar reference table earlier in this appendix
remains the four-way full/lucky × raw/ML4 design). `ml5_audit.py` runs
the same registrations and the same fit/validate rectangles for every
render; the ML5 renders use reduced star sharpening. The full design —
both stacks, three renders each; extended-structure kernel FWHM and
held-out star-masked nebula NRMSE against Hubble at a common 1.3″:

| Variant | S II | Hα | O III |
| --- | --- | --- | --- |
| Full stack, raw | 1.92″ / 0.78 | 3.08″ / 0.31 | 2.49″ / 0.57 |
| Full + ML4 | 1.55″ / 0.51 | 2.61″ / 0.21 | 1.40″ / 0.29 |
| Full + ML5 | 1.58″ / **0.27** | 2.66″ / 0.20 | 1.60″ / **0.23** |
| Full + deconv (1.8″ RED) | 1.96″ / 0.73 | 2.75″ / 0.30 | 2.01″ / 0.51 |
| Lucky, raw | 1.92″ / 0.57 | 3.05″ / 0.23 | 1.74″ / 0.37 |
| Lucky + ML4 | 1.74″ / 0.39 | 2.54″ / 0.20 | 1.57″ / 0.34 |
| Lucky + ML5 | 1.82″ / 0.36 | 2.52″ / 0.20 | 1.71″ / 0.33 |
| Lucky + deconv (1.8″ RED) | 2.10″ / 0.50 | 2.81″ / 0.23 | 1.93″ / 0.31 |

The deconvolution rows are the calibrated MCS+RED products (declared
1.8″, stellar delivery verified in contract), and at first sight they
look alarming: the S II extended kernel does not tighten at all. The
alarm dissolves under quadrature arithmetic, and the dissolution is
itself the finding. The S II transfer takes the stellar 1.96″ to 1.80″,
so its effect on extended structure already at 1.92″ is
$\sqrt{1.92^2 - 1.96^2 + 1.80^2} = 1.75″$ — and with the pure-MCS
control at a 1.9″ declaration the predicted gain is a bare 0.06″,
invisible inside the kernel estimator's ±0.2″. Where the raw extended
kernel *exceeded* the stellar one, the predicted gains duly appear
(pure-MCS control, 1.9″ declaration: O III 2.49″ → 2.07″ measured
against 2.16″ predicted; Hα 3.08″ → 2.84″). **The calibrated
deconvolution changes extended structure by exactly the amount it
declares — which at a conservative target is small.** Neural models
show large kernel changes precisely because they are not bound by a
declaration. On fidelity the deconvolution rows sit modestly above raw
(no harm, honest gain) but far from the neural rows: at this metric the
truth is at 1.3″, and a declared 1.8″ product simply cannot approach it
the way an unbound model that also denoises can. One measurable cost of
the RED prior surfaced here: the pure-MCS control scores better on S II
nebulosity (0.64 vs 0.73) — the starlet prior buys its quiet background
with a little nebular fidelity, a milder cousin of the asymmetry this
appendix documents for BXT (the prior is star-neutral, so stars get the
full inversion while nebulosity gets inversion-plus-smoothing).

Five findings, none visible to a star-based comparison:

1. **ML5 sharpens extended structure slightly *less* than ML4 on both
   stacks** (markedly so in O III) **while being more faithful to it** —
   on the full stack the S II fidelity nearly halves. The revision
   traded nebular aggression for nebular truth.
2. **The model revision's gains materialize on the deep stack.** On the
   lucky selection ML4→ML5 is nearly a wash (S 0.39→0.36); on the full
   stack it is transformative (0.51→0.27). Recovering faint structure
   faithfully needs the photons the culling threw away.
3. **Sub-selection helps the raw render, not the ceiling.** Raw-lucky
   beats raw-full in fidelity everywhere, and O III's kernel gains a
   real 0.75″ — but every lucky variant loses to ML5-on-full in S II
   and O III. The contrarian moral: *keep the frames, upgrade the
   model.*
4. The S II raw kernels are **identical (1.92″) between stacks** — on
   seeing-limited extended structure, culling 85 % of the subs changed
   nothing, echoing the stellar finding.
5. **A declared method scores as it declares.** The deconvolution rows
   change the extended-structure kernel by their stated (small) amount
   and buy fidelity honestly but modestly; the neural rows change it by
   whatever their prior deems right and score accordingly. The table
   does not say which philosophy to prefer — it says, for the first
   time, exactly what each one costs.

One measured caveat carried from the audit: the extended-structure
kernel is render-sensitive at the ±0.2″ level (faint-star leakage into
the "starless" statistics), so the fidelity score — star-masked by
construction — is the sturdier of the two instruments.

## Calibrated deconvolution with a stated model

The final experiment: deconvolve the full raw master with the *measured*
PSF, to a *declared* target, and verify the delivery. Three formulations
failed instructively before one worked:

1. **Truncated Richardson–Lucy** (Richardson 1972; Lucy 1974) with the
   iteration count chosen for fidelity stops at ~5–10 iterations — far
   short of shape convergence, so the input's elongation survives. An
   iterative solver trades noise against the PSF contract *implicitly*,
   and loses.
2. **An explicit "partial kernel"** $p = k \oslash t$ formed by Fourier
   division explodes: a Moffat spectrum decays polynomially, a Gaussian
   target's super-exponentially, so the quotient diverges at high
   frequency and the clipped kernel is structurally wrong.
3. The working form is the **MCS one-filter transform** of Magain, Courbin & Sohy
   (1998) — see also the review by Starck, Pantin & Murtagh (2002):
   deconvolve by the measured OTF
   and reconvolve by the target in a single linear pass,
   $$\hat X = Y \cdot \frac{T\,\overline{K}}{|K|^2 + \lambda},$$
   the target's own spectral decay taming the high band. Regularization is
   chosen **contract-first**: the largest $\lambda$ whose *delivered*
   stellar FWHM — re-measured on the filtered frame — still honours the
   declared target within 5 %. Fidelity is then reported, not optimized.

Two honest limits emerged. A 1.5″ round target is **unsupported by the
data**: the anisotropy correction lives at frequencies where the noise
floor forbids amplification, which is precisely the quantitative sense in
which BXT's 1.3″ output is drawn from its prior rather than from the
photons. And saturated star cores violate the convolution model outright;
they are excluded by a feathered mask (0.04 % of pixels retain input
values), the exception recorded in the output header.

At a 1.9″ circular target the contract closes: delivered geometric-mean
FWHM 1.89–1.91″ in all three channels — **the PSF homogenized across
S II/Hα/O III to 1 %**, valuable in itself for any line-ratio work —
eccentricity reduced (0.56 → 0.36 in the worst channel, position angles
decohered), and held-out nebular fidelity statistically tied with both the
raw master and BXT. The product is not the sharpest image of the three; it
is the only one whose every pixel is a stated linear functional of the
data.

## Was Hubble necessary?

For the *deconvolution*: no — and noticing this is worth a section. The
shipped product consumed only the frame's own stars (the Moffat PSF), a
declared target, and a regularizer chosen by the delivered-PSF contract,
itself checked on the frame's own stars. Not one HST pixel enters the
operator. What the Hubble overlap provided is **certification** — three
claims the deconvolution could not make about itself:

1. that the star-derived PSF applies to extended structure at all (the
   S II channel proved it: extended-structure kernel 1.92″ against a
   stellar 1.96″ — textbook-true in principle, *measured* here);
2. that the operator does not distort what it sharpens (the held-out
   fidelity tie), including the negative certificate that a 1.5″ target
   is unsupported by the data;
3. the star-versus-nebula asymmetry of neural deconvolution, unknowable
   without ground truth by definition.

The right mental model is instrument calibration: the method was taken to
the standard once, certified, and now transfers to any field with a few
hundred usable stars — no HST overlap required. The certificate covers
this optical system and processing chain; a materially changed setup
deserves a re-issue, and the fit/validate framework for doing so is in
the repository.

That transferability is why the two certified instruments now ship inside
NebulaScope itself, as native C++ ports validated against these scripts:
**Tools ▸ Measure PSF** (the star fitter, field map included) and
**Tools ▸ Deconvolve to Target PSF** (the MCS filter with contract-first
λ, saturated-core protection, and the delivered-PSF audit) — see the
manual, and `deconv` in the scripting reference. The delivered-PSF
contract itself runs in the test suite on a synthetic field with exact
truth, so the promise this section describes is enforced by CI.

**A postscript in Gaia's hand.** The delivered PSF can also be checked
against an external astrometric standard. The pair Gaia DR3
4146599613565336064 / 4146599613565335808 is separated by 2.08″ — a
ratio of 1.06 to the delivered 1.96″ FWHM. Two equal round Gaussians
first show a saddle between their peaks at a separation of
$2\sigma \approx 0.85\,\mathrm{FWHM}$, so theory predicts a shallow
notch, made shallower by the pair's unequal flux; the deconvolved frame
shows exactly that marginal split, where the raw frame — 2″-class and
elongated besides — blends the pair into one oval. A resolution
certificate signed by a spacecraft that never saw the image.

## The noise, and a denoiser inside the inversion

The linear filter's noise behaviour is a stated property: wherever
$|\mathrm{OTF}_k|$ has fallen below the target's, the quotient amplifies
that band — signal and photon noise alike — by a known factor, and λ is
exactly the knob that caps it (this is also why an honest 1.5″ target is
unsupported by these data). Side-by-side with a neural product, the
difference shows as grain: the network's smoothness is its prior acting
as a denoiser, a judgment baked invisibly into the pixels.

The wrong fix is denoising *before* deconvolving. A linear denoiser
commutes with the filter and merely re-derives λ — the Wiener quotient
already **is** the jointly optimal linear denoise-and-deconvolve. A
nonlinear denoiser is worse than redundant: it replaces well-characterized
photon noise with signal-correlated residual error of unknown statistics,
which the inversion then amplifies into structured artifacts, while
anything the denoiser smoothed away becomes unrecoverable.

The principled construction puts the denoiser *inside* the inversion:
plug-and-play priors (Venkatakrishnan, Bouman & Wohlberg 2013) and, in
the form shipped here, **Regularization by Denoising** (Romano, Elad &
Milanfar 2017). The RED fixed-point iteration alternates a denoising step
with a Fourier-diagonal data-consistency solve,

$$X^{(k+1)} = \frac{\overline{\mathrm{OTF}_k}\,Y
  + \mu\,\mathcal{F}\!\left[D(x^{(k)})\right]}
  {|\mathrm{OTF}_k|^2 + \mu},$$

warm-started at the pure-MCS solution; the declared target is applied by
one final convolution, so the partial kernel is still never formed. The
denoiser $D$ is a starlet (à-trous B3-spline) soft-threshold with
per-scale MAD noise estimates — the classical astronomical sparsity
prior, a *declared assumption* rather than learned weights. The prior
weight μ plays λ's role and obeys the same contract-first ladder, and the
delivered-PSF audit is untouched: it measures the product and does not
care how the product was made. What is honestly surrendered is the
"stated linear functional" property — the header records the full
variational model instead — plus a footnote of rigour: the RED gradient
identity does not strictly hold for real denoisers (Reehorst & Schniter
2019), so the iteration is best read as a well-behaved algorithm in its
own right; with a nonexpansive threshold it converges in practice, and
the test suite asserts the claim that matters — at equal delivered PSF,
the RED background variance comes in well under the pure filter's.

## Future work: a spatially-variant PSF

This field earned a single kernel per channel — stellar FWHM uniform to
~5 % across 2°, eccentricity axis constant — but that is the exception
among amateur optical trains, not the rule. Fast Newtonians, reduced
refractors and imperfectly spaced flatteners show coma and astigmatism
that grow radially: the PSF is then a *field* of kernels, typically well
parameterized as elliptical Moffat parameters varying smoothly with
radius (and the elongation axis rotating tangentially, coma's signature).
The measurement side already exists: the star fitter's per-zone field map
*is* the sampled PSF field. The natural extension of the present method:

- fit the Moffat parameters as low-order functions of field position
  (radial polynomials suffice for centred aberrations);
- deconvolve by **overlapping tiles**, each with its locally interpolated
  kernel through the same MCS one-filter transform, cross-faded in the
  overlaps — the classic interpolated-PSF decomposition of Nagy &
  O'Leary (1998), of which efficient filter flow (Hirsch et al. 2010) is
  the modern refinement;
- keep the contract audit per zone: the delivered stellar FWHM map of the
  output must be flat at the declared target, which turns "the corners
  are soft" from an aesthetic complaint into a testable claim.

Nothing about the certification argument changes: the field's own stars
supply the PSF field, and ground truth remains necessary only the first
time, to certify the machinery.

## Lessons, collected

- The unverified link in a calibration chain is where the error is; verify
  same-source, sub-pixel, or not at all.
- Never compare a stretched measurement to a linear one; the stretch bias
  is large (2×) and non-uniform.
- A fit that silently returns "no improvement" for a whole parameter looks
  identical to a hard problem (this study found two such bugs — a search
  domain and a kernel quotient — each masquerading as physics).
- Registration failures under large unknown transforms are best solved by
  exhaustive search over the group, not by better correspondence
  heuristics; and parity is part of the group.
- Iterative deconvolution cannot promise a PSF; a linear filter can, and
  the promise can be *audited* by re-measuring stars on the product.
- Stellar analysis is measurement only where the stars are physics: raw
  linear data, and a calibrated deconvolution whose delivered PSF is
  declared and re-checked. A neural model's stars are prior-drawn —
  essentially cosmetic — and can neither certify optics nor arbitrate a
  model revision.
- Ground truth changes the epistemics: with a held-out half of a Hubble
  overlap, every processing claim — including a neural network's — becomes
  a measurement.

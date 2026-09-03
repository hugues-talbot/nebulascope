# PSF study toolkit

Research scripts behind the *Measuring a telescope against Hubble* appendix
([docs/PSF-STUDY.md](../../docs/PSF-STUDY.md)) — a case study measuring the
true PSF of a TEC 140 refractor on M16, auditing what BlurXTerminator does
to stars versus nebulosity against Hubble ground truth, and performing a
full-frame calibrated deconvolution with a stated model.

These are **case-study instruments, not product code**: constants such as
the pillars ROI, the comparison-image seam and the plate-scale chain are the
M16 study's values, kept so the appendix's numbers are reproducible. Adapt
them for another field.

| Script | What it does |
| --- | --- |
| `psf_pipeline.py <target> [<reference>]` | Display-space kernel between a stretched export and a reference: brute-force rotation×scale registration (parallel), star refinement, Wiener kernel, FWHM. Relative comparisons only — stretch space inflates FWHM. |
| `star_fwhm.py <master.fits> [<solved.xisf>]` | Linear stellar PSF: elliptical Moffat fits over thousands of stars; median FWHM/eccentricity/PA + a field map (drift vs optics diagnosis). |
| `linear_deconv.py [target_asec] [dump_dir]` | Linear extended-structure kernels vs the HST Heritage M16 mosaics, with a held-out fit/validate split — the honest BXT-on-nebulosity measurement. |
| `full_deconv.py [target_asec] [dump_dir]` | Full-frame calibrated deconvolution: measured elliptical-Moffat PSF in, declared circular Gaussian out, MCS one-filter transform, contract-first regularization, saturated-core protection, delivered-PSF verification. |
| `ml5_audit.py` | The linear_deconv protocol condensed to one comparative question: raw vs BXT ML4 vs BXT ML5, per channel — extended-structure kernel FWHM and held-out star-masked nebula fidelity, same registration and rectangles for all three. |

Set `PSF_DATA` to the folder holding the data (reference images, and a
`PSF_comparison/` subfolder with the linear masters and the HST mosaics);
it defaults to the working directory. Float32 FITS dumps of XISF masters
are produced with NebulaScope itself (`open …` / `save …` in a script).
Requires Python 3 with NumPy, SciPy and Pillow.
| `patch_extract.py` | Registered-sub patch extraction: `--locate` maps the study ROI into a new WBPP run's grid (same-source registration); extraction crops every sub into a compact float32 cube + manifest. Ships a minimal monolithic-XISF reader (uncompressed / zlib / zstd / lz4, +byteshuffle) validated bit-exact against the app's decoder. |
| `proper_coadd.py` | Proper coaddition (Zackay & Ofek 2017): per-sub Moffat PSFs (with residual registration offsets as kernel phases), 1/sigma^2 x transparency weights, matched-filter Fourier accumulation, declared-target output. `--selftest` synthesizes 40 subs with random seeing and asserts it beats stack-then-deconvolve (it does, by 2x) and honours the delivered-PSF contract. |

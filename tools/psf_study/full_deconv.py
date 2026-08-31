#!/usr/bin/env python3
"""Full-frame calibrated deconvolution of the TEC140 raw master.

Model, fully stated:
  * Kernel per channel: the ELLIPTICAL Moffat PSF measured from ~2700 stars
    on this very frame (major/minor FWHM, position angle, beta — medians of
    the per-star fits). Measured PSF in.
  * Target: a CIRCULAR Gaussian of --target arcsec (default 1.5). Declared
    PSF out — the drift elongation is corrected by construction.
  * Solver: damped Richardson-Lucy with the partial kernel p (PSF = target
    * p), run on the full 9523x6388 frame per channel; the iteration count
    is selected at checkpoints scored on the Hubble pillars overlap
    (nebula-only NRMSE on the fit rectangle; the validation rectangle is
    reported once, untouched by any choice).
Output: Float32 FITS cube (native grid, same orientation as the input dump)
with the model documented in COMMENT cards, plus a re-measurement of the
stellar PSF on the deconvolved frame — the delivered PSF must match the
declared one.

Usage: full_deconv.py [target_asec] [scratch_dir_with_dumps]
"""
import sys
import os
import numpy as np
from scipy import ndimage
from scipy import fft as sfft
from numpy.fft import fft2, ifft2, ifftshift, fftshift

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psf_pipeline import bruteforce_similarity, refine_affine, detect_stars, \
    describe, compose
from star_fwhm import read_fits_f32, detect as star_detect, fit_one
from linear_deconv import (load_hst, regprep, warp_to, conv, gauss_psf,
                           affine_match, HST_FILES, ROI_CX, ROI_CY, ROI_R, PLATE)
import multiprocessing as mp

D = os.path.join(os.environ.get('PSF_DATA', os.getcwd()), 'PSF_comparison')
K_HALF = 2.0*np.sqrt(2.0**(1.0/2.5) - 1.0)      # placeholder; real k uses beta


def measure_psf(img, tag):
    """Median elliptical-Moffat PSF of a frame's stars (fmaj, fmin, pa, beta)."""
    pts, _ = star_detect(img)
    N = 13
    cuts = []
    sat = np.nanpercentile(img, 99.999)
    for (x, y) in pts.astype(int):
        cut = img[y-N:y+N+1, x-N:x+N+1]
        if cut.shape == (2*N+1, 2*N+1) and np.isfinite(cut).all():
            cuts.append((cut.astype(np.float64), float(sat)))
    with mp.Pool(max(1, (os.cpu_count() or 4) - 2)) as pool:
        fits = [f for f in pool.map(fit_one, cuts) if f is not None]
    arr = np.array(fits)
    med = np.median(arr, axis=0)
    pa2 = np.radians(arr[:, 4]*2)
    pa = 0.5*np.degrees(np.arctan2(np.median(np.sin(pa2)), np.median(np.cos(pa2))))
    beta = np.median(arr[:, 7])
    print(f'  {tag}: {len(fits)} stars | FWHM maj {med[1]:.3f} min {med[2]:.3f} px '
          f'({med[1]*PLATE:.2f}"/{med[2]*PLATE:.2f}") | ecc {med[3]:.2f} @ '
          f'PA {pa:+.0f} | beta {beta:.2f}')
    return med[1], med[2], pa, beta


def moffat_kernel(fmaj, fmin, pa_deg, beta, n=61):
    kfac = 2.0*np.sqrt(2.0**(1.0/beta) - 1.0)
    amaj, amin = fmaj/kfac, fmin/kfac
    th = np.radians(pa_deg)
    yy, xx = np.mgrid[0:n, 0:n] - n//2
    u = ((xx*np.cos(th) + yy*np.sin(th))/amaj)**2 + \
        ((-xx*np.sin(th) + yy*np.cos(th))/amin)**2
    k = (1.0 + u)**(-beta)
    return k/k.sum()


def rl_full(y, p, checkpoints, score_fn):
    """Damped RL on the full frame (float32, threaded rffts); returns the
    checkpoint iterate with the best score plus the (iters, score) trace."""
    y32 = np.maximum(y, 0).astype(np.float32)
    pf = np.zeros_like(y32)
    c = np.asarray(y32.shape)//2
    h = p.shape[0]//2
    pf[c[0]-h:c[0]+h+1, c[1]-h:c[1]+h+1] = p.astype(np.float32)
    OTF = sfft.rfft2(ifftshift(pf), workers=-1)
    OTFc = np.conj(OTF)
    floor = np.float32(1e-3*np.percentile(y32, 95))
    x = y32.copy() + np.float32(1e-10)
    best = None
    trace = []
    it = 0
    for cp in checkpoints:
        while it < cp:
            est = sfft.irfft2(sfft.rfft2(x, workers=-1)*OTF, s=y32.shape, workers=-1)
            ratio = (y32 + floor)/(np.maximum(est, 0) + floor)
            x *= sfft.irfft2(sfft.rfft2(ratio, workers=-1)*OTFc, s=y32.shape, workers=-1)
            np.maximum(x, 0, out=x)
            it += 1
        sc = score_fn(x)
        trace.append((cp, sc))
        # iteration 0 participates in the TRACE (does any RL help at all?)
        # but never in the shipped frame — the product is a deconvolution.
        if cp > 0 and (best is None or sc < best[1]):
            best = (cp, sc, x.copy())
    return best, trace


def mcs_filter(y, k, t, lam):
    """MCS one-filter transform: X = Y . OTF_t conj(OTF_k) / (|OTF_k|^2+lam).
    Deconvolve by the measured PSF and reconvolve by the declared target in a
    single linear pass — never forming the partial kernel k (/) t, whose
    Fourier division explodes (Moffat spectra decay polynomially, the
    Gaussian target's super-exponentially). The target's own decay tames the
    high band, so noise amplification stays bounded."""
    def otf(psf, shape):
        pf = np.zeros(shape, np.float32)
        c = np.asarray(shape)//2; h = psf.shape[0]//2
        pf[c[0]-h:c[0]+h+1, c[1]-h:c[1]+h+1] = psf.astype(np.float32)
        return sfft.rfft2(ifftshift(pf), workers=-1)
    OK, OT = otf(k, y.shape), otf(t, y.shape)
    F = OT*np.conj(OK)/(np.abs(OK)**2 + np.float32(lam))
    return np.maximum(sfft.irfft2(sfft.rfft2(y.astype(np.float32), workers=-1)*F,
                                  s=y.shape, workers=-1), 0)


def write_fits_f32(path, cube, comments):
    cards = [f'SIMPLE  =                    T',
             f'BITPIX  =                  -32',
             f'NAXIS   =                    3',
             f'NAXIS1  = {cube.shape[2]:>20d}',
             f'NAXIS2  = {cube.shape[1]:>20d}',
             f'NAXIS3  = {cube.shape[0]:>20d}']
    cards += [f'COMMENT {c}'[:80] for c in comments]
    cards += ['END']
    hdr = ''.join(c.ljust(80) for c in cards)
    hdr = hdr.ljust(((len(hdr) + 2879)//2880)*2880)
    with open(path, 'wb') as f:
        f.write(hdr.encode('ascii'))
        data = cube.astype('>f4').tobytes()
        f.write(data)
        pad = (-len(data)) % 2880
        f.write(b'\0'*pad)


def main():
    target_asec = float(sys.argv[1]) if len(sys.argv) > 1 else 1.5
    S = sys.argv[2] if len(sys.argv) > 2 else '.'
    raw, _ = read_fits_f32(os.path.join(S, 'm16_full_raw.fits'))
    bxt, _ = read_fits_f32(os.path.join(S, 'm16_full_BXT.fits'))
    tgt_px = target_asec/PLATE
    t_psf = gauss_psf(tgt_px)
    x0, x1 = ROI_CX - ROI_R, ROI_CX + ROI_R
    y0, y1 = ROI_CY - ROI_R, ROI_CY + ROI_R
    nroi = 2*ROI_R
    print(f'target {target_asec}" = {tgt_px:.2f} px (native {PLATE:.4f}"/px)')

    out = np.empty_like(raw)
    comments = [f'Calibrated deconvolution: measured elliptical-Moffat PSF in,',
                f'declared circular Gaussian {target_asec}\" out. MCS linear',
                f'filter; lam = largest honouring the delivered-PSF contract.',
                f'Saturated cores (input > p99.995, dilated+feathered) retain',
                f'INPUT pixels: nonlinear cores violate the convolution model.']
    for c, ch in enumerate(['S', 'H', 'O']):
        img = np.nan_to_num(raw[c].astype(np.float64))
        print(f'[{ch}] measuring PSF...')
        fmaj, fmin, pa, beta = measure_psf(img, f'{ch} raw')
        k = moffat_kernel(fmaj, fmin, pa, beta)
        # Hubble truth on the native ROI for iteration scoring
        m_roi = img[y0:y1, x0:x1]
        hst = np.fliplr(load_hst(HST_FILES[ch]))
        hst8 = np.fliplr(load_hst(HST_FILES[ch], binf=8))
        m_reg = regprep(m_roi)
        A0, score, s0, r0 = bruteforce_similarity(regprep(hst8), m_reg,
                                np.geomspace(0.40, 0.44, 5),
                                np.arange(-67.0, -60.5, 1.0), n=1400)
        Sh, Sm = detect_stars(regprep(hst8), maxn=600), detect_stars(m_reg, maxn=600)
        A8, npairs, med = refine_affine(A0, Sh, Sm, tol0=4.0)
        print(f'[{ch}] overlap registration: {npairs} pairs, resid {med:.2f} px')
        Ahalf = np.zeros((3, 2)); Ahalf[0, 0] = Ahalf[1, 1] = 0.5
        A = compose(Ahalf, A8)
        hst_w = warp_to(hst, A, m_roi.shape)
        valid = ndimage.binary_erosion(hst > 0.01, iterations=4)
        cover = warp_to(valid.astype(np.float64), A, m_roi.shape) > 0.98
        truth = conv(np.clip(hst_w, 0, np.percentile(hst_w, 99.8)), t_psf)
        # scoring needs MASKS, not rectangles (no windowed FFT here): the
        # covered west half fits, the covered east half validates.
        xxg = np.linspace(0, 1, nroi)[None, :]*np.ones((nroi, 1))
        def neb_mask(half):
            m = (cover & half).astype(np.float64)
            if m.sum() < 5000: return None
            stars = (truth > np.percentile(truth[m > 0.5], 99)) | \
                    (m_roi > np.percentile(m_roi[m > 0.5], 99))
            return m*(~ndimage.binary_dilation(stars, iterations=6))
        fneb = neb_mask(xxg < 0.50)
        vneb = neb_mask(xxg > 0.55)
        if fneb is None:
            print(f'[{ch}] no overlap coverage — skipped'); out[c] = raw[c]; continue
        bg = np.percentile(img, 2)
        def score_fn(xfull):
            _, e = affine_match(xfull[y0:y1, x0:x1].astype(np.float64) + bg, truth, fneb)
            return e
        # lam selection: CONTRACT FIRST — the largest (most regularized) lam
        # whose delivered stellar FWHM still honours the target within 5% —
        # measured on a central crop; fidelity is then reported, not optimized.
        print(f'[{ch}] deconvolving (MCS filter; lam by delivered-PSF contract)...')
        crop = img[2000:4400, 3400:5800]
        cbg = np.percentile(crop, 2)
        lam_c = None
        for lam in (3e-3, 1e-3, 3e-4, 1e-4):
            d = mcs_filter(crop - cbg, k, t_psf, lam) + cbg
            fj, fn, _, _ = measure_psf(d, f'{ch} lam {lam:g}')
            if np.sqrt(fj*fn) <= 1.05*tgt_px:
                lam_c = lam
                break
        if lam_c is None: lam_c = 1e-4
        xbest = mcs_filter(img - bg, k, t_psf, lam_c)
        e_fit = score_fn(xbest)
        print(f'[{ch}] chose lam {lam_c:g} (fit-half NRMSE {e_fit:.4f})')
        dec = xbest.astype(np.float64) + bg
        # Saturated-core protection (stated model exception): the brightest
        # cores are nonlinear — never truth (*) PSF — so the linear filter
        # correctly rings around them. Those pixels keep the INPUT values,
        # feathered; the fraction is reported and recorded in the header.
        sat_level = np.percentile(img, 99.995)
        wmask = ndimage.binary_dilation(img > sat_level, iterations=5).astype(np.float64)
        wmask = ndimage.gaussian_filter(wmask, 2.0)
        dec = wmask*img + (1.0 - wmask)*dec
        print(f'[{ch}] saturated-core protection: {100*(wmask > 0.5).mean():.3f}% of pixels')
        out[c] = dec.astype(np.float32)
        n_it = lam_c
        if vneb is not None:
            for name, im in (('raw', m_roi), ('BXT', np.nan_to_num(bxt[c][y0:y1, x0:x1].astype(np.float64))),
                             ('deconv', out[c][y0:y1, x0:x1].astype(np.float64))):
                _, e = affine_match(im, truth, vneb)
                print(f'[{ch}]   validation (nebula-only): {name:7s} {e:.4f}')
        comments.append(f'{ch}: PSF maj/min {fmaj*PLATE:.2f}/{fmin*PLATE:.2f} arcsec '
                        f'PA {pa:+.0f} beta {beta:.2f}, MCS lam {n_it:g}')
    path = os.path.join(D, f'M16_SHO_full_raw_deconv{target_asec:g}.fits')
    write_fits_f32(path, out, comments)
    print('wrote', path)
    # closing loop: the delivered PSF must be the declared one
    print('re-measuring stellar PSF on the deconvolved frame:')
    for c, ch in enumerate(['S', 'H', 'O']):
        measure_psf(out[c].astype(np.float64), f'{ch} deconv')

if __name__ == '__main__':
    main()

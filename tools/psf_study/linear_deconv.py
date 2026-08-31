#!/usr/bin/env python3
"""Linear-space extended-structure kernels + calibrated deconvolution vs BXT.

Ground truth: the Hubble Heritage WFC3/UVIS M16 mosaics (F673N/F657N/F502N,
~0.040\"/px, linear e-/s), filter-matched to the user's S/H/O channels.

Stages (all in LINEAR flux space, on a 2x-upsampled master grid, 0.382\"/px,
so a ~1.3\" target PSF stays Nyquist-sampled):
  1. Bin each HST mosaic x4 and register it to the upsampled master ROI
     (brute-force rot x scale seed + star similarity refinement — the same
     machinery validated on the display-space study).
  2. Wiener kernel per channel for BOTH masters (raw and BXT), estimated on
     the WEST half of the overlap only; FWHM of these kernels is the honest
     extended-structure resolution, and raw-vs-BXT the honest BXT gain.
  3. MCS-style calibrated deconvolution of the raw master: solve for the
     image AT A STATED TARGET PSF (Gaussian, --target arcsec) by damped
     Richardson-Lucy with the partial kernel p where k = t * p.
  4. Validation on the EAST half (never seen by the kernel fit): per-channel
     affine intensity match, then RMSE against HST-degraded-to-target, for
     raw / BXT / calibrated deconvolution.
Outputs: metrics on stdout, kernels PNG, and a triptych PNG per channel.
"""
import sys
import os
import re
import multiprocessing as mp
import numpy as np
from scipy import ndimage
from numpy.fft import fft2, ifft2, fftshift, ifftshift
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psf_pipeline import bruteforce_similarity, refine_affine, detect_stars, \
    sim_from_pairs, apply_aff, describe, hann2, bandpass, compose
from star_fwhm import read_fits_f32

# Site configuration: PSF_DATA points at the study data folder; the HST
# mosaics and outputs live in its PSF_comparison subdirectory.
D = os.path.join(os.environ.get('PSF_DATA', os.getcwd()), 'PSF_comparison')
PLATE = 0.76371                    # master, arcsec/px (PI solution)
GRID = PLATE / 2.0                 # working grid: 2x upsampled master
HST_FILES = { 'S': 'hlsp_heritage_hst_wfc3-uvis_m16_f673n_v1_drz.fits',
              'H': 'hlsp_heritage_hst_wfc3-uvis_m16_f657n_v1_drz.fits',
              'O': 'hlsp_heritage_hst_wfc3-uvis_m16_f502n_v1_drz.fits' }
ROI_CX, ROI_CY, ROI_R = 4580, 3120, 260     # master px: pillars-centred ROI

def load_hst(name, binf=4):
    cube, cards = read_fits_f32(os.path.join(D, name))
    img = cube[0].astype(np.float64)
    # Drizzle edge artifacts reach 1e33 and large negatives — sanitize hard
    # before anything sees the data.
    img = np.nan_to_num(img, nan=0.0, posinf=0.0, neginf=0.0)
    finite = img[np.abs(img) < 1e10]
    top = np.percentile(finite, 99.999)
    img = np.clip(img, 0.0, top)
    H, W = img.shape
    H2, W2 = (H//binf)*binf, (W//binf)*binf
    return img[:H2, :W2].reshape(H2//binf, binf, W2//binf, binf).mean(axis=(1, 3))

def regprep(a):
    """Registration-space compression: HST star cores reach 1e6 x the median
    and would own the band-passed spectrum; asinh tames them while keeping
    the extended structure the correlation actually needs."""
    scale = max(1e-12, np.percentile(a[a > 0], 90) if (a > 0).any() else 1.0)
    return np.arcsinh(a / scale)

def warp_to(ref, A, shape):
    M = np.vstack([A.T, [0, 0, 1]])
    Minv = np.linalg.inv(M)
    yy, xx = np.mgrid[0:shape[0], 0:shape[1]]
    src = Minv @ np.stack([xx.ravel(), yy.ravel(), np.ones(xx.size)])
    return ndimage.map_coordinates(ref, [src[1].reshape(shape), src[0].reshape(shape)],
                                   order=3, mode='constant')

def inscribed_rect(valid, xlo, xhi):
    """Largest axis-aligned all-valid rect within columns [xlo, xhi): shrink
    the region bbox about its centroid (integral-image containment test)."""
    v = valid.copy(); v[:, :xlo] = False; v[:, xhi:] = False
    ys, xs = np.nonzero(v)
    if len(ys) < 100: return None
    cy, cx = ys.mean(), xs.mean()
    bh, bw = ys.max()-ys.min(), xs.max()-xs.min()
    ii = np.pad(np.cumsum(np.cumsum(~v, 0), 1), ((1, 0), (1, 0)))
    def bad(y0, y1, x0, x1):
        return ii[y1, x1] - ii[y0, x1] - ii[y1, x0] + ii[y0, x0]
    def fits(sc):
        y0, y1 = int(cy - sc*bh/2), int(cy + sc*bh/2)
        x0, x1 = int(cx - sc*bw/2), int(cx + sc*bw/2)
        if y0 < 0 or x0 < 0 or y1 > v.shape[0] or x1 > v.shape[1] or y1 <= y0 or x1 <= x0:
            return False
        return bad(y0, y1, x0, x1) == 0
    lo_s, hi_s = 0.0, 1.0
    if not fits(0.02): return None                 # even a sliver won't fit
    for _ in range(24):
        m = 0.5*(lo_s + hi_s)
        if fits(m): lo_s = m
        else: hi_s = m
    y0, y1 = int(cy - lo_s*bh/2), int(cy + lo_s*bh/2)
    x0, x1 = int(cx - lo_s*bw/2), int(cx + lo_s*bw/2)
    if y1 - y0 < 64 or x1 - x0 < 64: return None   # too small to be useful
    return y0, y1, x0, x1

def wiener_kernel_lin(h, t, rect, lam_rel=1e-3, ksz=61):
    """Wiener kernel on an all-covered rectangle. Both sides are BAND-PASSED
    first: linear images concentrate power at DC (nebulosity), and a lam
    anchored to |F|max over-regularizes everything else — the kernel then
    degenerates into a low-frequency cross-correlation several arcsec wide.
    Removing the >25 px scales equalizes the spectrum (the ratio k = T/H is
    unchanged where the band-pass is nonzero)."""
    y0, y1, x0, x1 = rect
    hc, tc = h[y0:y1, x0:x1], t[y0:y1, x0:x1]
    w = hann2(hc.shape)
    hb = (hc - ndimage.gaussian_filter(hc, 25)) * w
    tb = (tc - ndimage.gaussian_filter(tc, 25)) * w
    Fh, Ft = fft2(hb), fft2(tb)
    lam = lam_rel * np.abs(Fh).max()**2
    K = np.real(ifft2(np.conj(Fh)*Ft/(np.abs(Fh)**2 + lam)))
    K = fftshift(K)
    c = np.asarray(K.shape)//2
    k = K[c[0]-ksz//2:c[0]+ksz//2+1, c[1]-ksz//2:c[1]+ksz//2+1].copy()
    k = np.clip(k - np.median(K), 0, None)
    ksum = k.sum()
    if not np.isfinite(ksum) or ksum <= 0:
        raise RuntimeError('degenerate kernel — check registration/inputs')
    return k / ksum

def fwhm_area(k, zoom=8):
    kz = ndimage.zoom(k, zoom, order=3)
    kz = np.clip(kz - np.median(kz), 0, None)
    return 2.0*np.sqrt(((kz >= 0.5*kz.max()).sum()/zoom**2)/np.pi)

def gauss_psf(fwhm_px, n=61):
    sig = fwhm_px/2.3548
    yy, xx = np.mgrid[0:n, 0:n] - n//2
    g = np.exp(-(xx**2 + yy**2)/(2*sig**2))
    return g/g.sum()

def conv(img, k):
    pk = np.zeros_like(img)
    c = np.asarray(pk.shape)//2
    h = k.shape[0]//2
    pk[c[0]-h:c[0]+h+1, c[1]-h:c[1]+h+1] = k
    return np.real(ifft2(fft2(img)*fft2(ifftshift(pk))))

def partial_kernel(k, t, eps_rel=1e-3, ksz=61):
    """p with k ~= t * p (Fourier division, regularized). Both PSFs centred
    in the frame and ifftshift'ed, so p comes back centred — a miscentred p
    turns every RL-deconvolved star into a dipole."""
    n = 256; c = n//2
    K = np.zeros((n, n)); h = k.shape[0]//2
    K[c-h:c+h+1, c-h:c+h+1] = k
    T = np.zeros((n, n)); ht = t.shape[0]//2
    T[c-ht:c+ht+1, c-ht:c+ht+1] = t
    FK, FT = fft2(ifftshift(K)), fft2(ifftshift(T))
    eps = eps_rel * np.abs(FT).max()**2
    P = np.real(fftshift(ifft2(FK*np.conj(FT)/(np.abs(FT)**2 + eps))))
    p = np.clip(P[c-ksz//2:c+ksz//2+1, c-ksz//2:c+ksz//2+1], 0, None)
    return p / p.sum()

def rl_deconv(y, p, iters=80, damp=1e-3):
    """Damped Richardson-Lucy; y >= 0, background-subtracted."""
    x = np.maximum(y, 0) + 1e-12
    pf = np.zeros_like(y); c = np.asarray(y.shape)//2; h = p.shape[0]//2
    pf[c[0]-h:c[0]+h+1, c[1]-h:c[1]+h+1] = p
    P = fft2(ifftshift(pf)); Pc = np.conj(P)
    floor = damp * np.percentile(y, 95)
    for _ in range(iters):
        est = np.real(ifft2(fft2(x)*P))
        ratio = (np.maximum(y, 0) + floor) / (np.maximum(est, 0) + floor)
        x = x * np.real(ifft2(fft2(ratio)*Pc))
        x = np.maximum(x, 0)
    return x

def affine_match(a, b, mask):
    """gain,offset minimizing ||g*a+o - b|| on mask; returns matched a and rmse."""
    av, bv = a[mask > 0.5], b[mask > 0.5]
    A = np.stack([av, np.ones_like(av)], 1)
    sol, *_ = np.linalg.lstsq(A, bv, rcond=None)
    resid = A @ sol - bv
    return sol[0]*a + sol[1], np.sqrt(np.mean(resid**2))/max(1e-12, bv.std())

def show(img, lo=1, hi=99.7):
    a, b = np.percentile(img, [lo, hi])
    x = np.clip((img - a)/(b - a + 1e-12), 0, 1)
    return (255*np.sqrt(x)).astype(np.uint8)      # gamma-ish display, same for all

def main():
    target_asec = float(sys.argv[1]) if len(sys.argv) > 1 else 1.3
    S = sys.argv[2] if len(sys.argv) > 2 else '.'
    raw, _ = read_fits_f32(os.path.join(S, 'm16_full_raw.fits'))
    bxt, _ = read_fits_f32(os.path.join(S, 'm16_full_BXT.fits'))
    x0, x1 = ROI_CX - ROI_R, ROI_CX + ROI_R
    y0, y1 = ROI_CY - ROI_R, ROI_CY + ROI_R
    n2 = 4*ROI_R                                   # 2x-upsampled ROI side
    tgt_px = target_asec / GRID
    t_psf = gauss_psf(tgt_px)
    print(f'ROI master[{x0}:{x1},{y0}:{y1}] -> {n2}x{n2} @ {GRID:.4f}\"/px; '
          f'target {target_asec}\" = {tgt_px:.2f} px')
    # validation split: WEST half fits the kernels, EAST half validates —
    # each as the largest all-covered rectangle inside its half.

    for c, ch in enumerate(['S', 'H', 'O']):
        up = lambda img: ndimage.zoom(img[y0:y1, x0:x1].astype(np.float64), 2, order=3)
        m_raw, m_bxt = up(raw[c]), up(bxt[c])
        hst = load_hst(HST_FILES[ch])              # bin 4: 0.160 "/px
        hst8 = load_hst(HST_FILES[ch], binf=8)     # bin 8 for registration (fits 2048)
        m_reg = regprep(m_raw)
        # PARITY is unknown a priori (the HST CD determinant is negative, the
        # master's positive, but the dump chain's row order enters too):
        # try both mirror states, keep the better correlation.
        best = None
        for flip in (False, True):
            cand = np.fliplr(hst8) if flip else hst8
            A0, score, s0, r0 = bruteforce_similarity(regprep(cand), m_reg,
                                    np.geomspace(0.70, 1.00, 10), range(0, 360, 4), n=2048)
            if best is None or score > best[1]:
                best = (flip, score, s0, r0)
        flip, score, s0, r0 = best
        if flip:
            hst8 = np.fliplr(hst8); hst = np.fliplr(hst)
        print(f'[{ch}] parity: {"flipped" if flip else "direct"} (score {score:.3f})')
        A0, score, s0, r0 = bruteforce_similarity(regprep(hst8), m_reg,
                                np.geomspace(s0*0.94, s0*1.06, 7),
                                np.arange(r0-3, r0+3.5, 1.0), n=2048)
        Sh, Sm = detect_stars(regprep(hst8), maxn=600), detect_stars(m_reg, maxn=600)
        A8, npairs, med = refine_affine(A0, Sh, Sm, tol0=5.0)
        print(f'[{ch}] register: score {score:.3f}, {npairs} pairs, resid {med:.2f} px', end='; ')
        describe(A8, 'hst8->roi')
        # bin-4 coords are 2x bin-8 coords: compose the halving in front
        Ahalf = np.zeros((3, 2)); Ahalf[0, 0] = Ahalf[1, 1] = 0.5
        A = compose(Ahalf, A8)
        hst_w = warp_to(hst, A, m_raw.shape)
        # HST coverage in ROI coords: the mosaic's footprint, eroded off seams
        valid8 = ndimage.binary_erosion(hst > 0.01, iterations=4)
        cover = warp_to(valid8.astype(np.float64), A, m_raw.shape) > 0.98
        rect_fit = inscribed_rect(cover, 0, n2//2)
        rect_val = inscribed_rect(cover, n2//2 + n2//20, n2)
        if rect_fit is None or rect_val is None:
            print(f'[{ch}] insufficient coverage'); continue
        print(f'[{ch}] fit rect {rect_fit}, val rect {rect_val}')
        # star cores are saturated/nonlinear on both sides — clip them out of
        # the KERNEL estimate (tiny area, extended structure untouched).
        hst_w = np.clip(hst_w, 0, np.percentile(hst_w, 99.8))
        m_raw_k = np.clip(m_raw, 0, np.percentile(m_raw, 99.8))
        m_bxt_k = np.clip(m_bxt, 0, np.percentile(m_bxt, 99.8))
        # kernels: raw and BXT, fit half only
        k_raw = wiener_kernel_lin(hst_w, m_raw_k, rect_fit)
        k_bxt = wiener_kernel_lin(hst_w, m_bxt_k, rect_fit)
        f_raw, f_bxt = fwhm_area(k_raw)*GRID, fwhm_area(k_bxt)*GRID
        print(f'[{ch}] EXTENDED-STRUCTURE kernel FWHM: raw {f_raw:.2f}\", BXT {f_bxt:.2f}\"')
        # calibrated deconvolution of raw to the target PSF. The iteration
        # count is RL's one knob (as BXT's strength is its knob): select it on
        # the FIT half — the validation half never votes.
        p = partial_kernel(k_raw, t_psf)
        bg = np.percentile(m_raw, 2)
        truth_fit = conv(hst_w, t_psf)
        fy0, fy1, fx0, fx1 = rect_fit
        fm = np.zeros_like(m_raw); fm[fy0:fy1, fx0:fx1] = 1.0
        sm_f = (truth_fit > np.percentile(truth_fit[fm > 0.5], 99)) | \
               (m_raw > np.percentile(m_raw[fm > 0.5], 99))
        fneb = fm * (~ndimage.binary_dilation(sm_f, iterations=6))
        best = None
        for iters in (10, 20, 40, 80):
            x = rl_deconv(m_raw - bg, p, iters=iters)
            _, e_fit = affine_match(x + bg, truth_fit, fneb)
            if best is None or e_fit < best[1]:
                best = (iters, e_fit, x + bg)
        n_it, e_fit, dec = best
        print(f'[{ch}] RL iterations selected on fit half: {n_it} (fit NRMSE {e_fit:.4f})')
        # ground truth at target resolution; score on validation half
        truth = conv(hst_w, t_psf)
        vy0, vy1, vx0, vx1 = rect_val
        vmask = np.zeros_like(truth); vmask[vy0:vy1, vx0:vx1] = 1.0
        # star-masked variant: the extended-structure verdict must not be
        # decided by star profiles (clipped on one side, PSF-dependent on the
        # other) — mask the bright tails of truth AND raw, dilated.
        smask = (truth > np.percentile(truth[vmask > 0.5], 99)) | \
                (m_raw > np.percentile(m_raw[vmask > 0.5], 99))
        smask = ndimage.binary_dilation(smask, iterations=6)
        vneb = vmask * (~smask)
        out = { 'raw': m_raw, 'BXT': m_bxt, 'calib-deconv': dec }
        print(f'[{ch}] val NRMSE vs Hubble@{target_asec}\" (all / nebula-only):')
        for name, img in out.items():
            _, e_all = affine_match(img, truth, vmask)
            _, e_neb = affine_match(img, truth, vneb)
            print(f'    {name:14s} {e_all:.4f} / {e_neb:.4f}')
        # triptych + truth
        panels = [show(truth), show(m_raw), show(m_bxt), show(dec)]
        seam = np.full((n2, 6), 32, np.uint8)
        row = np.hstack(sum(([pnl, seam] for pnl in panels), [])[:-1])
        Image.fromarray(row).save(os.path.join(D, f'deconv_{ch}_triptych.png'))
        kk = np.hstack([ndimage.zoom(k/k.max(), 3, order=1) for k in (k_raw, k_bxt)])
        Image.fromarray((255*np.clip(kk, 0, 1)).astype(np.uint8)).save(
            os.path.join(D, f'kernels_{ch}.png'))
    print('panels: truth | raw | BXT | calibrated deconv  (per-channel PNGs in PSF_comparison)')

if __name__ == '__main__':
    main()

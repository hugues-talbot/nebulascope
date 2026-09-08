"""Hubble-overlap audit of PATCH-sized renders (the ninth-row referee).

ml5_audit.py generalized away from the full-master ROI: works on any
patch-grid images at the master plate scale (0.7637 arcsec/px), e.g. the
proper-coadd product, its plain-mean control, and a raw-master crop of
the same sky. Registers the HST mosaic onto the patch (wide-search
fallback for an unknown WBPP reference orientation), estimates the
extended-structure Wiener kernel on the west rectangle and star-masked
nebula NRMSE against Hubble degraded to 1.3 arcsec on the held-out east
rectangle — the same protocol, the same numbers, comparable with the
eight-way table.

    patch_audit.py --filter H [--nii] [--starless [--remover sxt|starnet|analytic|darkstar]]
                   [--borrow A=B,C=B] name=path ...

--nii (Hα): model Hubble's F657N truth as Hα + k·[S II] (F673N) to remove
the [N II] contribution the study's 3 nm filter does not see.

Each path: FITS with one plane (or first plane used). PSF_DATA must
point at the folder whose PSF_comparison/ holds the HST mosaics.

--borrow A=B: render A takes render B's Hubble registration instead of
its own. The registration is star-based; a STARLESS render (a starless
coadd) registers on residuals only, and a 0.7-px misregistration
inflates its fidelity score. All renders of one patch share the grid,
so a starless render borrows its starry sibling's solution.

--starless (metric v2): the fidelity residuals of v1 turned out to be
dominated by under-masked faint stars and Hubble's diffraction spikes
on faint, patch-sized regions (see PSF-STUDY.md, the ninth row). v2
removes stars SYMMETRICALLY on both sides with the RC-Astro
StarXTerminator CLI (`rc-astro sxt`), then scores starless-vs-starless
with small residual apertures taken from the two star images. The
kernel estimate is unchanged. The remover is a backend of
star_removers.py (--remover, default sxt): the referee itself can be
A/B'd across star-removal tools, including the prior-free analytic one.
"""
import sys, os
import numpy as np
from scipy import ndimage


def prepare_truth(truth, cover, seed=7):
    """Make the referee truth a fair input for a star remover: it is
    noiseless and drops to exactly zero at the mosaic's coverage edge,
    a hard step that every remover treats as structure (the analytic
    detector scallops it, the neural ones bead it). Continue the image
    beyond coverage by nearest-value extension, smoothed there, and add
    a noise floor at 1e-3 of the nebula peak — far below anything the
    metric resolves — so noise-calibrated tools work in their design
    regime. Returns (truth_in, inner) with `inner` the eroded coverage
    inside which the score is taken."""
    idx = ndimage.distance_transform_edt(~cover, return_distances=False, return_indices=True)
    ext = truth[tuple(idx)]
    ext = np.where(cover, truth, ndimage.gaussian_filter(ext, 8))
    rng = np.random.default_rng(seed)
    sigma = 2e-4*float(np.percentile(truth[cover], 99.9))
    truth_in = ext + rng.normal(0.0, sigma, truth.shape)
    inner = ndimage.binary_erosion(cover, iterations=24)
    return truth_in, inner, sigma


def star_discs(shape, detect_stars, *imgs_r_maxn):
    """Boolean mask of discs around the stars the study's detector finds
    in each (image, radius, maxn) triple; True inside a disc."""
    ap = np.zeros(shape, bool)
    yy, xx = np.mgrid[:shape[0], :shape[1]]
    for img, r, maxn in imgs_r_maxn:
        for x, y in detect_stars(img, nsig=6.0, box=9, maxn=maxn):
            y0, y1 = int(max(0, y - r - 1)), int(min(shape[0], y + r + 2))
            x0, x1 = int(max(0, x - r - 1)), int(min(shape[1], x + r + 2))
            ap[y0:y1, x0:x1] |= (yy[y0:y1, x0:x1] - y)**2 + (xx[y0:y1, x0:x1] - x)**2 <= r*r
    return ap


def star_apertures(m, truth_in, m_st, detect_stars, r_truth=8, r_render=10):
    """Residual apertures for metric v2, independent of the remover:
    discs at every star the study's detector finds in the truth and in
    the render (positions come from the images themselves, not from a
    remover's residual, so a remover that smooths the nebula is not
    'rewarded' by masking its own error), plus the cores the render's
    stars image still holds above 6 sigma (bright-star halos)."""
    def sig(a): return np.median(np.abs(a - np.median(a)))/0.6745 + 1e-12
    ap = np.zeros(m.shape, bool)
    yy, xx = np.mgrid[:m.shape[0], :m.shape[1]]
    for img, r, maxn in ((truth_in, r_truth, 4000), (m, r_render, 2000)):
        pts = detect_stars(img, nsig=6.0, box=9, maxn=maxn)
        for x, y in pts:
            y0, y1 = int(max(0, y - r - 1)), int(min(m.shape[0], y + r + 2))
            x0, x1 = int(max(0, x - r - 1)), int(min(m.shape[1], x + r + 2))
            ap[y0:y1, x0:x1] |= (yy[y0:y1, x0:x1] - y)**2 + (xx[y0:y1, x0:x1] - x)**2 <= r*r
    hp = m_st - ndimage.gaussian_filter(m_st, 6)
    ap |= ndimage.binary_dilation(hp > 6*sig(m), iterations=4)
    return ap


def main():
    sys.path.insert(0, os.environ.get('PSF_DATA', '.'))
    from psf_pipeline import bruteforce_similarity, refine_affine, detect_stars, compose
    from star_fwhm import read_fits_f32
    from star_removers import remove_stars
    from kernel_fit import moffat_kernel_fit
    from linear_deconv import (load_hst, regprep, warp_to, wiener_kernel_lin,
                               fwhm_area, inscribed_rect, conv, gauss_psf,
                               affine_match, HST_FILES, GRID)
    args = sys.argv[1:]
    ch = 'H'
    if '--filter' in args:
        i = args.index('--filter'); ch = args[i+1]; del args[i:i+2]
    starless = '--starless' in args
    if starless:
        args.remove('--starless')
    remover = 'sxt'
    if '--remover' in args:
        i = args.index('--remover'); remover = args[i+1]; del args[i:i+2]
    # --nii (Hα only): Hubble's F657N admits both [N II] lines, which the
    # study's 3 nm Hα filter excludes; the kernel estimator absorbs that
    # structural mismatch as blur (Hα kernels read ~3" against ~2" stars).
    # [N II] follows [S II] in this nebula, so the truth becomes a linear
    # combination F657N + k·F673N with k fitted at low resolution, where
    # PSF differences do not matter (expect k < 0).
    nii = '--nii' in args
    if nii:
        args.remove('--nii')
    borrow = {}
    if '--borrow' in args:
        i = args.index('--borrow')
        borrow = dict(kv.split('=', 1) for kv in args[i+1].split(','))
        del args[i:i+2]
    if not args:
        raise SystemExit(__doc__)
    workdir = os.path.join(os.environ.get('PSF_DATA', '.'), 'ninth_row', f'{remover}_work')
    inputs = []
    for a in args:
        name, path = a.split('=', 1)
        cube, _ = read_fits_f32(path)
        plane = np.asarray(cube, dtype=np.float64)
        plane = plane[0] if plane.ndim == 3 else plane
        inputs.append((name, np.nan_to_num(plane)))

    hst = np.fliplr(load_hst(HST_FILES[ch]))
    hst8 = np.fliplr(load_hst(HST_FILES[ch], binf=8))
    hst2 = np.fliplr(load_hst(HST_FILES['S'])) if (nii and ch == 'H') else None
    Sh = detect_stars(regprep(hst8), maxn=600)
    t_psf = gauss_psf(1.3/GRID)

    def register(m_reg):
        A0, score, s0, r0 = bruteforce_similarity(regprep(hst8), m_reg,
                                np.geomspace(0.80, 0.88, 5), np.arange(-67.0, -60.5, 1.0), n=2048)
        Sm = detect_stars(m_reg, maxn=600)
        A8, npairs, med = refine_affine(A0, Sh, Sm, tol0=4.0)
        if npairs < 20 or med > 1.0:
            A0, score, s0, r0 = bruteforce_similarity(regprep(hst8), m_reg,
                                    np.geomspace(0.70, 1.00, 10), range(0, 360, 4), n=2048)
            A0, score, s0, r0 = bruteforce_similarity(regprep(hst8), m_reg,
                                    np.geomspace(s0*0.94, s0*1.06, 7),
                                    np.arange(r0-3, r0+3.5, 1.0), n=2048)
            A8, npairs, med = refine_affine(A0, Sh, Sm, tol0=5.0)
        return A8, npairs, med

    regs = {}
    for name, plane in inputs:
        if name not in borrow:
            regs[name] = register(regprep(ndimage.zoom(plane, 2, order=3)))
    for name, src in borrow.items():
        if src not in regs:
            raise SystemExit(f'--borrow {name}={src}: {src} is not a registered input')
        regs[name] = regs[src]

    for name, plane in inputs:
        m = ndimage.zoom(plane, 2, order=3)
        n2 = m.shape[1]
        A8, npairs, med = regs[name]
        Ahalf = np.zeros((3, 2)); Ahalf[0, 0] = Ahalf[1, 1] = 0.5
        A = compose(Ahalf, A8)
        hst_w = warp_to(hst, A, m.shape)
        valid = ndimage.binary_erosion(hst > 0.01, iterations=4)
        cover = warp_to(valid.astype(np.float64), A, m.shape) > 0.98
        nii_note = ''
        if hst2 is not None:
            hst2_w = warp_to(hst2, A, m.shape)
            # Fit k on NEBULA only: stars dominate any low-passed starry
            # image and F673N's stars would "help" the match (k came out
            # +6 on a first try). Mask star discs found on both sides, use
            # normalized (masked) smoothing, and keep k in its physical
            # range: F657N = Hα + [N II], so removing [N II] means k ≤ 0.
            w = (~star_discs(m.shape, detect_stars, (m, 14, 2000), (hst_w, 10, 4000),
                             (hst2_w, 10, 4000))).astype(float)
            # At 8-px smoothing the two Hubble images are nearly collinear
            # (k swung from -1.5 to +1.2 between halves of the same sky), so
            # the fit works in a BAND-PASS at the render's own resolution:
            # Hubble degraded to the render's approximate PSF (from the
            # one-regressor kernel, ~2.9"), then scales 3–20 px on the 2x
            # grid, where the [N II] fronts differ from Hα.
            s_r = max(1.0, 0.85*fw_est/GRID/2.355) if 'fw_est' in dir() else 3.2
            wl = ndimage.gaussian_filter(w, 3)
            def bp(a, extra=0.0):
                if extra > 0:
                    a = ndimage.gaussian_filter(a, extra)
                aw = ndimage.gaussian_filter(a*w, 3)/(wl + 1e-9)
                return aw - ndimage.gaussian_filter(aw, 20)
            sel = ndimage.binary_erosion(cover, iterations=16) & (wl > 0.8)
            X = np.stack([bp(hst_w, s_r)[sel], bp(hst2_w, s_r)[sel], np.ones(sel.sum())], 1)
            sol, *_ = np.linalg.lstsq(X, bp(m)[sel], rcond=None)
            k = float(np.clip(sol[1]/sol[0], -1.0, 0.0))
            r1 = np.corrcoef(bp(hst_w, s_r)[sel], bp(m)[sel])[0, 1]
            r2 = np.corrcoef((bp(hst_w, s_r) + k*bp(hst2_w, s_r))[sel], bp(m)[sel])[0, 1]
            hst_w = np.clip(hst_w + k*hst2_w, 0, None)
            nii_note = f' | [N II] via F673N: k {k:+.3f} (raw {sol[1]/sol[0]:+.3f}), band-pass corr {r1:.3f} -> {r2:.3f}'
        rect_fit = inscribed_rect(cover, 0, n2//2)
        rect_val = inscribed_rect(cover, n2//2 + n2//20, n2)
        if rect_fit is None or rect_val is None:
            print(f'{name:14s} insufficient HST coverage on this patch'); continue
        hw = np.clip(hst_w, 0, np.percentile(hst_w, 99.8))
        mk = np.clip(m, 0, np.percentile(m, 99.8))
        k = wiener_kernel_lin(hw, mk, rect_fit)
        fw = fwhm_area(k)*GRID
        # Parametric kernel with stars masked on BOTH sides (generous discs:
        # a star's wings convolved with the kernel leak past its core).
        kmask = ~star_discs(m.shape, detect_stars, (hw, 12, 4000), (m, 16, 2000))
        fwm, beta, _ = moffat_kernel_fit(hw, mk, rect_fit, mask=kmask)
        fwm *= GRID
        truth = conv(hw, t_psf)
        vy0, vy1, vx0, vx1 = rect_val
        vmask = np.zeros_like(truth); vmask[vy0:vy1, vx0:vx1] = 1.0
        stars = (truth > np.percentile(truth[vmask > 0.5], 99)) | \
                (m > np.percentile(m[vmask > 0.5], 99))
        vneb = vmask*(~ndimage.binary_dilation(stars, iterations=6))
        _, e = affine_match(m, truth, vneb)
        line = (f'{name:14s} reg {npairs:3d} pairs/{med:.2f} px | extended-structure '
                f'FWHM {fw:.2f}" (Wiener) / {fwm:.2f}" (Moffat fit, beta {beta:.1f}) | '
                f'nebula NRMSE vs Hubble@1.3" {e:.4f}{nii_note}')
        if starless:
            # v2: symmetric star removal, then starless-vs-starless with
            # small residual apertures from BOTH star images.
            # The truth is noiseless; noise-calibrated removers (the analytic
            # one's detector, the neural tools' stretches) misbehave on it.
            # A noise floor at 1e-3 of the nebula peak is far below anything
            # the metric resolves and puts every remover in its design regime.
            truth_in, inner, t_sigma = prepare_truth(truth, cover)
            r_sl, r_st = remove_stars(plane, remover, workdir, f'{name}_render')
            t_sl, t_st = remove_stars(truth_in, remover, workdir, f'{name}_truth')
            m_sl = ndimage.zoom(r_sl, 2, order=3)
            m_st = ndimage.zoom(r_st, 2, order=3)
            # Residual apertures at the stars of BOTH images, found by the
            # study's own detector (remover-independent), plus bright cores
            # left in the render's stars image; scored inside the eroded
            # coverage only.
            resid = star_apertures(m, truth_in, m_st, detect_stars)
            vneb2 = vmask*(~resid)*inner
            _, e2 = affine_match(m_sl, t_sl, vneb2)
            line += f' | STARLESS v2/{remover} {e2:.4f} (keep {float((vneb2>0.5).sum()/max(vmask.sum(),1)):.0%})'
        print(line)

if __name__ == '__main__':
    main()

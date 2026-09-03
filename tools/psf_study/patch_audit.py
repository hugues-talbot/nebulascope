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

    patch_audit.py --filter H name=path [name=path ...]

Each path: FITS with one plane (or first plane used). PSF_DATA must
point at the folder whose PSF_comparison/ holds the HST mosaics.
"""
import sys, os
import numpy as np
from scipy import ndimage

def main():
    sys.path.insert(0, os.environ.get('PSF_DATA', '.'))
    from psf_pipeline import bruteforce_similarity, refine_affine, detect_stars, compose
    from star_fwhm import read_fits_f32
    from linear_deconv import (load_hst, regprep, warp_to, wiener_kernel_lin,
                               fwhm_area, inscribed_rect, conv, gauss_psf,
                               affine_match, HST_FILES, GRID)
    args = sys.argv[1:]
    ch = 'H'
    if '--filter' in args:
        i = args.index('--filter'); ch = args[i+1]; del args[i:i+2]
    if not args:
        raise SystemExit(__doc__)
    inputs = []
    for a in args:
        name, path = a.split('=', 1)
        cube, _ = read_fits_f32(path)
        plane = np.asarray(cube, dtype=np.float64)
        plane = plane[0] if plane.ndim == 3 else plane
        inputs.append((name, np.nan_to_num(plane)))

    hst = np.fliplr(load_hst(HST_FILES[ch]))
    hst8 = np.fliplr(load_hst(HST_FILES[ch], binf=8))
    Sh = detect_stars(regprep(hst8), maxn=600)
    t_psf = gauss_psf(1.3/GRID)

    for name, plane in inputs:
        m = ndimage.zoom(plane, 2, order=3)
        m_reg = regprep(m)
        n2 = m.shape[1]
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
        Ahalf = np.zeros((3, 2)); Ahalf[0, 0] = Ahalf[1, 1] = 0.5
        A = compose(Ahalf, A8)
        hst_w = warp_to(hst, A, m.shape)
        valid = ndimage.binary_erosion(hst > 0.01, iterations=4)
        cover = warp_to(valid.astype(np.float64), A, m.shape) > 0.98
        rect_fit = inscribed_rect(cover, 0, n2//2)
        rect_val = inscribed_rect(cover, n2//2 + n2//20, n2)
        if rect_fit is None or rect_val is None:
            print(f'{name:14s} insufficient HST coverage on this patch'); continue
        hw = np.clip(hst_w, 0, np.percentile(hst_w, 99.8))
        mk = np.clip(m, 0, np.percentile(m, 99.8))
        k = wiener_kernel_lin(hw, mk, rect_fit)
        fw = fwhm_area(k)*GRID
        truth = conv(hw, t_psf)
        vy0, vy1, vx0, vx1 = rect_val
        vmask = np.zeros_like(truth); vmask[vy0:vy1, vx0:vx1] = 1.0
        stars = (truth > np.percentile(truth[vmask > 0.5], 99)) | \
                (m > np.percentile(m[vmask > 0.5], 99))
        vneb = vmask*(~ndimage.binary_dilation(stars, iterations=6))
        _, e = affine_match(m, truth, vneb)
        print(f'{name:14s} reg {npairs:3d} pairs/{med:.2f} px | extended-structure '
              f'FWHM {fw:.2f}" | nebula NRMSE vs Hubble@1.3" {e:.4f}')

if __name__ == '__main__':
    main()

"""ML-version audit: raw vs BXT ML4 vs BXT ML5 against the Hubble overlap.

The appendix protocol (linear_deconv.py) condensed to the comparative
question: per channel and per render, register the WFC3/UVIS mosaic onto
the ROI, estimate the extended-structure Wiener kernel on the west
(fit) rectangle, and score star-masked nebula NRMSE against Hubble
degraded to a common 1.3" on the held-out east rectangle. Expects the
three full-frame dumps m16_full_raw.fits / m16_full_BXT.fits /
m16_full_BXT5.fits next to this file (save each master as FITS from
NebulaScope), and PSF_DATA pointing at the folder whose PSF_comparison/
holds the HST mosaics. Findings of the 2026-09 run are recorded in
docs/PSF-STUDY.md.
"""
import sys, os
sys.path.insert(0, '/Users/talboth/Projects/Claude/Data/Nebulascope')
import numpy as np
from scipy import ndimage
from psf_pipeline import bruteforce_similarity, refine_affine, detect_stars, compose
from star_fwhm import read_fits_f32
from linear_deconv import (load_hst, regprep, warp_to, wiener_kernel_lin, fwhm_area,
                           inscribed_rect, conv, gauss_psf, affine_match,
                           HST_FILES, ROI_CX, ROI_CY, ROI_R, PLATE, GRID)

def main():
    S = os.path.dirname(os.path.abspath(__file__))
    if len(sys.argv) > 1:
        # ml5_audit.py name=path [name=path ...] — any set of renders of the
        # same field (e.g. the lucky-stack trio), same protocol.
        masters = {}
        for a in sys.argv[1:]:
            name, path = a.split('=', 1)
            masters[name] = read_fits_f32(path)[0]
    else:
        masters = { 'raw':  read_fits_f32(os.path.join(S, 'm16_full_raw.fits'))[0],
                    'BXT4': read_fits_f32(os.path.join(S, 'm16_full_BXT.fits'))[0],
                    'BXT5': read_fits_f32(os.path.join(S, 'm16_full_BXT5.fits'))[0] }
    x0, x1 = ROI_CX - ROI_R, ROI_CX + ROI_R
    y0, y1 = ROI_CY - ROI_R, ROI_CY + ROI_R
    n2 = 4*ROI_R
    t_psf = gauss_psf(1.3/GRID)
    for c, ch in enumerate(['S', 'H', 'O']):
        hst = np.fliplr(load_hst(HST_FILES[ch]))
        hst8 = np.fliplr(load_hst(HST_FILES[ch], binf=8))
        Sh = detect_stars(regprep(hst8), maxn=600)
        print(f'[{ch}]')
        for name, cube in masters.items():
            m = ndimage.zoom(np.nan_to_num(cube[c][y0:y1, x0:x1].astype(np.float64)), 2, order=3)
            m_reg = regprep(m)
            # Seed with the known full-master window; a different stack (e.g.
            # the lucky selection) may be oriented differently, so fall back
            # to the exhaustive search when the refinement looks unseeded.
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
                print(f'  {name:5s} coverage failure'); continue
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
            print(f'  {name:5s} reg {npairs:3d} pairs/{med:.2f} px | extended-structure '
                  f'FWHM {fw:.2f}\" | nebula NRMSE vs Hubble@1.3\" {e:.4f}')

if __name__ == '__main__':
    main()

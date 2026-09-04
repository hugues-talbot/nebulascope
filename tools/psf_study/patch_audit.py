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

    patch_audit.py --filter H [--starless] [--borrow A=B,C=B] name=path ...

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
kernel estimate is unchanged. Requires `rc-astro` on PATH.
"""
import sys, os, subprocess
import numpy as np
from scipy import ndimage


def rc_sxt(plane, workdir, tag, ref):
    """Run StarXTerminator on a 2-D float plane; return (starless, stars)
    in OUR orientation (rc-astro writes standard FITS, our study writer
    is top-down: pick the flip that correlates with the input)."""
    sys.path.insert(0, os.environ.get('PSF_DATA', '.'))
    from star_fwhm import read_fits_f32
    from full_deconv import write_fits_f32
    os.makedirs(workdir, exist_ok=True)
    src = os.path.join(workdir, f'{tag}.fits')
    scale = float(np.percentile(plane, 99.9)) or 1.0
    write_fits_f32(src, (plane/scale)[None].astype(np.float32), ['sxt input'])
    subprocess.run(['rc-astro', '--no-banner', 'sxt', src, '--stars', '--depth', '32F',
                    '-o', workdir + '/', '--overwrite'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    out = []
    for suffix in ('-sxt', '-sxt-stars'):
        a = np.asarray(read_fits_f32(os.path.join(workdir, f'{tag}{suffix}.fits'))[0],
                       dtype=np.float64)[0]*scale
        f = np.flipud(a)
        r = lambda x: np.corrcoef(x.ravel(), ref.ravel())[0, 1]
        out.append(f if r(f) > r(a) else a)
    # SXT normalizes by the frame max: recover the exact gain (starless+stars
    # reconstructs the input to correlation 1.0) so units are preserved.
    rec = out[0] + out[1]
    g = np.polyfit(rec.ravel(), plane.ravel(), 1)[0]
    return out[0]*g, out[1]*g

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
    starless = '--starless' in args
    if starless:
        args.remove('--starless')
    borrow = {}
    if '--borrow' in args:
        i = args.index('--borrow')
        borrow = dict(kv.split('=', 1) for kv in args[i+1].split(','))
        del args[i:i+2]
    if not args:
        raise SystemExit(__doc__)
    workdir = os.path.join(os.environ.get('PSF_DATA', '.'), 'ninth_row', 'sxt_work')
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
        line = (f'{name:14s} reg {npairs:3d} pairs/{med:.2f} px | extended-structure '
                f'FWHM {fw:.2f}" | nebula NRMSE vs Hubble@1.3" {e:.4f}')
        if starless:
            # v2: symmetric star removal, then starless-vs-starless with
            # small residual apertures from BOTH star images.
            r_sl, r_st = rc_sxt(plane, workdir, f'{name}_render', plane)
            t_sl, t_st = rc_sxt(truth, workdir, f'{name}_truth', truth)
            m_sl = ndimage.zoom(r_sl, 2, order=3)
            m_st = ndimage.zoom(r_st, 2, order=3)
            # Residual apertures: where either star image holds real star
            # flux — judged against the RENDER's noise (a star image is ~0
            # almost everywhere, so its own MAD is meaningless) and, for the
            # noiseless truth, against a fraction of its star peak.
            def sig(a): return np.median(np.abs(a - np.median(a)))/0.6745 + 1e-12
            resid = (m_st > 3*sig(m)) | (t_st > 0.005*np.percentile(t_st, 99.9))
            vneb2 = vmask*(~ndimage.binary_dilation(resid, iterations=4))*cover
            _, e2 = affine_match(m_sl, t_sl, vneb2)
            line += f' | STARLESS v2 {e2:.4f} (keep {float((vneb2>0.5).sum()/max(vmask.sum(),1)):.0%})'
        print(line)

if __name__ == '__main__':
    main()

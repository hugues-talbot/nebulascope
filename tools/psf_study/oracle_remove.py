"""Oracle-guided analytic star removal on every sub of a patch cube.

    oracle_remove.py --stack <stack.fits> <patch_cube.fits> [--tag oracle]

The first stack (plain mean or integrated master, on the sub grid) is
the ORACLE: it sees stars at sqrt(N) times a sub's signal-to-noise, so
its catalogue of positions and fluxes reaches stars no per-sub detector
can. Per sub, the catalogue drives everything:

  1. the sub's PSF (elliptical Moffat, proper_coadd's own fitter) is
     measured on the catalogue's brightest unsaturated stars at their
     KNOWN positions, which also yields the sub's residual registration
     offset and its transparency t_i = median(A_sub / F_stack);
  2. every catalogue star is subtracted with that PSF, brightest first
     on the running residual: bright stars (expected peak > 8 sigma)
     get a free amplitude and sub-pixel position; faint ones get their
     amplitude FIXED at F_stack * t_i — fitting it would fit noise —
     which is photometric star subtraction in the hierarchical form;
  3. clipped pixels are excluded from every fit (flux comes from the
     wings); cores where the model exceeds 100 sigma — grown, for bright
     stars, over the halo the Moffat does not model, as long as the
     annuli hold a coherent offset — are filled harmonically from their
     boundary (a plane fill dug craters along bright nebula rims), then
     given noise at the sub's sigma, feathered.

Outputs <cube>_starless_<tag>.fits, <cube>_stars_<tag>.fits (stars =
sub - starless exactly) and <cube>_<tag>_report.json. The same star
list across subs also makes the per-sub PSFs comparable like for like.
"""
import sys, os, json
import numpy as np
from scipy import ndimage
from scipy.optimize import least_squares


def _noise(img):
    hp = img - ndimage.uniform_filter(img, 31)
    return float(np.median(np.abs(hp - np.median(hp))) / 0.6745)


def _shape_params(fmaj, fmin, pa_deg, beta):
    conv = 2*np.sqrt(2**(1/beta) - 1)
    return max(0.3, fmaj/conv), max(0.3, fmin/conv), np.radians(pa_deg)


def moffat_stamp(yy, xx, x0, y0, A, sx, sy, th, beta):
    ct, st = np.cos(th), np.sin(th)
    u = ((xx-x0)*ct + (yy-y0)*st)**2/sx**2 + (-(xx-x0)*st + (yy-y0)*ct)**2/sy**2
    return A*(1.0 + u)**(-beta)


def harmonic_fill(stamp, mask, iters=400):
    """Replace stamp[mask] by the harmonic (Laplace) interpolation of the
    boundary values — the Poisson-integral fill the app uses, so a disc
    on a nebula rim follows the rim instead of sinking to a plane."""
    out = stamp.copy()
    out[mask] = ndimage.median_filter(stamp, 9)[mask]
    k = np.array([[0, 0.25, 0], [0.25, 0, 0.25], [0, 0.25, 0]])
    for _ in range(iters):
        nb = ndimage.convolve(out, k, mode='nearest')
        out[mask] = nb[mask]
    return out


def grow_disc(resid, mdl, x0, y0, sig, r_start, r_max):
    """Bright stars wear halos no Moffat models. Starting outside the
    core, grow a disc radius while each 3-px annulus of the residual
    still holds a COHERENT mean offset (3 sigma of its own mean), the
    outer stamp ring giving the local level. Returns the radius."""
    yy, xx = np.mgrid[:resid.shape[0], :resid.shape[1]]
    r = np.hypot(yy - y0, xx - x0)
    outer = (r >= 0.8*r_max) & (r < r_max)
    level = float(np.median(resid[outer])) if outer.sum() > 20 else float(np.median(resid))
    rad = r_start
    while rad + 3 <= r_max:
        ann = (r >= rad) & (r < rad + 3)
        n = int(ann.sum())
        if n < 8:
            break
        if abs(float(np.mean(resid[ann])) - level) > 3*sig/np.sqrt(n):
            rad += 3
        else:
            break
    return rad


def build_catalogue(stack, fit_moffat, sat, nsig=4.0, border=14):
    """Positions and stack fluxes (Moffat amplitudes) of every star the
    stack shows above nsig; the stack's own PSF from its brightest."""
    hp = stack - ndimage.uniform_filter(stack, 31)
    sig = _noise(stack)
    mx = (hp == ndimage.maximum_filter(hp, 5)) & (hp > nsig*sig)
    mx[:border, :] = mx[-border:, :] = False; mx[:, :border] = mx[:, -border:] = False
    ys, xs = np.nonzero(mx)
    order = np.argsort(hp[ys, xs])[::-1]
    ys, xs = ys[order], xs[order]
    # stack PSF on the brightest unsaturated, isolated stars
    fits, used = [], []
    for y, x in zip(ys, xs):
        if len(fits) >= 60: break
        cut = stack[y-13:y+14, x-13:x+14].astype(np.float64)
        if cut.shape != (27, 27) or cut.max() >= sat: continue
        if any((y-py)**2 + (x-px)**2 < 15**2 for py, px in used): continue
        f = fit_moffat(cut)
        if f: fits.append(f); used.append((y, x))
    arr = np.array(fits)
    a2 = np.radians(2*arr[:, 2])
    pa = 0.5*np.degrees(np.arctan2(np.median(np.sin(a2)), np.median(np.cos(a2))))
    shape = (float(np.median(arr[:, 0])), float(np.median(arr[:, 1])), float(pa), float(np.median(arr[:, 3])))
    sx, sy, th = _shape_params(*shape)
    beta = shape[3]
    # flux (amplitude) of every catalogue star with the stack shape fixed;
    # sub-pixel position refined for the bright ones
    cat = []
    yy, xx = np.mgrid[-6:7, -6:7]
    for y, x in zip(ys, xs):
        cut = stack[y-6:y+7, x-6:x+7].astype(np.float64)
        if cut.shape != (13, 13): continue
        bg = np.median(stack[max(0, y-15):y+16, max(0, x-15):x+16])
        peak = cut[6, 6] - bg
        if cut.max() >= sat:
            # saturated in the stack too: amplitude from the wings
            ok = cut < sat
            m = moffat_stamp(yy, xx, 0, 0, 1.0, sx, sy, th, beta)
            A = float(np.sum((cut-bg)[ok]*m[ok])/max(1e-12, np.sum(m[ok]**2)))
            cat.append((float(y), float(x), A, True)); continue
        if peak > 20*sig:
            def res(p):
                return (moffat_stamp(yy, xx, p[0], p[1], p[2], sx, sy, th, beta) + p[3] - cut).ravel()
            r = least_squares(res, [0.0, 0.0, peak, bg], max_nfev=40,
                              bounds=([-2, -2, 0, -np.inf], [2, 2, 5*peak+1e-12, np.inf]))
            cat.append((y + r.x[1], x + r.x[0], float(r.x[2]), False))
        else:
            m = moffat_stamp(yy, xx, 0, 0, 1.0, sx, sy, th, beta)
            A = float(np.sum((cut-bg)*m)/np.sum(m**2))
            if A > 0:
                cat.append((float(y), float(x), A, False))
    cat.sort(key=lambda c: -c[2])
    return cat, shape, sig


def remove_sub(sub, cat, sat, fit_moffat, sig_i, nref=60, halo_model=False):
    """One sub: PSF + offset + transparency at catalogue positions, then
    hierarchical subtraction. Returns starless, report."""
    h, w = sub.shape
    yy27, xx27 = np.mgrid[0:27, 0:27] - 13
    fits, amps = [], []
    for (cy, cx, F, satflag) in cat:
        if len(fits) >= nref: break
        if satflag: continue
        y, x = int(round(cy)), int(round(cx))
        if not (13 <= y < h-13 and 13 <= x < w-13): continue
        cut = sub[y-13:y+14, x-13:x+14].astype(np.float64)
        if cut.max() >= sat: continue
        f = fit_moffat(cut)
        if f:
            fits.append(f + (cy - y, cx - x))
            # amplitude of the fitted model at its own peak ~ cut peak - bg
            amps.append((cut.max() - np.median(cut)) / F)
    if len(fits) < 8:
        return None, {'used': False, 'nstars': len(fits)}
    arr = np.array(fits)
    a2 = np.radians(2*arr[:, 2])
    pa = 0.5*np.degrees(np.arctan2(np.median(np.sin(a2)), np.median(np.cos(a2))))
    fmaj, fmin, beta = float(np.median(arr[:, 0])), float(np.median(arr[:, 1])), float(np.median(arr[:, 3]))
    # residual registration offset: fitted centre minus catalogue sub-pixel position
    dx = float(np.median(arr[:, 4] - arr[:, 7])); dy = float(np.median(arr[:, 5] - arr[:, 6]))
    t = float(np.median(amps))
    sx, sy, th = _shape_params(fmaj, fmin, pa, beta)
    alpha = max(sx, sy)
    resid = sub.astype(np.float64).copy()
    fill = np.zeros_like(resid, dtype=bool)
    n_free = n_fixed = 0
    for (cy, cx, F, satflag) in cat:
        a0 = F*t
        if a0 <= 0: continue
        # stamp radius where the model drops under 0.2 sigma
        R = int(np.clip(np.ceil(alpha*np.sqrt(max(0.0, (a0/(0.2*sig_i))**(1.0/beta) - 1.0))), 6, 70))
        y0, x0 = cy + dy, cx + dx
        yi, xi = int(round(y0)), int(round(x0))
        ya, yb, xa, xb = max(0, yi-R), min(h, yi+R+1), max(0, xi-R), min(w, xi+R+1)
        if yb - ya < 5 or xb - xa < 5: continue
        yy, xx = np.mgrid[ya:yb, xa:xb]
        cut = resid[ya:yb, xa:xb]
        ok = cut < sat
        halo = None
        if a0 > 8*sig_i and ok.sum() > 30:
            # free amplitude and position, local background plane; bright
            # stars add a wide circular Gaussian HALO component, because a
            # Moffat alone leaves their halos in the coadd (the referee saw
            # them as bright blobs where SXT had erased them) and filling
            # the halo instead invents smooth nebula over 2 % of the frame
            # (that scored worse still). Modelling keeps the nebula under.
            # (Optional: on the S II cube the halo term over-subtracts by
            # ~1.5 sigma per sub around the brightest stars, a bias that
            # adds coherently over 50 subs into 12-17 sigma holes in the
            # coadd; without it the halo is left and partly filled. Both
            # lose to the neural removers at those few stars only.)
            bright = halo_model and a0 > 30*sig_i
            def res(p):
                mdl = moffat_stamp(yy, xx, p[0], p[1], p[2], sx, sy, th, beta) + p[3] + p[4]*(xx-x0) + p[5]*(yy-y0)
                if bright:
                    mdl = mdl + p[6]*np.exp(-0.5*((xx-p[0])**2 + (yy-p[1])**2)/p[7]**2)
                return (mdl - cut)[ok].ravel()
            bg0 = float(np.median(cut[ok]))
            fw_px = 2*np.sqrt(2**(1/beta) - 1)*alpha
            p0 = [x0, y0, a0, bg0, 0.0, 0.0] + ([0.02*a0, 3.0*fw_px] if bright else [])
            lo_b = [x0-1.5, y0-1.5, 0.0, -np.inf, -np.inf, -np.inf] + ([0.0, 1.5*fw_px] if bright else [])
            hi_b = [x0+1.5, y0+1.5, 20*a0, np.inf, np.inf, np.inf] + ([0.3*a0, max(1.6*fw_px, 0.5*R)] if bright else [])
            try:
                r = least_squares(res, p0, max_nfev=40, bounds=(lo_b, hi_b))
                x0, y0, A, bg, gx, gy = r.x[:6]
                if bright:
                    halo = (float(r.x[6]), float(r.x[7]))
            except Exception:
                A, bg, gx, gy = a0, bg0, 0.0, 0.0
            n_free += 1
        else:
            A = a0; bg = float(np.median(cut[ok])) if ok.any() else 0.0; gx = gy = 0.0
            n_fixed += 1
        mdl = moffat_stamp(yy, xx, x0, y0, A, sx, sy, th, beta)
        if halo is not None:
            mdl = mdl + halo[0]*np.exp(-0.5*((xx-x0)**2 + (yy-y0)**2)/halo[1]**2)
        resid[ya:yb, xa:xb] -= mdl
        core = (mdl > 100*sig_i) | (~ok)
        if a0 > 8*sig_i:
            # What the model still misses near the core (wing shape, PSF
            # asymmetry) shows as a connected region of >4 sigma residual
            # attached to the core; fill that too (the v2 compromise that
            # scored best), bounded by where the model itself is faint.
            plane = bg + gx*(xx-x0) + gy*(yy-y0)
            off = np.abs(resid[ya:yb, xa:xb] - plane) > 4*sig_i
            region = ndimage.binary_closing((off | core) & (mdl > 0.5*sig_i), iterations=2)
            lab, n = ndimage.label(region)
            if n:
                cy_ = int(np.clip(round(y0 - ya), 0, region.shape[0]-1))
                cx_ = int(np.clip(round(x0 - xa), 0, region.shape[1]-1))
                if lab[cy_, cx_]:
                    core = core | (lab == lab[cy_, cx_])
        if core.any():
            stamp = resid[ya:yb, xa:xb]
            stamp[:] = harmonic_fill(stamp, core)
            fill[ya:yb, xa:xb] |= core
    # feathered noise in the filled cores so they do not read as holes
    if fill.any():
        rng = np.random.default_rng(int(1e6*sig_i) % 2**32)
        wgt = ndimage.gaussian_filter(fill.astype(float), 1.5)
        resid += wgt*rng.normal(0.0, sig_i, resid.shape)
    rep = {'used': True, 'fwhm': round(float(np.sqrt(fmaj*fmin)), 3), 'fmaj': round(fmaj, 3),
           'fmin': round(fmin, 3), 'pa': round(float(pa), 1), 'beta': round(beta, 2),
           'dx': round(dx, 3), 'dy': round(dy, 3), 'transparency': round(t, 4),
           'nref': len(fits), 'n_free': n_free, 'n_fixed': n_fixed, 'filled_px': int(fill.sum())}
    return resid, rep


def main():
    sys.path.insert(0, os.environ.get('PSF_DATA', '.'))
    from star_fwhm import read_fits_f32
    from full_deconv import write_fits_f32
    from proper_coadd import fit_moffat
    args = sys.argv[1:]
    stack_path = None; tag = 'oracle'
    if '--stack' in args:
        i = args.index('--stack'); stack_path = args[i+1]; del args[i:i+2]
    if '--tag' in args:
        i = args.index('--tag'); tag = args[i+1]; del args[i:i+2]
    halo_model = '--halo' in args
    if halo_model:
        args.remove('--halo')
    if not args or stack_path is None:
        raise SystemExit(__doc__)
    cube_path = args[0]
    cube = np.nan_to_num(np.asarray(read_fits_f32(cube_path)[0], dtype=np.float64))
    stack = np.nan_to_num(np.asarray(read_fits_f32(stack_path)[0], dtype=np.float64))
    stack = stack[0] if stack.ndim == 3 else stack
    # the stack may be background-subtracted while subs carry a pedestal;
    # amplitudes are differential so only the sub's own background matters
    sat = 0.9*float(cube.max())
    cat, shape, sig_s = build_catalogue(stack, fit_moffat, sat)
    print(f'catalogue: {len(cat)} stars above 4 sigma in the stack (sigma {sig_s:.3g}); '
          f'stack PSF {shape[0]:.2f}x{shape[1]:.2f} px, beta {shape[3]:.2f}; '
          f'{sum(1 for c in cat if c[3])} saturated', flush=True)
    N = cube.shape[0]
    starless = np.empty_like(cube, dtype=np.float32)
    reports = []
    for i in range(N):
        sig_i = _noise(cube[i])
        sl, rep = remove_sub(cube[i], cat, sat, fit_moffat, sig_i, halo_model=halo_model)
        rep['i'] = i; rep['sigma'] = round(sig_i, 6)
        if sl is None:
            starless[i] = cube[i]
        else:
            starless[i] = sl
        reports.append(rep)
        if (i + 1) % 10 == 0 or i == 0:
            print(f'  {i+1}/{N}: fwhm {rep.get("fwhm")} px, t {rep.get("transparency")}, '
                  f'free {rep.get("n_free")} fixed {rep.get("n_fixed")}', flush=True)
    base = cube_path[:-5] if cube_path.endswith('.fits') else cube_path
    write_fits_f32(base + f'_starless_{tag}.fits', starless, [f'oracle-guided analytic starless subs from {os.path.basename(cube_path)}'])
    write_fits_f32(base + f'_stars_{tag}.fits', (cube - starless).astype(np.float32), ['stars = sub - starless'])
    json.dump({'catalogue_size': len(cat), 'stack_psf': shape, 'subs': reports},
              open(base + f'_{tag}_report.json', 'w'), indent=1)
    print('written:', base + f'_starless_{tag}.fits')


if __name__ == '__main__':
    main()

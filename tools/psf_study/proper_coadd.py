"""Proper coaddition (Zackay & Ofek 2017) of a registered-sub patch cube.

The matched-filter stack: with subs y_i = f_i * (k_i (*) x) + n_i,

    X_t = OTF_t . [ sum_i (f_i/s_i^2) conj(K_i) Y_i ]
                / [ sum_i (f_i^2/s_i^2) |K_i|^2 + lambda ]

Each sub is weighted frequency-by-frequency by its own OTF: good-seeing
frames rule the high frequencies, poor ones still donate all their
photons at low frequencies — lucky imaging without discarding anything.
The declared circular-Gaussian target OTF_t keeps the study's contract
discipline; re-measure the product's stars to audit delivery.

    proper_coadd.py <patch_cube.fits> <out_prefix> [target_fwhm_px] [lambda]
    proper_coadd.py --selftest

Inputs come from patch_extract.py (cube [N,h,w] + .manifest.json).
Outputs: <prefix>_proper.fits (target PSF), <prefix>_mean.fits (classic
mean of the same subs, for the comparison row), and a per-sub report.
"""
import sys, os, json
import numpy as np
from numpy.fft import rfft2, irfft2
from scipy import ndimage
from scipy.optimize import least_squares

# --------------------------------------------------------- PSF fitting ----

def detect_stars(img, maxn=60):
    hp = img - ndimage.uniform_filter(img, 31)
    sig = np.median(np.abs(hp - np.median(hp))) / 0.6745
    mx = (hp == ndimage.maximum_filter(hp, 9)) & (hp > 6 * sig)
    ys, xs = np.nonzero(mx)
    amps = hp[ys, xs]
    order = np.argsort(amps)[::-1]
    pts = []
    for i in order:
        y, x = ys[i], xs[i]
        if 13 <= y < img.shape[0]-13 and 13 <= x < img.shape[1]-13:
            if all((y-py)**2 + (x-px)**2 > 15**2 for py, px in pts):
                pts.append((y, x))
        if len(pts) >= maxn:
            break
    return pts

def fit_moffat(cut):
    """Elliptical Moffat on a 2R+1 cutout -> (fmaj, fmin, pa_deg, beta, dx, dy)."""
    s = cut.shape[0]; half = s // 2
    yy, xx = np.mgrid[0:s, 0:s] - half
    bg0 = np.median(cut); A0 = cut.max() - bg0
    if A0 <= 0: return None
    def model(p):
        x0, y0, A, bg, sx, sy, th, beta = p
        ct, st = np.cos(th), np.sin(th)
        u = ((xx-x0)*ct + (yy-y0)*st)**2/sx**2 + (-(xx-x0)*st + (yy-y0)*ct)**2/sy**2
        return bg + A*(1.0 + u)**(-beta)
    p0 = [0, 0, A0, bg0, 2.3, 1.8, 0.0, 2.5]
    try:
        r = least_squares(lambda p: (model(p)-cut).ravel(), p0, max_nfev=120,
                          bounds=([-3,-3,0.05*A0,bg0-5*abs(bg0)-1e-3,0.4,0.4,-np.pi,1.2],
                                  [ 3, 3,  3*A0,  bg0+5*abs(bg0)+1e-3, 12, 12, np.pi, 8.0]))
    except Exception:
        return None
    x0, y0, A, bg, sx, sy, th, beta = r.x
    if np.sqrt(np.mean(r.fun**2)) > 0.08*A: return None
    conv = 2*np.sqrt(2**(1/beta) - 1)
    fa, fb = sx*conv, sy*conv
    if fb > fa: fa, fb, th = fb, fa, th + np.pi/2
    return fa, fb, np.degrees(th) % 180.0, beta, x0, y0

def measure_sub(img, refpts=None, maxstars=40):
    """Median PSF of one sub. refpts pins the STAR LIST (and yields the
    mean residual registration offset relative to the reference sub)."""
    pts = refpts if refpts is not None else detect_stars(img, maxstars)
    fits = []
    for y, x in pts:
        cut = img[y-13:y+14, x-13:x+14]
        if cut.shape != (27, 27): continue
        f = fit_moffat(cut.astype(np.float64))
        if f: fits.append(f)
    if len(fits) < 5: return None
    arr = np.array(fits)
    a2 = np.radians(2*arr[:, 2])
    pa = 0.5*np.degrees(np.arctan2(np.median(np.sin(a2)), np.median(np.cos(a2))))
    return {'fmaj': float(np.median(arr[:, 0])), 'fmin': float(np.median(arr[:, 1])),
            'pa': float(pa), 'beta': float(np.median(arr[:, 3])),
            'dx': float(np.median(arr[:, 4])), 'dy': float(np.median(arr[:, 5])),
            'nstars': len(fits)}

# ----------------------------------------------------------- kernels ------

def moffat_kernel(fmaj, fmin, pa_deg, beta, dx, dy, n):
    """Apodized elliptical Moffat, centred at (+dx,+dy) sub-pixel (carries the
    sub's residual registration offset into K_i as a phase)."""
    conv = 2*np.sqrt(2**(1/beta) - 1)
    sx, sy = max(0.3, fmaj/conv), max(0.3, fmin/conv)
    half = n // 2
    yy, xx = np.mgrid[0:n, 0:n] - half
    th = np.radians(pa_deg); ct, st = np.cos(th), np.sin(th)
    u = ((xx-dx)*ct + (yy-dy)*st)**2/sx**2 + (-(xx-dx)*st + (yy-dy)*ct)**2/sy**2
    k = (1.0 + u)**(-beta)
    d = np.hypot(xx-dx, yy-dy)
    R, Ri = half, 0.75*half
    w = np.clip(0.5 + 0.5*np.cos(np.pi*(d-Ri)/max(1e-9, R-Ri)), 0, 1)
    w[d <= Ri] = 1.0; w[d >= R] = 0.0
    k *= w
    return k / k.sum()

def embed_otf(k, shape):
    n = k.shape[0]; half = n // 2
    f = np.zeros(shape)
    ys = (np.arange(n) - half) % shape[0]
    xs = (np.arange(n) - half) % shape[1]
    f[np.ix_(ys, xs)] = k
    return rfft2(f)

def gauss_otf(fwhm, shape):
    n = 65
    half = n // 2
    yy, xx = np.mgrid[0:n, 0:n] - half
    s = max(0.3, fwhm/2.3548)
    k = np.exp(-0.5*(xx**2 + yy**2)/s**2)
    return embed_otf(k/k.sum(), shape)

# ----------------------------------------------------- RED prior (v2) -----
# The multi-frame RED fixed point over the coadd accumulators:
#     X = (Num + mu.F[D(x)]) / (Den + mu),   target OTF applied at the end.
# Ported lessons from the in-app starlet-RED (NebulaScope core/Deconvolve):
# threshold only the FINE starlet scales (coarse shrinkage prints the B3
# tensor support around stars) and hold the prior NEUTRAL near bright stars
# (round, brightness-tiered zones — here via distance transforms).

def atrous_smooth(x, step):
    n = 4*step + 1
    w = np.zeros(n)
    w[::step] = [1/16, 4/16, 6/16, 4/16, 1/16]
    x = ndimage.convolve1d(x, w, axis=0, mode='mirror')
    return ndimage.convolve1d(x, w, axis=1, mode='mirror')

def starlet_denoise(x, levels=5, k=3.0, thresh_levels=3):
    cur, out = x, np.zeros_like(x)
    for j in range(levels):
        sm = atrous_smooth(cur, 1 << j)
        d = cur - sm
        if j < thresh_levels:
            thr = k*np.median(np.abs(d))/0.6745
            out += np.sign(d)*np.maximum(np.abs(d) - thr, 0)
        else:
            out += d
        cur = sm
    return out + cur

def star_neutral_mask(img):
    fin = img[np.isfinite(img)]
    mask = np.zeros(img.shape)
    for pct, r0, r1 in ((99.9, 6, 14), (99.99, 14, 32), (99.999, 28, 64)):
        thr = np.percentile(fin, pct)
        seeds = img > thr
        if not seeds.any():
            continue
        d = ndimage.distance_transform_edt(~seeds)
        m = np.clip(0.5 + 0.5*np.cos(np.pi*(d - r0)/(r1 - r0)), 0, 1)
        m[d <= r0] = 1.0
        m[d >= r1] = 0.0
        mask = np.maximum(mask, m)
    return mask

# ------------------------------------------------------------ coadd -------

def edge_window(shape, margin=32):
    wy = np.ones(shape[0]); wx = np.ones(shape[1])
    r = np.arange(margin)/margin
    ramp = 0.5 - 0.5*np.cos(np.pi*r)
    wy[:margin] = ramp; wy[-margin:] = ramp[::-1]
    wx[:margin] = ramp; wx[-margin:] = ramp[::-1]
    return np.outer(wy, wx)

def proper_coadd(cube, psfs, target_fwhm, lam, verbose=True,
                 red_iters=0, red_mu=1e-2, data=None):
    """cube [N,h,w] background-subtracted; psfs: list of measure_sub dicts
    (None entries are skipped). red_iters > 0 runs the multi-frame RED
    fixed point over the accumulators (starlet prior, fine scales only,
    star-neutral near bright stars). `data` (same shape) substitutes the
    frames the estimator SEES while `cube` still supplies noise/weights:
    pass the SXT-starless subs to coadd nebulosity with no star cores —
    the one model violation — and hence no ringing moats.
    Returns (proper, mean, report)."""
    shape = cube.shape[1:]
    if data is None:
        data = cube
    win = edge_window(shape)
    num = np.zeros((shape[0], shape[1]//2 + 1), np.complex128)
    den = np.zeros_like(num, np.float64)
    used, mean_acc = 0, np.zeros(shape)
    ref_flux = None
    report = []
    for i, (sub, dsub, p) in enumerate(zip(cube, data, psfs)):
        if p is None:
            report.append({'i': i, 'used': False}); continue
        sig = float(np.median(np.abs(sub - np.median(sub))))/0.6745
        bright = sub[sub > 5*sig]
        flux = float(np.sum(bright)) if bright.size else 1.0
        if ref_flux is None: ref_flux = flux
        f = flux/ref_flux                       # transparency scale
        wgt = f/max(sig, 1e-12)**2
        K = embed_otf(moffat_kernel(p['fmaj'], p['fmin'], p['pa'], p['beta'],
                                    p['dx'], p['dy'], 65), shape)
        Y = rfft2((dsub - np.median(dsub))*win)
        num += wgt*np.conj(K)*Y
        den += wgt*f*np.abs(K)**2
        mean_acc += (dsub - np.median(dsub))/f
        used += 1
        report.append({'i': i, 'used': True, 'fwhm': round(np.sqrt(p['fmaj']*p['fmin']), 3),
                       'sigma': round(sig, 6), 'f': round(f, 4), 'nstars': p['nstars']})
        if verbose and (i+1) % 20 == 0:
            print(f'  sub {i+1}/{len(cube)}')
    Tt = gauss_otf(target_fwhm, shape)
    scale = den.flat[0]/max(used, 1)           # keep output near input units
    if red_iters > 0:
        mu = red_mu*den.flat[0]
        X = num/(den + mu)                     # warm start, zero prior mean
        x = irfft2(X, s=shape)
        sm = star_neutral_mask(x)
        for it in range(red_iters):
            d = starlet_denoise(x)
            d = sm*x + (1.0 - sm)*d
            X = (num + mu*rfft2(d))/(den + mu)
            x = irfft2(X, s=shape)
        proper = irfft2(Tt*X, s=shape)*scale
    else:
        proper = irfft2(Tt*num/(den + lam*den.flat[0]), s=shape)*scale
    return proper, mean_acc/max(used, 1), report

# ---------------------------------------------------------- self-test -----

def selftest():
    rng = np.random.default_rng(7)
    H = W = 512
    truth = np.zeros((H, W))
    for _ in range(120):                       # stars
        x, y = rng.uniform(30, W-30), rng.uniform(30, H-30)
        truth[int(y), int(x)] += rng.uniform(0.3, 6.0)
    yy, xx = np.mgrid[0:H, 0:W]
    for _ in range(6):                         # nebular blobs
        cx, cy, s = rng.uniform(100, 400), rng.uniform(100, 400), rng.uniform(20, 60)
        truth += rng.uniform(0.02, 0.08)*np.exp(-((xx-cx)**2+(yy-cy)**2)/(2*s*s))
    N, target = 40, 2.6
    cube, psfs_true = [], []
    for i in range(N):
        fmaj = rng.uniform(2.4, 5.0); fmin = fmaj*rng.uniform(0.75, 1.0)
        pa = rng.uniform(0, 180); dx, dy = rng.uniform(-0.5, 0.5, 2)
        k = moffat_kernel(fmaj, fmin, pa, 2.5, dx, dy, 65)
        sub = irfft2(embed_otf(k, (H, W))*rfft2(truth), s=(H, W))
        sig = rng.uniform(0.002, 0.008)
        cube.append(sub + rng.normal(0, sig, (H, W)))
        psfs_true.append({'fmaj': fmaj, 'fmin': fmin, 'pa': pa, 'beta': 2.5,
                          'dx': dx, 'dy': dy, 'nstars': 999})
    cube = np.array(cube)
    # measured PSFs (blind), reference star list from the first sub
    refpts = detect_stars(cube[0], 50)
    psfs = [measure_sub(s, refpts) for s in cube]
    ok = sum(p is not None for p in psfs)
    print(f'per-sub PSF measurement: {ok}/{N} subs usable')
    proper, mean, rep = proper_coadd(cube, psfs, target, 1e-3, verbose=False)
    # classic route: mean stack deconvolved by its own measured PSF to target
    pm = measure_sub(mean, refpts)
    Km = embed_otf(moffat_kernel(pm['fmaj'], pm['fmin'], pm['pa'], pm['beta'],
                                 pm['dx'], pm['dy'], 65), (H, W))
    Tt = gauss_otf(target, (H, W))
    classic = irfft2(Tt*np.conj(Km)*rfft2(mean*edge_window((H, W)))
                     / (np.abs(Km)**2 + 1e-3), s=(H, W))
    ref = irfft2(Tt*rfft2(truth), s=(H, W))    # truth at the declared target
    m = np.zeros((H, W), bool); m[64:-64, 64:-64] = True
    stars = ndimage.binary_dilation(truth > 0.05, iterations=6)
    m &= ~stars
    def score(img):
        a = img[m]; b = ref[m]
        A = np.vstack([a, np.ones_like(a)]).T
        sol, *_ = np.linalg.lstsq(A, b, rcond=None)
        return float(np.sqrt(np.mean((A@sol - b)**2))/b.std())
    red, _, _ = proper_coadd(cube, psfs, target, 1e-3, verbose=False,
                             red_iters=10, red_mu=1e-2)
    sp, sc, sr = score(proper), score(classic), score(red)
    print(f'nebula NRMSE vs truth@target: proper {sp:.4f}  classic {sc:.4f}  '
          f'proper+RED {sr:.4f}')
    pp = measure_sub(proper, refpts)
    pr = measure_sub(red, refpts)
    print(f'delivered FWHM: proper {np.sqrt(pp["fmaj"]*pp["fmin"]):.2f} px, '
          f'proper+RED {np.sqrt(pr["fmaj"]*pr["fmin"]):.2f} px (declared {target})')
    assert sp < sc, 'proper coadd should beat stack-then-deconvolve'
    assert sr < sp*1.02, 'RED should not hurt fidelity'
    print('SELFTEST PASS')

# ------------------------------------------------------------- main -------

def main():
    if '--selftest' in sys.argv:
        selftest(); return
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    args = sys.argv[1:]
    red_iters, red_mu = 0, 1e-2
    data_path = None
    if '--data' in args:
        i = args.index('--data'); data_path = args[i+1]; del args[i:i+2]
    if '--red' in args:
        i = args.index('--red')
        red_iters = int(args[i+1])
        if i+2 < len(args) and not args[i+2].startswith('-'):
            try:
                red_mu = float(args[i+2]); del args[i:i+3]
            except ValueError:
                del args[i:i+2]
        else:
            del args[i:i+2]
    cube_path, prefix = args[0], args[1]
    target = float(args[2]) if len(args) > 2 else 2.36   # 1.8" at 0.7637"/px
    lam = float(args[3]) if len(args) > 3 else 1e-3
    sys.path.insert(0, os.environ.get('PSF_DATA', '.'))
    from star_fwhm import read_fits_f32
    from full_deconv import write_fits_f32
    cube = np.asarray(read_fits_f32(cube_path)[0], dtype=np.float64)
    med = np.median(cube, axis=(1, 2), keepdims=True)
    cube = cube - med                          # per-sub background off
    refpts = detect_stars(cube[0], 60)
    print(f'{cube.shape[0]} subs, {len(refpts)} reference stars')
    psfs = [measure_sub(s, refpts) for s in cube]
    print(f'PSF measured on {sum(p is not None for p in psfs)} subs')
    data = None
    if data_path:
        data = np.asarray(read_fits_f32(data_path)[0], dtype=np.float64)
        print(f'estimator data: {os.path.basename(data_path)} (PSFs/weights from the starry cube)')
    proper, mean, rep = proper_coadd(cube, psfs, target, lam,
                                     red_iters=red_iters, red_mu=red_mu, data=data)
    tag = f', RED {red_iters} iters mu {red_mu}' if red_iters else f', lambda {lam}'
    if data_path: tag += ', starless data'
    write_fits_f32(prefix + '_proper.fits', proper[None].astype(np.float32),
                   [f'proper coadd, target {target} px{tag}'])
    write_fits_f32(prefix + '_mean.fits', mean[None].astype(np.float32),
                   ['plain mean of the same subs'])
    with open(prefix + '_report.json', 'w') as f:
        json.dump(rep, f, indent=1)
    print('written:', prefix + '_proper.fits / _mean.fits / _report.json')

if __name__ == '__main__':
    main()

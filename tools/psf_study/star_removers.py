"""Star removal backends behind one call, for the study's A/B tests.

    starless, stars = remove_stars(plane, backend, workdir, tag)

`plane` is a 2-D float array in the study's own orientation and units;
the result comes back in the same orientation and units, with
stars = plane - starless exactly. Backends:

  sxt       RC-Astro StarXTerminator CLI (`rc-astro sxt`, neural).
  starnet   StarNet2 CLI (`starnet2 --linear`, neural, open source).
  analytic  NebulaScope's analytic removal (Tools > Remove Stars:
            per-star Moffat fits, harmonic core fill; no learned prior),
            driven headlessly through `NebulaScope --run`.
  darkstar  SetiAstro Cosmic Clarity "Dark Star" CLI (neural), run
            through its input/ and output/ folders under /opt/CosmicClarity.

Every external tool has its own conventions, handled here once:
orientation (rc-astro writes standard bottom-up FITS, the study's writer
is top-down: the flip that best correlates the LOW-PASSED output with
the low-passed input is chosen — a starless image correlates weakly with
its starry input at full resolution), and units (SXT normalizes by the
frame maximum, StarNet2 wants [0,1] floats: the output is put back on
the input's scale by a gain fitted on nebula pixels away from stars).

NebulaScope's binary: $NEBULASCOPE_BIN, else the repo's build tree.
"""
import os, sys, subprocess
import numpy as np
from scipy import ndimage

_DEFAULT_BIN = ('/Users/talboth/Projects/Claude/Astro_Inspector/build/src/'
                'NebulaScope.app/Contents/MacOS/NebulaScope')


def _study_io():
    sys.path.insert(0, os.environ.get('PSF_DATA', '.'))
    from star_fwhm import read_fits_f32
    from full_deconv import write_fits_f32
    return read_fits_f32, write_fits_f32


def _read2d(path):
    read_fits_f32, _ = _study_io()
    a = np.asarray(read_fits_f32(path)[0], dtype=np.float64)
    return a[0] if a.ndim == 3 else a


def write_fits_2d(path, img):
    """Minimal NAXIS=2 float32 FITS, rows in array order (the study's
    top-down convention). StarNet2 refuses one-plane cubes."""
    img = np.ascontiguousarray(np.asarray(img, dtype='>f4'))
    cards = ['SIMPLE  =                    T', 'BITPIX  =                  -32',
             'NAXIS   =                    2', 'NAXIS1  = %20d' % img.shape[1],
             'NAXIS2  = %20d' % img.shape[0], 'END']
    hdr = ''.join(c.ljust(80) for c in cards)
    hdr += ' ' * ((2880 - len(hdr) % 2880) % 2880)
    data = img.tobytes()
    with open(path, 'wb') as f:
        f.write(hdr.encode('ascii') + data + b'\0' * ((2880 - len(data) % 2880) % 2880))


def _orient(out, ref):
    """Return `out` in `ref`'s orientation: the vertical flip whose
    low-passed image correlates best with the low-passed reference."""
    lp = lambda a: ndimage.gaussian_filter(a, 3)
    r = lp(ref).ravel()
    best, score = out, -2
    for cand in (out, np.flipud(out)):
        s = np.corrcoef(lp(cand).ravel(), r)[0, 1]
        if s > score:
            best, score = cand, s
    return best


def _finish(plane, starless_raw):
    """Put a starless estimate on the input's scale and return
    (starless, stars). The neural tools rescale (SXT by the frame max,
    StarNet2 to [0,1]) and may shift; an affine fit on LOW-PASSED nebula
    pixels away from stars recovers gain and offset without being led
    by pixel noise (a plain slope fit on a noise-dominated S II frame
    came out negative)."""
    sl = _orient(starless_raw, plane)
    lp = lambda a: ndimage.gaussian_filter(a, 4)
    resid = plane - sl
    # "Calm" = away from star residuals. The threshold is noise-relative
    # on real frames and peak-relative on a noiseless truth, where the
    # residual scatter is ~0 and a pure-noise criterion keeps only the
    # empty region outside coverage (which then fits a zero gain).
    hp = resid - ndimage.gaussian_filter(resid, 6)
    sig = np.median(np.abs(hp - np.median(hp))) / 0.6745
    thr = max(3 * sig, 0.002 * float(np.percentile(hp, 99.99)))
    calm = ndimage.binary_erosion(np.abs(hp) < thr, iterations=3)
    p_lp, s_lp = lp(plane), lp(sl)
    lo, hi = np.percentile(p_lp[calm], [20, 95]) if calm.sum() > 1000 else (-np.inf, np.inf)
    sel = calm & (p_lp > lo) & (p_lp < hi)
    if sel.sum() > 1000:
        g, b = np.polyfit(s_lp[sel].ravel(), p_lp[sel].ravel(), 1)
        if g > 0:
            sl = sl * g + b
    return sl, plane - sl


def _pedestal(plane):
    """External removers clip negatives; background-subtracted renders
    sit half below zero. Lift the frame so its faint tail is positive."""
    return max(0.0, -float(np.percentile(plane, 0.05))) * 1.05


def _run_sxt(plane, workdir, tag):
    read_fits_f32, write_fits_f32 = _study_io()
    src = os.path.join(workdir, f'{tag}.fits')
    ped = _pedestal(plane)
    scale = float(np.percentile(plane + ped, 99.9)) or 1.0
    write_fits_f32(src, ((plane + ped) / scale)[None].astype(np.float32), ['sxt input'])
    subprocess.run(['rc-astro', '--no-banner', 'sxt', src, '--depth', '32F',
                    '-o', workdir + '/', '--overwrite'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return _read2d(os.path.join(workdir, f'{tag}-sxt.fits')) * scale - ped


def _mtf(x, m):
    return ((m - 1.0) * x) / (((2.0 * m - 1.0) * x) - m + 1e-12)


def _run_starnet(plane, workdir, tag):
    """StarNet2 expects a stretched image. Its own `--linear` auto-stretch
    derives the midtone from the frame median and MAD, which explodes on
    a referee truth (half the frame is empty outside Hubble coverage,
    and it is noiseless): the whole nebula saturated at 1. So the
    stretch is ours, invertible, with statistics taken inside coverage:
    black at the 0.5th percentile, white at the 99.95th, midtone placing
    the median at 0.25 (PixInsight's convention); the inverse MTF puts
    the starless output back on the input's scale."""
    src = os.path.join(workdir, f'{tag}_sn_in.fits')
    dst = os.path.join(workdir, f'{tag}_sn_out.fits')
    cov = plane > 1e-6 * float(plane.max()) if (plane == 0).mean() > 0.05 else np.ones(plane.shape, bool)
    lo, hi = np.percentile(plane[cov], [0.5, 99.95])
    med = float(np.median(plane[cov]))
    x = np.clip((plane - lo) / (hi - lo), 0, 1)
    b = min(max((med - lo) / (hi - lo), 1e-4), 0.5)
    m = _mtf(b, 0.25)                      # midtone sending the median to 0.25
    write_fits_2d(src, np.clip(_mtf(x, m), 0, 1))
    subprocess.run(['starnet2', '-q', '-i', src, '-o', dst], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    y = np.clip(_read2d(dst).reshape(plane.shape), 0, 1)
    return _mtf(y, 1.0 - m) * (hi - lo) + lo


def _run_analytic(plane, workdir, tag):
    _, write_fits_f32 = _study_io()
    binary = os.environ.get('NEBULASCOPE_BIN', _DEFAULT_BIN)
    src = os.path.abspath(os.path.join(workdir, f'{tag}_an_in.fits'))
    o2 = os.path.abspath(os.path.join(workdir, f'{tag}_an_row2.fits'))
    o3 = os.path.abspath(os.path.join(workdir, f'{tag}_an_row3.fits'))
    write_fits_f32(src, plane[None].astype(np.float32), ['analytic removal input'])
    script = os.path.join(workdir, f'{tag}_an.nsc')
    with open(script, 'w') as f:
        f.write(f'open {src}\nwaitloaded\nremovestars 5 100 stars\nwaitloaded 300000\n'
                f'assert rows 3\nshow 2\nwaitloaded\nsave {o2}\nshow 3\nwaitloaded\nsave {o3}\nquit\n')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    r = subprocess.run([binary, '--run', script], env=env, capture_output=True, text=True)
    if r.returncode != 0 or 'failure(s)' not in r.stdout or '0 failure(s)' not in r.stdout:
        raise RuntimeError('NebulaScope removestars failed:\n' + r.stdout[-2000:] + r.stderr[-500:])
    a, b = _read2d(o2), _read2d(o3)
    # Rows 2/3 are the stars complement and the starless entry. The
    # complement is exactly zero away from stars; the starless row never
    # is. (A median test is ambiguous on a zero-background render, a
    # low-pass correlation is won by bright stars, and a peak test fails
    # where nebula outshines stars, as on the referee's Hubble truth.)
    return a if (a == 0).mean() < (b == 0).mean() else b


_DARKSTAR_DIR = '/opt/CosmicClarity'


def _run_darkstar(plane, workdir, tag):
    """SetiAstro Cosmic Clarity 'Dark Star' (neural). The CLI has no
    input/output arguments: it processes every file in its own input/
    folder into output/, so the run is serialized through those folders."""
    ind = os.path.join(_DARKSTAR_DIR, 'input'); outd = os.path.join(_DARKSTAR_DIR, 'output')
    for d in (ind, outd):
        for f in os.listdir(d):
            if f.endswith('.fits'):
                os.remove(os.path.join(d, f))
    ped = _pedestal(plane)
    scale = float((plane + ped).max()) or 1.0
    write_fits_2d(os.path.join(ind, f'{tag}.fits'), np.clip((plane + ped) / scale, 0, 1))
    subprocess.run([os.path.join(_DARKSTAR_DIR, 'setiastrocosmicclarity_darkstar'),
                    '--star_removal_mode', 'additive'], cwd=_DARKSTAR_DIR, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    out = _read2d(os.path.join(outd, f'{tag}_starless.fits')).reshape(plane.shape) * scale - ped
    for d in (ind, outd):
        for f in os.listdir(d):
            if f.startswith(tag):
                os.remove(os.path.join(d, f))
    return out


BACKENDS = ('sxt', 'starnet', 'analytic', 'darkstar')


def remove_stars(plane, backend, workdir, tag):
    if backend not in BACKENDS:
        raise ValueError(f'backend must be one of {BACKENDS}')
    os.makedirs(workdir, exist_ok=True)
    plane = np.nan_to_num(np.asarray(plane, dtype=np.float64))
    raw = {'sxt': _run_sxt, 'starnet': _run_starnet, 'analytic': _run_analytic,
           'darkstar': _run_darkstar}[backend](plane, workdir, tag)
    return _finish(plane, raw)

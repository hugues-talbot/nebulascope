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
BACKENDS = ('sxt', 'starnet', 'analytic')


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
    """Put a starless estimate on the input's scale (gain fitted on
    nebula pixels away from stars) and return (starless, stars)."""
    sl = _orient(starless_raw, plane)
    resid = plane - sl
    sig = np.median(np.abs(resid - np.median(resid))) / 0.6745 + 1e-12
    calm = np.abs(resid - np.median(resid)) < 3 * sig
    lo, hi = np.percentile(sl[calm], [5, 95])
    sel = calm & (sl > lo) & (sl < hi)
    g = np.polyfit(sl[sel].ravel(), plane[sel].ravel(), 1)[0] if sel.sum() > 1000 else 1.0
    sl = sl * g
    return sl, plane - sl


def _run_sxt(plane, workdir, tag):
    read_fits_f32, write_fits_f32 = _study_io()
    src = os.path.join(workdir, f'{tag}.fits')
    scale = float(np.percentile(plane, 99.9)) or 1.0
    write_fits_f32(src, (plane / scale)[None].astype(np.float32), ['sxt input'])
    subprocess.run(['rc-astro', '--no-banner', 'sxt', src, '--depth', '32F',
                    '-o', workdir + '/', '--overwrite'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return _read2d(os.path.join(workdir, f'{tag}-sxt.fits')) * scale


def _run_starnet(plane, workdir, tag):
    src = os.path.join(workdir, f'{tag}_sn_in.fits')
    dst = os.path.join(workdir, f'{tag}_sn_out.fits')
    scale = float(plane.max()) or 1.0
    write_fits_2d(src, np.clip(plane / scale, 0, 1))
    subprocess.run(['starnet2', '--linear', '-q', '-i', src, '-o', dst], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return _read2d(dst).reshape(plane.shape) * scale


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
    # rows: the starless entry keeps the sky level, the stars complement is ~0
    return a if abs(np.median(a) - np.median(plane)) < abs(np.median(b) - np.median(plane)) else b


def remove_stars(plane, backend, workdir, tag):
    if backend not in BACKENDS:
        raise ValueError(f'backend must be one of {BACKENDS}')
    os.makedirs(workdir, exist_ok=True)
    plane = np.nan_to_num(np.asarray(plane, dtype=np.float64))
    raw = {'sxt': _run_sxt, 'starnet': _run_starnet, 'analytic': _run_analytic}[backend](plane, workdir, tag)
    return _finish(plane, raw)

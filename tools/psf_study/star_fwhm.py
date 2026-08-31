#!/usr/bin/env python3
"""Linear-space stellar PSF measurement (FWHMEccentricity-style, but open).

Usage: star_fwhm.py <master.fits> [<solved.xisf-for-header>]

<master.fits> is a Float32 FITS dump (NebulaScope `save`); plate scale is
read from the XISF header (FOCALLEN/XPIXSZ) when given, else 0.7637"/px.

Per channel: detect stars, keep isolated unsaturated ones, fit each with an
ELLIPTICAL MOFFAT in linear space (Moffat because seeing profiles have wings
a Gaussian misfits, biasing FWHM low), filter by fit quality, report median
FWHM (px and arcsec), median eccentricity and its position angle, and write
a field map (FWHM number + elongation quiver per zone). Fits run on a
process pool — thousands of independent 27x27 least-squares problems.
"""
import sys
import os
import re
import struct
import multiprocessing as mp
import numpy as np
from scipy import ndimage, optimize

PLATE_DEFAULT = 0.7637


def read_fits_f32(path):
    """Minimal reader for NebulaScope's Float32 FITS output."""
    with open(path, 'rb') as f:
        hdr = b''
        while True:
            block = f.read(2880)
            hdr += block
            if b'END     ' in block:
                break
        cards = {}
        for i in range(0, len(hdr), 80):
            c = hdr[i:i+80].decode('ascii', 'ignore')
            m = re.match(r'([A-Z0-9_-]+)\s*=\s*([^/]+)', c)
            if m:
                cards[m.group(1)] = m.group(2).strip()
        w = int(cards['NAXIS1']); h = int(cards['NAXIS2'])
        nc = int(cards.get('NAXIS3', 1))
        assert int(cards['BITPIX']) == -32, 'expected Float32 FITS'
        data = np.frombuffer(f.read(w*h*nc*4), dtype='>f4').astype(np.float32)
    return data.reshape((nc, h, w)), cards


def plate_from_xisf(path):
    data = open(path, 'rb').read(400000).decode('utf-8', 'ignore')
    fl = re.search(r'name="FOCALLEN" value="\'?([\d.]+)', data)
    px = re.search(r'name="XPIXSZ" value="\'?([\d.]+)', data)
    if fl and px:
        return 206.265*float(px.group(1))/float(fl.group(1))
    return PLATE_DEFAULT


def detect(img, nsig=8.0, box=11, maxn=3000, sep=20):
    # Separable box background — median_filter(25) on a 61 Mpx frame takes
    # tens of minutes; a box high-pass is seconds and only feeds DETECTION
    # (the fit estimates its own local background per star).
    hp = img - ndimage.uniform_filter(img, 31)
    sd = 1.4826*np.median(np.abs(hp - np.median(hp)))
    mx = (hp == ndimage.maximum_filter(hp, box)) & (hp > nsig*sd)
    b = 20
    mx[:b, :] = mx[-b:, :] = False; mx[:, :b] = mx[:, -b:] = False
    ys, xs = np.nonzero(mx)
    amp = hp[ys, xs]
    order = np.argsort(amp)[::-1][:maxn]
    ys, xs, amp = ys[order], xs[order], amp[order]
    # isolation: reject any peak with a comparable neighbour within `sep` px
    keep = []
    pts = np.stack([xs, ys], 1).astype(float)
    from scipy.spatial import cKDTree
    tree = cKDTree(pts)
    for i in range(len(pts)):
        nb = tree.query_ball_point(pts[i], sep)
        if all(j == i or amp[j] < 0.2*amp[i] for j in nb):
            keep.append(i)
    return pts[keep], amp[keep]


def moffat(params, xx, yy):
    x0, y0, A, bg, sx, sy, th, beta = params
    ct, st = np.cos(th), np.sin(th)
    dx = xx - x0; dy = yy - y0
    u = ((dx*ct + dy*st)/sx)**2 + ((-dx*st + dy*ct)/sy)**2
    return bg + A*(1.0 + u)**(-beta)


def fit_one(args):
    cut, sat = args
    n = cut.shape[0]; h = n//2
    yy, xx = np.mgrid[0:n, 0:n].astype(float)
    bg0 = float(np.median(cut))
    A0 = float(cut.max() - bg0)
    if A0 <= 0 or cut.max() >= sat:
        return None
    p0 = [h, h, A0, bg0, 2.0, 2.0, 0.0, 2.5]
    lo = [h-3, h-3, 0.1*A0, bg0 - 5*abs(bg0) - 1e-3, 0.4, 0.4, -np.pi, 1.2]
    hi = [h+3, h+3, 3.0*A0, bg0 + 5*abs(bg0) + 1e-3, 12.0, 12.0, np.pi, 8.0]
    try:
        res = optimize.least_squares(
            lambda p: (moffat(p, xx, yy) - cut).ravel(), p0,
            bounds=(lo, hi), method='trf', max_nfev=200)
    except Exception:
        return None
    p = res.x
    model = moffat(p, xx, yy)
    resid = np.sqrt(np.mean((model - cut)**2))/max(1e-12, p[2])
    if resid > 0.05:                      # poor fit: crowding, nebulosity, defect
        return None
    k = 2.0*np.sqrt(2.0**(1.0/p[7]) - 1.0)
    f1, f2 = k*p[4], k*p[5]
    fmaj, fmin = max(f1, f2), min(f1, f2)
    pa = p[6] if f1 >= f2 else p[6] + np.pi/2
    pa = (np.degrees(pa) + 90.0) % 180.0 - 90.0
    ecc = np.sqrt(max(0.0, 1.0 - (fmin/fmaj)**2))
    return (np.sqrt(fmaj*fmin), fmaj, fmin, ecc, pa, p[0], p[1], p[7])


def main():
    fits_path = sys.argv[1]
    plate = plate_from_xisf(sys.argv[2]) if len(sys.argv) > 2 else PLATE_DEFAULT
    cube, cards = read_fits_f32(fits_path)
    print(f'{os.path.basename(fits_path)}: {cube.shape[2]}x{cube.shape[1]}x{cube.shape[0]}, '
          f'plate {plate:.4f} "/px')
    N = 13                                 # cutout half-size
    names = ['ch0', 'ch1', 'ch2'][:cube.shape[0]]
    for c, name in enumerate(names):
        img = cube[c]
        sat = np.nanpercentile(img, 99.999)
        pts, amp = detect(img)
        cuts = []
        locs = []
        for (x, y) in pts.astype(int):
            cut = img[y-N:y+N+1, x-N:x+N+1]
            if cut.shape != (2*N+1, 2*N+1) or not np.isfinite(cut).all():
                continue
            cuts.append((cut.astype(np.float64), float(sat)))
            locs.append((x, y))
        with mp.Pool(max(1, (os.cpu_count() or 4) - 2)) as pool:
            fits = pool.map(fit_one, cuts)
        good = [(f, l) for f, l in zip(fits, locs) if f is not None]
        if len(good) < 20:
            print(f'  {name}: only {len(good)} good fits — skipped')
            continue
        arr = np.array([f for f, _ in good])
        med = np.median(arr, axis=0)
        # circular median for PA (axial, period 180)
        pa2 = np.radians(arr[:, 4]*2)
        pa_med = 0.5*np.degrees(np.arctan2(np.median(np.sin(pa2)), np.median(np.cos(pa2))))
        print(f'  {name}: {len(good)} stars | FWHM {med[0]:.3f} px = {med[0]*plate:.2f}\" '
              f'(maj {med[1]*plate:.2f}\" / min {med[2]*plate:.2f}\") | '
              f'ecc {med[3]:.2f} @ PA {pa_med:+.0f} deg')
        # field map: 4x3 zones
        H, W = img.shape
        print('  field map (median FWHM px / ecc / PA):')
        for gy in range(3):
            row = []
            for gx in range(4):
                zone = [f for f, (x, y) in good
                        if gx*W/4 <= x < (gx+1)*W/4 and gy*H/3 <= y < (gy+1)*H/3]
                if len(zone) >= 5:
                    z = np.median(np.array(zone), axis=0)
                    zp = np.array(zone)
                    pz = np.radians(zp[:, 4]*2)
                    zpa = 0.5*np.degrees(np.arctan2(np.median(np.sin(pz)), np.median(np.cos(pz))))
                    row.append(f'{z[0]:4.2f}/{z[3]:.2f}/{zpa:+4.0f}')
                else:
                    row.append('      --      ')
            print('    ' + ' | '.join(row))


if __name__ == '__main__':
    main()

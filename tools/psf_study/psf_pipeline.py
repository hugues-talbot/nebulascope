#!/usr/bin/env python3
"""PSF-from-Hubble pipeline for the TEC140 2x2 BXT study (display-space).

Usage: psf_pipeline.py <target_image> [<hubble_reference>]

Without a reference argument, the right (Hubble) half of the TEC140-vs-Hubble
comparison PNG is used. Stages:
  1. Plate-scale link: exhaustive rot x scale correlation of the comparison's
     TEC left half against the target (no correspondences to fool), refined by
     star-pair similarity LSQ -> target arcsec/px through the calibrated chain
     (1 comp px = 0.260 master px; master plate = 0.7637 arcsec/px).
  2. Hubble -> target: same brute-force seed + star refinement.
  3. Warp Hubble into the target frame (checkerboard saved for eyeballing);
     Wiener kernel K = conj(Fh)Ft/(|Fh|^2+lam) on windowed band-passed data.
  4. FWHM: half-max area at x8 zoom (core) + clipped second moments (wings).

Display-space caveat: inputs are stretched exports, so the kernel is an
approximation of the optical+seeing PSF, good for FWHM comparison across
processing variants -- NOT valid for actual deconvolution (that needs the
linear master vs linear HST frames).
"""
import sys
import os
import multiprocessing as mp
import numpy as np
from PIL import Image
from scipy import ndimage
from numpy.fft import fft2, ifft2, fftshift

# Site configuration: the study's data live outside the repository. Point
# PSF_DATA at the folder holding the comparison PNG, reference images and
# exports (default: the working directory).
DATA = os.environ.get('PSF_DATA', os.getcwd())
COMP = os.path.join(DATA, 'Tec140-vs-Hubble_pillars_of_creation.png')
SEAM = 646
# MEASURED 2026-08-30 by registering a clean native export directly against
# the plate-solved master (same-source stars, 16 pairs, 0.12 px residual,
# scale 0.9999): user exports are at NATIVE master sampling. The original
# template-matched 0.260 was wrong by 1.69x and inflated every early result.
MASTER_PER_COMP = 0.4395         # master px per comparison px (via native-export link)
PLATE = 0.7637                   # arcsec per master px (FOCALLEN 1015.52, 3.76um)
ASEC_PER_COMP = MASTER_PER_COMP * PLATE

def lum(path):
    return np.asarray(Image.open(path).convert('RGB')).astype(np.float64).mean(axis=2)

def bandpass(a, lo=1.0, hi=12.0):
    return ndimage.gaussian_filter(a, lo) - ndimage.gaussian_filter(a, hi)

def hann2(shape):
    return np.outer(np.hanning(shape[0]), np.hanning(shape[1]))

def detect_stars(img, nsig=6.0, box=9, maxn=400):
    hp = img - ndimage.uniform_filter(img, 31)
    sd = 1.4826*np.median(np.abs(hp - np.median(hp)))
    mx = (hp == ndimage.maximum_filter(hp, box)) & (hp > nsig*sd)
    mx[:box, :] = mx[-box:, :] = False; mx[:, :box] = mx[:, -box:] = False
    ys, xs = np.nonzero(mx)
    order = np.argsort(hp[ys, xs])[::-1][:maxn]
    pts = []
    h = box//2
    for y, x in zip(ys[order], xs[order]):
        cut = np.clip(hp[y-h:y+h+1, x-h:x+h+1], 0, None)
        s = cut.sum()
        if s <= 0: continue
        gy, gx = np.mgrid[-h:h+1, -h:h+1]
        pts.append((x + (gx*cut).sum()/s, y + (gy*cut).sum()/s))
    return np.array(pts)

def sim_from_pairs(P, Q):
    """Least-squares similarity q = a*p + b over C (a = s*e^{i theta})."""
    zp = P[:, 0] + 1j*P[:, 1]; zq = Q[:, 0] + 1j*Q[:, 1]
    mp, mq = zp.mean(), zq.mean()
    a = np.vdot(zp - mp, zq - mq) / np.vdot(zp - mp, zp - mp)
    b = mq - a*mp
    A = np.zeros((3, 2))
    A[0] = [a.real, a.imag]; A[1] = [-a.imag, a.real]; A[2] = [b.real, b.imag]
    return A

def apply_aff(A, P):
    return np.hstack([P, np.ones((len(P), 1))]) @ A

def refine_affine(A, stars_ref, stars_tgt, rounds=8, tol0=6.0):
    """Iterative similarity LSQ with UNIQUE nearest-neighbour pairs."""
    tol = tol0
    npairs, med = 0, np.inf
    for _ in range(rounds):
        mapped = apply_aff(A, stars_ref)
        d = np.linalg.norm(mapped[:, None, :] - stars_tgt[None, :, :], axis=2)
        pairs = []
        used_r, used_t = set(), set()
        for idx in np.argsort(d, axis=None):
            i, j = np.unravel_index(idx, d.shape)
            if d[i, j] > tol: break
            if i in used_r or j in used_t: continue
            used_r.add(i); used_t.add(j); pairs.append((i, j))
        if len(pairs) < 6: break
        I = np.array([p[0] for p in pairs]); J = np.array([p[1] for p in pairs])
        A = sim_from_pairs(stars_ref[I], stars_tgt[J])
        resid = np.linalg.norm(apply_aff(A, stars_ref[I]) - stars_tgt[J], axis=1)
        npairs, med = len(pairs), np.median(resid)
        tol = max(3.0*med, 1.0)
    return A, npairs, med

_BF = {}                                        # per-worker canvases (spawn-safe)

def _bf_init(CR, Ft, nT, n):
    _BF.update(CR=CR, Ft=Ft, nT=nT, n=n)

def _bf_scan(args):
    """One scale, all rotations — returns that slice's best cell."""
    s, rots = args
    CR, Ft, nT, n = _BF['CR'], _BF['Ft'], _BF['nT'], _BF['n']
    c0 = (n-1)/2.0
    best = None
    for rot in rots:
        th = np.radians(rot)
        L = s*np.array([[np.cos(th), -np.sin(th)], [np.sin(th), np.cos(th)]])
        Linv = np.linalg.inv(L)
        M = np.array([[Linv[1, 1], Linv[1, 0]], [Linv[0, 1], Linv[0, 0]]])
        offv = np.array([c0, c0]) - M@np.array([c0, c0])
        w = ndimage.affine_transform(CR, M, offset=offv, order=1, mode='constant')
        nw = np.linalg.norm(w)
        if nw < 1e-6: continue
        cc = np.real(ifft2(fft2(w)*np.conj(Ft)))
        idx = np.unravel_index(np.argmax(cc), cc.shape)
        score = cc[idx]/(nw*nT)
        if best is None or score > best[0]:
            dy = idx[0] if idx[0] <= n//2 else idx[0]-n
            dx = idx[1] if idx[1] <= n//2 else idx[1]-n
            best = (score, float(s), float(rot), int(dx), int(dy), L)
    return best

def bruteforce_similarity(ref, tgt, scales, rots, n=1024):
    """Exhaustive rot x scale x shift search via band-passed cross-correlation,
    parallelized over the scale axis (one process per scale slice).

    Returns a full (3,2) affine ref->tgt (row-vector convention) and the score.
    """
    def canv(a):
        c = np.zeros((n, n)); c[:a.shape[0], :a.shape[1]] = bandpass(a, 1.5, 10)
        return c
    T = canv(tgt); Ft = fft2(T); nT = np.linalg.norm(T)
    CR = canv(ref)
    rots = list(rots)
    tasks = [(float(s), rots) for s in scales]
    nproc = min(len(tasks), max(1, (os.cpu_count() or 4) - 2))
    with mp.Pool(nproc, initializer=_bf_init, initargs=(CR, Ft, nT, n)) as pool:
        results = [r for r in pool.map(_bf_scan, tasks) if r is not None]
    best = max(results, key=lambda r: r[0])
    score, s, rot, dx, dy, L = best
    c0 = (n-1)/2.0
    # warped ref: q_w = L(p - c) + c ; correlation peak: tgt(x) ~ w(x + d)
    # => q_tgt = q_w - d = L p + (c - L c - d)
    cvec = np.array([c0, c0])
    t = cvec - L@cvec - np.array([dx, dy])
    A = np.zeros((3, 2)); A[:2, :] = L.T; A[2, :] = t
    return A, score, s, rot

def compose(A, B):
    """C such that applying C == apply A then B (all (3,2) row-vector affines)."""
    M = lambda X: np.vstack([np.hstack([X, np.array([[0], [0], [1.0]])])]).T
    C3 = M(B) @ M(A)
    return C3.T[:, :2]

def describe(A, tag):
    L = A[:2, :].T
    sx, sy = np.linalg.norm(L[:, 0]), np.linalg.norm(L[:, 1])
    rot = np.degrees(np.arctan2(L[1, 0], L[0, 0]))
    print(f'  {tag}: scale x={sx:.4f} y={sy:.4f} rot={rot:+.2f} deg')
    return 0.5*(sx+sy)

def warp_ref_to_tgt(ref, A, shape):
    M = np.vstack([A.T, [0, 0, 1]])
    Minv = np.linalg.inv(M)
    yy, xx = np.mgrid[0:shape[0], 0:shape[1]]
    src = Minv @ np.stack([xx.ravel(), yy.ravel(), np.ones(xx.size)])
    return ndimage.map_coordinates(ref, [src[1].reshape(shape), src[0].reshape(shape)],
                                   order=3, mode='constant')

def wiener_kernel(h, t, lam_rel=1e-3, ksz=41):
    w = hann2(h.shape)
    hb = bandpass(h, 0.0, 25.0) * w
    tb = bandpass(t, 0.0, 25.0) * w
    Fh, Ft = fft2(hb), fft2(tb)
    lam = lam_rel * np.abs(Fh).max()**2
    K = np.real(ifft2(np.conj(Fh)*Ft/(np.abs(Fh)**2 + lam)))
    K = fftshift(K)
    c = np.asarray(K.shape)//2
    k = K[c[0]-ksz//2:c[0]+ksz//2+1, c[1]-ksz//2:c[1]+ksz//2+1].copy()
    k = np.clip(k - np.median(K), 0, None)
    return k / k.sum()

def fwhm_halfmax_area(k, zoom=8):
    kz = ndimage.zoom(k, zoom, order=3)
    kz = np.clip(kz - np.median(kz), 0, None)
    area = (kz >= 0.5*kz.max()).sum() / zoom**2
    return 2.0*np.sqrt(area/np.pi)

def fwhm_moments(k, rclip=None):
    if rclip is None: rclip = k.shape[0]//2
    c = np.asarray(k.shape)//2
    yy, xx = np.mgrid[0:k.shape[0], 0:k.shape[1]]
    m = (yy-c[0])**2 + (xx-c[1])**2 <= rclip**2
    kk = np.where(m, np.clip(k, 0, None), 0)
    s = kk.sum()
    my = (yy*kk).sum()/s; mx = (xx*kk).sum()/s
    vy = ((yy-my)**2*kk).sum()/s; vx = ((xx-mx)**2*kk).sum()/s
    return 2.3548*np.sqrt(0.5*(vy+vx))

def main():
    tgt_path = sys.argv[1]
    ref_path = sys.argv[2] if len(sys.argv) > 2 else None
    comp = lum(COMP)
    hub = lum(ref_path) if ref_path else comp[:, SEAM+6:]
    left = comp[:, :SEAM-6]
    tgt = lum(tgt_path)
    print(f'hubble ref {hub.shape} ({ref_path or "comp right half"}), '
          f'TEC left {left.shape}, target {tgt.shape}')

    # --- 1. plate-scale link: TEC-left -> target ----------------------------
    S_left = detect_stars(left); S_tgt = detect_stars(tgt); S_hub = detect_stars(hub)
    print(f'stars: left {len(S_left)}, tgt {len(S_tgt)}, hub {len(S_hub)}')
    A0_LT, score, s0, r0 = bruteforce_similarity(left, tgt,
                                                 np.geomspace(0.15, 1.1, 24), range(0, 360, 4))
    print(f'brute force left->tgt: scale {s0:.3f} rot {r0} deg (score {score:.3f})')
    A0_LT, score, s0, r0 = bruteforce_similarity(left, tgt,
                                                 np.geomspace(s0*0.88, s0*1.14, 9),
                                                 np.arange(r0-4, r0+4.5, 1.0))
    print(f'  refined grid: scale {s0:.3f} rot {r0:.0f} deg (score {score:.3f})')
    A_LT, np_lt, med_lt = refine_affine(A0_LT, S_left, S_tgt, tol0=4.0)
    print(f'left->tgt stars: {np_lt} pairs, median resid {med_lt:.2f} px')
    s_t = describe(A_LT, 'left->tgt')

    # --- 2. Hubble -> target ------------------------------------------------
    if ref_path:
        A0_HT, score, s0, r0 = bruteforce_similarity(hub, tgt,
                                                     np.geomspace(0.1, 1.1, 28), range(0, 360, 4),
                                                     n=1280)
        print(f'brute force hub->tgt: scale {s0:.3f} rot {r0} deg (score {score:.3f})')
        A0_HT, score, s0, r0 = bruteforce_similarity(hub, tgt,
                                                     np.geomspace(s0*0.88, s0*1.14, 9),
                                                     np.arange(r0-4, r0+4.5, 1.0), n=1280)
        print(f'  refined grid: scale {s0:.3f} rot {r0:.0f} deg (score {score:.3f})')
    else:
        Aid = np.zeros((3, 2)); Aid[0, 0] = Aid[1, 1] = 1.0
        A_HL, np_hl, med_hl = refine_affine(Aid, S_hub, S_left, tol0=12.0)
        print(f'hub->left: {np_hl} pairs, median resid {med_hl:.2f} px')
        describe(A_HL, 'hub->left')
        A0_HT = compose(A_HL, A_LT)

    A, npairs, med = refine_affine(A0_HT, S_hub, S_tgt, tol0=5.0)
    if npairs < 6:
        print('direct hub->tgt refinement thin; keeping seed transform')
        A, npairs, med = A0_HT, np_lt, med_lt
    print(f'hub->tgt: {npairs} pairs, median resid {med:.2f} px')
    describe(A, 'hub->tgt')

    asec_per_tgt = ASEC_PER_COMP / s_t
    print(f'target plate scale: {asec_per_tgt:.4f} arcsec/px '
          f'(chain: {ASEC_PER_COMP:.4f} as/comp-px / {s_t:.4f} tgt-px/comp-px)')

    # --- 3. warp + kernel ---------------------------------------------------
    hub_w = warp_ref_to_tgt(hub, A, tgt.shape)
    def norm01(a):
        lo, hi = np.percentile(a, [1, 99.8]); return np.clip((a-lo)/(hi-lo+1e-9), 0, 1)
    cb = np.indices(tgt.shape).sum(0)//48 % 2
    board = np.where(cb == 0, norm01(hub_w), norm01(tgt))
    chk = tgt_path.rsplit('.', 1)[0] + '_regcheck.png'
    Image.fromarray((255*board).astype(np.uint8)).save(chk)
    print('registration checkerboard:', chk)

    k = wiener_kernel(hub_w, tgt)
    f_area = fwhm_halfmax_area(k)
    f_mom = fwhm_moments(k, rclip=12)
    print(f'kernel FWHM: half-max-area {f_area:.2f} px, clipped moments {f_mom:.2f} px')
    print(f'==> PSF FWHM: {f_area*asec_per_tgt:.2f}" (area) / {f_mom*asec_per_tgt:.2f}" (moments)')

    out = tgt_path.rsplit('.', 1)[0] + '_kernel.png'
    kk = k/k.max()
    Image.fromarray((255*np.clip(ndimage.zoom(kk, 6, order=1), 0, 1)).astype(np.uint8)).save(out)
    print('kernel image:', out)

if __name__ == '__main__':
    main()

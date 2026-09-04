"""Star removal on every sub of a patch cube (RC-Astro StarXTerminator CLI).

    sxt_subs.py <patch_cube.fits>

Writes <cube>_starless.fits and <cube>_stars.fits beside the input (same
[N,h,w] layout). Used by proper_coadd.py --data: the PSF of each sub is
measured on the STARRY cube, the estimator runs on the STARLESS one —
star cores (saturated, sharper than any Moffat) are the one thing that
violates the convolution model, and their ringing moats were the S II
proper coadd's whole defect. Requires `rc-astro` on PATH.
"""
import sys, os, subprocess
import numpy as np

def main():
    sys.path.insert(0, os.environ.get('PSF_DATA', '.'))
    from star_fwhm import read_fits_f32
    from full_deconv import write_fits_f32
    cube_path = sys.argv[1]
    cube = np.asarray(read_fits_f32(cube_path)[0], dtype=np.float64)
    N = cube.shape[0]
    work = cube_path + '.sxt_work'
    os.makedirs(work, exist_ok=True)
    starless, stars = np.empty_like(cube, dtype=np.float32), np.empty_like(cube, dtype=np.float32)
    for i in range(N):
        sub = np.nan_to_num(cube[i])
        scale = float(np.percentile(sub, 99.9)) or 1.0
        src = os.path.join(work, f'sub{i:04d}.fits')
        write_fits_f32(src, (sub/scale)[None].astype(np.float32), ['sxt input'])
        subprocess.run(['rc-astro', '--no-banner', 'sxt', src, '--stars', '--depth', '32F',
                        '-o', work + '/', '--overwrite'], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for suffix, dst in (('-sxt', starless), ('-sxt-stars', stars)):
            a = np.asarray(read_fits_f32(os.path.join(work, f'sub{i:04d}{suffix}.fits'))[0],
                           dtype=np.float64)[0]*scale
            f = np.flipud(a)          # rc-astro writes standard FITS; ours is top-down
            r = lambda x: np.corrcoef(x.ravel(), sub.ravel())[0, 1]
            dst[i] = (f if r(f) > r(a) else a).astype(np.float32)
        # SXT normalizes by the frame MAX, not our p99.9: recover the exact
        # gain from starless+stars vs the input (it reconstructs it to
        # correlation 1.0) so every sub keeps its own units.
        rec = starless[i].astype(np.float64) + stars[i]
        g = np.polyfit(rec.ravel(), sub.ravel(), 1)[0]
        starless[i] *= g; stars[i] *= g
        for suffix in ('', '-sxt', '-sxt-stars'):
            try: os.remove(os.path.join(work, f'sub{i:04d}{suffix}.fits'))
            except OSError: pass
        if (i + 1) % 20 == 0:
            print(f'  {i+1}/{N}')
    base = cube_path[:-5] if cube_path.endswith('.fits') else cube_path
    write_fits_f32(base + '_starless.fits', starless, [f'SXT starless subs from {os.path.basename(cube_path)}'])
    write_fits_f32(base + '_stars.fits', stars, [f'SXT star images from {os.path.basename(cube_path)}'])
    print('written:', base + '_starless.fits', base + '_stars.fits')

if __name__ == '__main__':
    main()

"""Star removal on every sub of a patch cube, with a chosen backend.

    remove_subs.py --backend {sxt|starnet|analytic} <patch_cube.fits>

Writes <cube>_starless_<backend>.fits and <cube>_stars_<backend>.fits
beside the input ([N,h,w], sub units, stars = sub - starless exactly).
Generalizes sxt_subs.py (whose outputs are the `sxt` backend under the
older names patch_F_starless.fits / patch_F_stars.fits) so the starless
proper coadd and the metric-v2 referee can be A/B'd across removers —
including one with no learned prior at all (`analytic`).
"""
import sys, os
import numpy as np

def main():
    sys.path.insert(0, os.environ.get('PSF_DATA', '.'))
    from star_fwhm import read_fits_f32
    from full_deconv import write_fits_f32
    from star_removers import remove_stars, BACKENDS
    args = sys.argv[1:]
    backend = 'sxt'
    if '--backend' in args:
        i = args.index('--backend'); backend = args[i+1]; del args[i:i+2]
    if not args or backend not in BACKENDS:
        raise SystemExit(__doc__)
    cube_path = args[0]
    cube = np.asarray(read_fits_f32(cube_path)[0], dtype=np.float64)
    N = cube.shape[0]
    work = cube_path + f'.{backend}_work'
    starless = np.empty_like(cube, dtype=np.float32)
    stars = np.empty_like(cube, dtype=np.float32)
    for i in range(N):
        sl, st = remove_stars(cube[i], backend, work, f'sub{i:04d}')
        starless[i], stars[i] = sl, st
        for f in os.listdir(work):
            if f.startswith(f'sub{i:04d}'):
                try: os.remove(os.path.join(work, f))
                except OSError: pass
        if (i + 1) % 10 == 0:
            print(f'  {i+1}/{N}', flush=True)
    base = cube_path[:-5] if cube_path.endswith('.fits') else cube_path
    write_fits_f32(base + f'_starless_{backend}.fits', starless,
                   [f'{backend} starless subs from {os.path.basename(cube_path)}'])
    write_fits_f32(base + f'_stars_{backend}.fits', stars,
                   [f'{backend} star images from {os.path.basename(cube_path)}'])
    print('written:', base + f'_starless_{backend}.fits', base + f'_stars_{backend}.fits')

if __name__ == '__main__':
    main()

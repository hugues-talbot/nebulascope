import sys, os
sys.path.insert(0, '/Users/talboth/Projects/Claude/Data/Nebulascope')
import numpy as np
from scipy import ndimage
from psf_pipeline import bruteforce_similarity, refine_affine, detect_stars, compose
from star_fwhm import read_fits_f32
from full_deconv import write_fits_f32
from linear_deconv import regprep, warp_to, ROI_CX, ROI_CY, ROI_R
from PIL import Image

def main():
    SP = os.path.dirname(os.path.abspath(__file__))
    x0, x1 = ROI_CX - ROI_R, ROI_CX + ROI_R
    y0, y1 = ROI_CY - ROI_R, ROI_CY + ROI_R
    variants = ["m16_full_raw", "m16_full_BXT", "m16_full_BXT5", "m16_full_deconv",
                "m16_lucky_raw", "m16_lucky_BXT", "m16_lucky_BXT5", "m16_lucky_deconv"]
    for v in variants:
        cube, _ = read_fits_f32(os.path.join(SP, f"{v}.fits"))
        roi = np.stack([np.nan_to_num(cube[c][y0:y1, x0:x1]) for c in range(3)]).astype(np.float32)
        write_fits_f32(os.path.join(SP, f"roi_{v}.fits"), roi, [f"ROI crop {x0},{y0} 520x520 of {v}"])
    print("8 ROI crops written")

    # Register the processed Hubble webp (display RGB) onto the 2x ROI grid.
    cube, _ = read_fits_f32(os.path.join(SP, "m16_full_raw.fits"))
    m = ndimage.zoom(np.nan_to_num(cube[1][y0:y1, x0:x1].astype(np.float64)), 2, order=3)
    m_reg = regprep(m)
    rgb = np.asarray(Image.open('/Users/talboth/Projects/Claude/Data/Nebulascope/pillars_of_creation_Hubble.webp'),
                     dtype=np.float64) / 255.0
    lum = rgb.mean(axis=2)
    A0, score, s0, r0 = bruteforce_similarity(regprep(lum), m_reg,
                            np.geomspace(0.30, 1.10, 14), range(0, 360, 4), n=2048)
    print("seed", score, s0, r0)
    A0, score, s0, r0 = bruteforce_similarity(regprep(lum), m_reg,
                            np.geomspace(s0*0.94, s0*1.06, 7), np.arange(r0-3, r0+3.5, 1.0), n=2048)
    Sh, Sm = detect_stars(regprep(lum), maxn=600), detect_stars(m_reg, maxn=600)
    A, npairs, med = refine_affine(A0, Sh, Sm, tol0=5.0)
    print(f"webp registered: score {score:.3f}, {npairs} pairs, {med:.2f} px")
    warped = np.stack([warp_to(rgb[:, :, c], A, m.shape) for c in range(3)], axis=-1)
    bin2 = lambda a: 0.25*(a[0::2,0::2]+a[1::2,0::2]+a[0::2,1::2]+a[1::2,1::2])
    ref = np.stack([bin2(warped[:, :, c]) for c in range(3)], axis=-1)   # 520 native
    Image.fromarray((255*np.clip(ref,0,1)).astype(np.uint8)).save(os.path.join(SP, "webp_roi.png"))
    # Hubble-at-our-resolution: blur to 1.9 arcsec (sigma = 2.49px/2.355)
    sig = (1.9/0.76371)/2.3548
    blur = np.stack([ndimage.gaussian_filter(ref[:, :, c], sig) for c in range(3)], axis=-1)
    Image.fromarray((255*np.clip(blur,0,1)).astype(np.uint8)).save(os.path.join(SP, "webp_roi_blur.png"))
    cov = (ref.sum(axis=2) > 0.01)
    print("reference coverage of ROI:", float(cov.mean()))

if __name__ == '__main__':
    main()

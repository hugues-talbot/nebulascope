"""Ten-panel montage of the Hubble-overlap audit: the eight variants
(full/lucky x raw/ML4/ML5/deconv) plus Hubble native and Hubble degraded
to the common 1.3 arcsec, on the audit ROI and on the strictly-covered
common band. Companion to ml5_audit.py: same dumps, same registrations,
same rectangles. Needs PSF_DATA and the eight m16_*.fits dumps."""
import sys, os
sys.path.insert(0, '/Users/talboth/Projects/Claude/Data/Nebulascope')
import numpy as np
from scipy import ndimage
from psf_pipeline import bruteforce_similarity, refine_affine, detect_stars, compose
from star_fwhm import read_fits_f32
from linear_deconv import (load_hst, regprep, warp_to, gauss_psf, conv, inscribed_rect,
                           HST_FILES, ROI_CX, ROI_CY, ROI_R, GRID)
from PIL import Image, ImageDraw

def main():
    SP = os.path.dirname(os.path.abspath(__file__))
    x0, x1 = ROI_CX - ROI_R, ROI_CX + ROI_R
    y0, y1 = ROI_CY - ROI_R, ROI_CY + ROI_R
    n2 = 4*ROI_R

    def show(ch, lo=1, hi=99.7):
        a, b = np.percentile(ch, [lo, hi])
        return np.sqrt(np.clip((ch - a)/(b - a + 1e-12), 0, 1))

    def colorize(cube):   # SHO -> RGB, study display convention per channel
        return np.stack([show(cube[i]) for i in range(3)], axis=-1)

    variants = [
        ("raw full",    "m16_full_raw.fits"),   ("BXT ML4 full",  "m16_full_BXT.fits"),
        ("BXT ML5 full", "m16_full_BXT5.fits"), ("deconv full",   "m16_full_deconv.fits"),
        ("raw lucky",   "m16_lucky_raw.fits"),  ("BXT ML4 lucky", "m16_lucky_BXT.fits"),
        ("BXT ML5 lucky","m16_lucky_BXT5.fits"),("deconv lucky",  "m16_lucky_deconv.fits"),
    ]
    panels = {}
    for name, f in variants:
        cube, _ = read_fits_f32(os.path.join(SP, f))
        roi = np.stack([np.nan_to_num(cube[c][y0:y1, x0:x1].astype(np.float64)) for c in range(3)])
        panels[name] = colorize(roi)

    # Hubble: register each filter to the raw-full ROI (audit machinery, 2x grid)
    cube, _ = read_fits_f32(os.path.join(SP, "m16_full_raw.fits"))
    hst_ch = []
    for c, ch in enumerate(['S', 'H', 'O']):
        m = ndimage.zoom(np.nan_to_num(cube[c][y0:y1, x0:x1].astype(np.float64)), 2, order=3)
        m_reg = regprep(m)
        hst = np.fliplr(load_hst(HST_FILES[ch]))
        hst8 = np.fliplr(load_hst(HST_FILES[ch], binf=8))
        A0, score, s0, r0 = bruteforce_similarity(regprep(hst8), m_reg,
                                np.geomspace(0.80, 0.88, 5), np.arange(-67.0, -60.5, 1.0), n=2048)
        Sh, Sm = detect_stars(regprep(hst8), maxn=600), detect_stars(m_reg, maxn=600)
        A8, npairs, med = refine_affine(A0, Sh, Sm, tol0=4.0)
        Ahalf = np.zeros((3, 2)); Ahalf[0, 0] = Ahalf[1, 1] = 0.5
        A = compose(Ahalf, A8)
        hw = warp_to(hst, A, m.shape)
        # The audit clips the warped mosaic at p99.8 (saturated HST cores and
        # seam pixels run huge and would own any percentile stretch).
        pos = hw[hw > 0]
        hwc = np.clip(hw, 0, np.percentile(pos, 99.8))
        valid = ndimage.binary_erosion(hst > 0.01, iterations=4)
        cov_c = warp_to(valid.astype(np.float64), A, m.shape) > 0.98
        cover = cov_c if c == 0 else (cover & cov_c)
        # numeric alignment check: band-passed correlation on covered pixels
        cov = hw > 0
        cc = np.corrcoef(regprep(hwc)[cov].ravel(), m_reg[cov].ravel())[0, 1]
        print(f'[{ch}] covered-region corr {cc:.3f}')
        hst_ch.append(hwc)
        print(f'[{ch}] {npairs} pairs {med:.2f}px')
    hst_cube = np.stack(hst_ch)
    fy0, fy1, fx0, fx1 = inscribed_rect(cover, 0, n2//2)
    vy0, vy1, vx0, vx1 = inscribed_rect(cover, n2//2 + n2//20, n2)
    ry0, ry1, rx0, rx1 = max(fy0, vy0), min(fy1, vy1), fx0, vx1
    print("common rect (2x grid):", ry0, ry1, rx0, rx1)
    crops = {n: p[ry0//2:ry1//2, rx0//2:rx1//2] for n, p in panels.items()}
    t = gauss_psf(1.3/GRID)
    hst13 = np.stack([conv(hst_cube[c], t) for c in range(3)])
    bin2 = lambda a: 0.25*(a[0::2,0::2]+a[1::2,0::2]+a[0::2,1::2]+a[1::2,1::2])
    def colorize_masked(cube):
        chans = []
        for i in range(3):
            ch = cube[i]
            pos = ch[ch > 0]
            a, b = np.percentile(pos, [2, 99.5])
            t = np.clip((ch - a)/(b - a + 1e-12), 0, 1)
            chans.append(np.arcsinh(8*t)/np.arcsinh(8) * (ch > 0))
        return np.stack(chans, axis=-1)
    panels["Hubble native"] = colorize_masked(np.stack([bin2(hst_cube[c]) for c in range(3)]))
    panels["Hubble @1.3″"] = colorize_masked(np.stack([bin2(hst13[c]) for c in range(3)]))
    for n in ("Hubble native", "Hubble @1.3″"):
        crops[n] = panels[n][ry0//2:ry1//2, rx0//2:rx1//2]

    order = [["raw full", "BXT ML4 full", "BXT ML5 full", "deconv full", "Hubble @1.3″"],
             ["raw lucky", "BXT ML4 lucky", "BXT ML5 lucky", "deconv lucky", "Hubble native"]]
    def render(pans, path, H0):
        LB = 28
        sample = pans[order[0][0]]
        W0 = int(round(H0 * sample.shape[1] / sample.shape[0]))
        img = Image.new("RGB", (5*W0, 2*(H0+LB)), (12,12,16))
        dr = ImageDraw.Draw(img)
        for r, row in enumerate(order):
            for cidx, name in enumerate(row):
                p = (255*np.clip(pans[name],0,1)).astype(np.uint8)
                img.paste(Image.fromarray(p).resize((W0,H0), Image.LANCZOS), (cidx*W0, r*(H0+LB)))
                dr.text((cidx*W0+8, r*(H0+LB)+H0+6), name, fill=(230,230,235))
        img.save(path)
    render(panels, os.path.join(SP, "hubble_field_tenway.png"), 520)
    render(crops, os.path.join(SP, "hubble_field_common.png"), 300)
    print("both montages saved")
    return
    img = Image.new("RGB", (5*W0, 2*(H0+LB)), (12,12,16))
    dr = ImageDraw.Draw(img)
    for r, row in enumerate(order):
        for cidx, name in enumerate(row):
            p = (255*np.clip(panels[name],0,1)).astype(np.uint8)
            img.paste(Image.fromarray(p).resize((W0,H0), Image.LANCZOS), (cidx*W0, r*(H0+LB)))
            dr.text((cidx*W0+8, r*(H0+LB)+H0+6), name, fill=(230,230,235))
    img.save(os.path.join(SP, "hubble_field_tenway.png"))
    print("montage saved")

if __name__ == '__main__':
    main()

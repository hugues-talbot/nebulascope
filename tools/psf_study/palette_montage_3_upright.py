import sys, os
sys.path.insert(0, '/Users/talboth/Projects/Claude/Data/Nebulascope')
import numpy as np
from scipy import ndimage
from scipy.optimize import least_squares
from psf_pipeline import bruteforce_similarity, refine_affine, detect_stars, compose
from star_fwhm import read_fits_f32
from linear_deconv import regprep, warp_to, ROI_CX, ROI_CY, ROI_R
from PIL import Image, ImageDraw, ImageFont

def main():
    SP = os.path.dirname(os.path.abspath(__file__))
    x0, x1 = ROI_CX - ROI_R, ROI_CX + ROI_R
    y0, y1 = ROI_CY - ROI_R, ROI_CY + ROI_R

    # --- register webp -> 2x master ROI grid (as before) ---
    cube, _ = read_fits_f32(os.path.join(SP, "m16_full_raw.fits"))
    m = ndimage.zoom(np.nan_to_num(cube[1][y0:y1, x0:x1].astype(np.float64)), 2, order=3)
    m_reg = regprep(m)
    rgb = np.asarray(Image.open('/Users/talboth/Projects/Claude/Data/Nebulascope/pillars_of_creation_Hubble.webp'),
                     dtype=np.float64) / 255.0
    lum = rgb.mean(axis=2)
    A0, score, s0, r0 = bruteforce_similarity(regprep(lum), m_reg,
                            np.geomspace(0.30, 1.10, 14), range(0, 360, 4), n=2048)
    A0, score, s0, r0 = bruteforce_similarity(regprep(lum), m_reg,
                            np.geomspace(s0*0.94, s0*1.06, 7), np.arange(r0-3, r0+3.5, 1.0), n=2048)
    Sh, Sm = detect_stars(regprep(lum), maxn=600), detect_stars(m_reg, maxn=600)
    A, npairs, med = refine_affine(A0, Sh, Sm, tol0=5.0)
    print(f"webp registered: {npairs} pairs, {med:.2f} px")
    M3 = np.vstack([A.T, [0, 0, 1]])
    Ainv = np.linalg.inv(M3)[:2, :].T          # 2x-grid -> webp coords

    # --- shared palette transform (fit on raw-full over covered rect) ---
    rx, ry, rw, rh = np.load(f"{SP}/fit_rect.npy")
    ref520 = np.asarray(Image.open(f"{SP}/webp_roi.png"), dtype=np.float64)/255.0
    def mtf(x, mm): return np.clip(((mm-1.0)*x)/(((2.0*mm-1.0)*x)-mm + 1e-12), 0, 1)
    def fwd(c3, p):
        c = p[0:3]; lm = p[3:6]; MM = p[6:15].reshape(3,3)
        y = np.stack([mtf(np.clip((c3[i]-c[i])/(1.0-c[i]+1e-9), 0, 1), np.exp(lm[i])) for i in range(3)])
        return np.clip(np.einsum('ij,jhw->ihw', MM, y), 0, 1)
    src, _ = read_fits_f32(f"{SP}/roi_m16_full_raw.fits")
    sflat = np.nan_to_num(src.astype(np.float64))[:, ry:ry+rh, rx:rx+rw].reshape(3, -1)
    tflat = ref520[ry:ry+rh, rx:rx+rw].transpose(2,0,1).reshape(3, -1)
    idx = np.random.default_rng(1).choice(sflat.shape[1], 60000, replace=False)
    sflat, tflat = sflat[:, idx], tflat[:, idx]
    p0 = np.zeros(15)
    for i in range(3):
        med0 = np.median(sflat[i]); mad = np.median(np.abs(sflat[i]-med0))
        c0 = max(0.0, med0 - 2.8*1.4826*mad); p0[i] = c0
        b = max(1e-9, (med0-c0)/(1.0-c0))
        p0[3+i] = np.log(max(((0.25-1.0)*b)/(((0.5-1.0)*b)-0.25), 1e-6))
    p0[6:15] = np.eye(3).ravel()
    def resid(p):
        y = np.einsum('ij,jk->ik', p[6:15].reshape(3,3), np.stack(
            [mtf(np.clip((sflat[i]-p[i])/(1.0-p[i]+1e-9), 0, 1), np.exp(p[3+i])) for i in range(3)]))
        return (np.clip(y,0,1) - tflat).ravel()
    fit = least_squares(resid, p0, loss='soft_l1', f_scale=0.1, max_nfev=200)
    print("palette fit rms", round(float(np.sqrt(np.mean(fit.fun**2))), 4))

    # --- render panels UPRIGHT: transform, upsample x2, inverse-warp to webp frame ---
    wshape = lum.shape
    variants = [("raw · full", "m16_full_raw"), ("BXT ML4 · full", "m16_full_BXT"),
                ("BXT ML5 · full", "m16_full_BXT5"), ("deconv 1.8″ · full", "m16_full_deconv"),
                ("Hubble (reference)", None),
                ("raw · lucky", "m16_lucky_raw"), ("BXT ML4 · lucky", "m16_lucky_BXT"),
                ("BXT ML5 · lucky", "m16_lucky_BXT5"), ("deconv 1.8″ · lucky", "m16_lucky_deconv"),
                ("Hubble blurred to 1.9″", None)]
    panels = {}
    for name, v in variants:
        if not v: continue
        c3, _ = read_fits_f32(f"{SP}/roi_{v}.fits")
        disp = fwd(np.nan_to_num(c3.astype(np.float64)), fit.x)
        up = np.stack([ndimage.zoom(disp[i], 2, order=1) for i in range(3)])
        panels[name] = np.stack([warp_to(up[i], Ainv, wshape) for i in range(3)], axis=-1)
    covm = warp_to(np.ones_like(m), Ainv, wshape) > 0.98
    panels["Hubble (reference)"] = rgb
    sig = (1.9/0.76371) * (2.0/s0) / 2.3548  # 1.9 arcsec in webp px (webp px per master px = 2/s0)
    panels["Hubble blurred to 1.9″"] = np.stack(
        [ndimage.gaussian_filter(rgb[:, :, i], sig) for i in range(3)], axis=-1)
    print("amateur coverage of webp frame:", float(covm.mean()), "blur sigma(px)", round(sig,2))

    # crop to region covered by amateur data (largest inscribed rect)
    from linear_deconv import inscribed_rect
    r = inscribed_rect(covm, 0, wshape[1])
    cy0, cy1, cx0, cx1 = r
    print("upright rect:", r)
    try: font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 26)
    except Exception: font = ImageFont.load_default()
    LB = 40; scale = 2
    W, H = (cx1-cx0)*scale, (cy1-cy0)*scale
    out = Image.new("RGB", (5*W, 2*(H+LB)), (10, 10, 14))
    dr = ImageDraw.Draw(out)
    for i, (name, _) in enumerate(variants):
        p = panels[name][cy0:cy1, cx0:cx1]
        im = Image.fromarray((255*np.clip(p, 0, 1)).astype(np.uint8))
        im = im.resize((W, H), Image.NEAREST)
        rr, cc = divmod(i, 5)
        out.paste(im, (cc*W, rr*(H+LB)))
        dr.text((cc*W+10, rr*(H+LB)+H+7), name, fill=(235, 235, 240), font=font)
    out.save(f"{SP}/montage_hubblepal_upright.png")
    print("upright montage:", out.size)

if __name__ == '__main__':
    main()

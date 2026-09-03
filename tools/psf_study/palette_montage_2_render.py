import sys, os
sys.path.insert(0, '/Users/talboth/Projects/Claude/Data/Nebulascope')
import numpy as np
from scipy.optimize import least_squares
from star_fwhm import read_fits_f32
from PIL import Image, ImageDraw, ImageFont

def main():
    SP = os.path.dirname(os.path.abspath(__file__))
    rx, ry, rw, rh = np.load(f"{SP}/fit_rect.npy")
    ref = np.asarray(Image.open(f"{SP}/webp_roi.png"), dtype=np.float64)/255.0

    def mtf(x, m):
        return np.clip(((m-1.0)*x)/(((2.0*m-1.0)*x)-m + 1e-12), 0, 1)

    def fwd(cube, p):
        # per-channel clip c + midtone m (log-parametrized), then 3x3 mixer
        c = p[0:3]; lm = p[3:6]; M = p[6:15].reshape(3,3)
        y = np.stack([mtf(np.clip((cube[i]-c[i])/(1.0-c[i]+1e-9), 0, 1), np.exp(lm[i]))
                      for i in range(3)])
        out = np.einsum('ij,jhw->ihw', M, y)
        return np.clip(out, 0, 1)

    src, _ = read_fits_f32(f"{SP}/roi_m16_full_raw.fits")
    src = np.nan_to_num(src.astype(np.float64))
    sc = src[:, ry:ry+rh, rx:rx+rw]
    tc = ref[ry:ry+rh, rx:rx+rw].transpose(2, 0, 1)
    idx = np.random.default_rng(1).choice(sc.shape[1]*sc.shape[2], 60000, replace=False)
    sflat = sc.reshape(3, -1)[:, idx]
    tflat = tc.reshape(3, -1)[:, idx]

    p0 = np.zeros(15)
    for i in range(3):
        med = np.median(sflat[i]); mad = np.median(np.abs(sflat[i]-med))
        c0 = max(0.0, med - 2.8*1.4826*mad)
        p0[i] = c0
        b = max(1e-9, (med - c0)/(1.0 - c0))
        m0 = ((0.25-1.0)*b)/(((2*0.25-1.0)*b)-0.25)
        p0[3+i] = np.log(max(m0, 1e-6))
    p0[6:15] = np.eye(3).ravel()

    def resid(p):
        y = np.einsum('ij,jk->ik', p[6:15].reshape(3,3), np.stack(
            [mtf(np.clip((sflat[i]-p[i])/(1.0-p[i]+1e-9), 0, 1), np.exp(p[3+i])) for i in range(3)]))
        return (np.clip(y, 0, 1) - tflat).ravel()

    r = least_squares(resid, p0, loss='soft_l1', f_scale=0.1, max_nfev=200)
    rms = np.sqrt(np.mean(r.fun**2))
    print("fit rms", round(rms, 4), "clips", np.round(r.x[0:3], 5), "mids", np.round(np.exp(r.x[3:6]), 5))

    variants = [("raw · full", "m16_full_raw"), ("BXT ML4 · full", "m16_full_BXT"),
                ("BXT ML5 · full", "m16_full_BXT5"), ("deconv 1.8″ · full", "m16_full_deconv"),
                ("Hubble (reference)", None),
                ("raw · lucky", "m16_lucky_raw"), ("BXT ML4 · lucky", "m16_lucky_BXT"),
                ("BXT ML5 · lucky", "m16_lucky_BXT5"), ("deconv 1.8″ · lucky", "m16_lucky_deconv"),
                ("Hubble blurred to 1.9″", None)]
    panels = {}
    for name, v in variants:
        if v:
            cube, _ = read_fits_f32(f"{SP}/roi_{v}.fits")
            img = fwd(np.nan_to_num(cube.astype(np.float64)), r.x).transpose(1, 2, 0)
            panels[name] = img
    panels["Hubble (reference)"] = ref
    panels["Hubble blurred to 1.9″"] = np.asarray(Image.open(f"{SP}/webp_roi_blur.png"), dtype=np.float64)/255.0

    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 26)
    except Exception:
        font = ImageFont.load_default()

    def build(crop, scale, path):
        LB = 40
        if crop: W, H = int(rw)*scale, int(rh)*scale
        else:
            s0 = panels[variants[0][0]]
            W, H = s0.shape[1]*scale, s0.shape[0]*scale
        out = Image.new("RGB", (5*W, 2*(H+LB)), (10, 10, 14))
        dr = ImageDraw.Draw(out)
        for i, (name, _) in enumerate(variants):
            p = panels[name]
            if crop: p = p[ry:ry+rh, rx:rx+rw]
            im = Image.fromarray((255*np.clip(p, 0, 1)).astype(np.uint8))
            im = im.resize((im.width*scale, im.height*scale), Image.NEAREST)
            r_, c_ = divmod(i, 5)
            out.paste(im, (c_*W, r_*(H+LB)))
            dr.text((c_*W+10, r_*(H+LB)+H+7), name, fill=(235, 235, 240), font=font)
        out.save(path)
        print(os.path.basename(path), out.size)

    build(False, 2, f"{SP}/montage_hubblepal_roi.png")
    build(True, 3, f"{SP}/montage_hubblepal_common.png")

if __name__ == '__main__':
    main()

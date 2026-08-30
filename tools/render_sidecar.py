#!/usr/bin/env python3
"""
render_sidecar.py — reference implementation of the NebulaScope display block.

Renders an image exactly as NebulaScope shows it, from the "display" block of
its annotation sidecar ("<image>_annotation.json"), with NO NebulaScope code:
this is the executable specification of the format, so the same appearance
can be reproduced in any software. See docs/TRANSPORT.md §6 for the written
spec; every equation there is a line here.

    python3 render_sidecar.py image.fits [image_annotation.json] [-o out.png]
                              [--float out.npy] [--sidecar-json '{...}']

Output: an 8-bit PNG (plain rounding, no dither — dither is a display-time
cosmetic and deliberately NOT part of the format), and optionally the exact
Float32 [0,1] planes NebulaScope's own "renderFloat" produces (what Save
Stretched As… bakes). The Float32 output is what tests/conformance compares
against the C++ renderer, plane by plane.

Scope of this reference: Linear / Log / Asinh / GHS transfer functions,
per-channel windowing, tone and colour adjustments, mono and RGB images.
False-colour maps (cmap other than "gray", or the invert/split modifiers)
are out of scope here: they are 8-bit lookup tables defined by control
points in src/core/Colormap.cpp — authoritative there; a mono image with an
active colormap renders through the same stretch and is then indexed into
that table.

Dependencies: numpy; astropy (FITS) or Pillow (PNG/JPEG/TIFF) as needed.
"""
import argparse
import json
import math
import os
import sys

import numpy as np

# ---- I/O ---------------------------------------------------------------------

def _read_fits_primary(path):
    """Minimal stdlib FITS reader for the simple images this tool meets:
    primary HDU, NAXIS 2 or 3, any BITPIX, BZERO/BSCALE honoured. Returns
    (data as float64 in physical units, bitpix, bzero). Falls back to
    astropy (if installed) for anything it does not understand."""
    with open(path, "rb") as f:
        hdr = {}
        while True:
            block = f.read(2880)
            if len(block) < 2880:
                raise SystemExit("truncated FITS header")
            end = False
            for i in range(0, 2880, 80):
                card = block[i:i + 80].decode("ascii", "replace")
                key = card[:8].strip()
                if key == "END":
                    end = True
                    break
                if card[8:10] == "= ":
                    val = card[10:].split("/")[0].strip()
                    hdr[key] = val
            if end:
                break
        bitpix = int(hdr["BITPIX"])
        naxis = int(hdr["NAXIS"])
        if naxis not in (2, 3):
            raise ValueError("NAXIS %d" % naxis)
        dims = [int(hdr["NAXIS%d" % (k + 1)]) for k in range(naxis)]
        bzero = float(hdr.get("BZERO", 0)); bscale = float(hdr.get("BSCALE", 1))
        dt = {8: ">u1", 16: ">i2", 32: ">i4", 64: ">i8", -32: ">f4", -64: ">f8"}[bitpix]
        n = 1
        for d in dims: n *= d
        raw = np.frombuffer(f.read(n * abs(bitpix) // 8), dtype=dt)
    shape = list(reversed(dims))                # FITS is Fortran-ordered
    data = raw.reshape(shape).astype(np.float64) * bscale + bzero
    return data, bitpix, bzero


def load_image(path):
    """Return planar float32 array (C, H, W), integers normalised to [0,1] as
    NebulaScope's loader does (promoteToFloat with normalizeIntegers)."""
    ext = os.path.splitext(path)[1].lower()
    if ext in (".fits", ".fit", ".fts", ".fz"):
        try:
            data, bitpix, bzero = _read_fits_primary(path)
        except (ValueError, KeyError):
            from astropy.io import fits           # optional, for exotic files
            with fits.open(path) as hdul:
                hdu = next(h for h in hdul if h.data is not None)
                data = np.asarray(hdu.data).astype(np.float64)
                bitpix = hdu.header.get("BITPIX", -32)
                bzero = hdu.header.get("BZERO", 0)
        if data.ndim == 2:
            data = data[None, ...]
        elif data.ndim == 3 and data.shape[0] not in (1, 3):
            raise SystemExit("unsupported FITS layout %s" % (data.shape,))
        if bitpix > 0:  # integer data: normalise like the loader
            if bitpix == 8:    scale = 255.0
            elif bitpix == 16: scale = 65535.0 if bzero == 32768 else 32767.0
            else:              scale = 4294967295.0 if bzero == 2147483648 else 2147483647.0
            data = data / scale
        return np.ascontiguousarray(data.astype(np.float32))
    # Everything else through Pillow (8/16-bit gray or RGB).
    from PIL import Image
    im = Image.open(path)
    arr = np.asarray(im)
    if arr.dtype == np.uint16:
        arr = arr.astype(np.float32) / 65535.0
    elif arr.dtype == np.uint8:
        arr = arr.astype(np.float32) / 255.0
    else:
        arr = arr.astype(np.float32)
    if arr.ndim == 2:
        return arr[None, ...]
    if arr.shape[2] >= 3:
        return np.ascontiguousarray(arr[..., :3].transpose(2, 0, 1))
    return arr[..., 0][None, ...]


def load_display(sidecar_path, inline_json=None):
    if inline_json is not None:
        obj = json.loads(inline_json)
    else:
        with open(sidecar_path) as f:
            obj = json.load(f)
    disp = obj.get("display", obj)          # accept the sidecar or a bare block
    if int(disp.get("schema", 1)) > 2:
        raise SystemExit("display schema %s is newer than this renderer" % disp["schema"])
    if "channels" not in disp or len(disp["channels"]) != 3:
        raise SystemExit("not a NebulaScope display block")
    return disp

# ---- transfer function (src/core/Stretch.cpp) ---------------------------------

def mtf(x, m):
    """PixInsight midtones transfer function, vectorised, with the exact
    edge conventions of Stretch.cpp:mtf()."""
    x = np.asarray(x, dtype=np.float64)
    out = ((m - 1.0) * x) / (((2.0 * m - 1.0) * x) - m)
    out = np.where(x <= 0, 0.0, out)
    out = np.where(x >= 1, 1.0, out)
    out = np.where(np.abs(x - m) < 1e-12, 0.5, out)
    if m <= 0: out = np.ones_like(out)
    if m >= 1: out = np.zeros_like(out)
    return out


def base_shape(t, fn):
    t = np.clip(np.asarray(t, dtype=np.float64), 0.0, 1.0)
    if fn == "log":
        return np.log1p(t * 500.0) / math.log1p(500.0)
    if fn == "asinh":
        return np.arcsinh(t * 50.0) / math.asinh(50.0)
    return t                                    # linear


def ghs_slope(x, D, b, SP):
    dist = abs(x - SP)
    if b > 1e-4:
        return D * (1.0 + b * D * dist) ** (-(1.0 + 1.0 / b))     # hyperbolic
    if b < -1e-4:
        bb = -b
        return D / (1.0 + bb * D * dist)                          # logarithmic
    return D * math.exp(-D * dist)                                # exponential


def build_ghs_lut(g, N):
    """Cumulative trapezoid integral of the clamped slope, normalised — the
    exact loop of Stretch.cpp:buildGhsLut, including expm1(D)."""
    Deff = math.expm1(g["D"])
    b, SP, LP, HP = g["b"], g["SP"], g["LP"], g["HP"]

    def clamped(x):
        xx = LP if x < LP else (HP if x > HP else x)
        return ghs_slope(xx, Deff, b, SP)

    cum = np.zeros(N, dtype=np.float64)
    acc = 0.0
    prev = clamped(0.0)
    step = 1.0 / (N - 1)
    for i in range(1, N):
        s = clamped(i * step)
        acc += 0.5 * (prev + s) * step
        prev = s
        cum[i] = acc
    if acc <= 1e-12:                            # D ~ 0 -> identity
        return (np.arange(N) / (N - 1)).astype(np.float32)
    return (cum / acc).astype(np.float32)


def build_lut(fn, chan, ghs, N):
    if fn == "ghs":
        return build_ghs_lut(ghs, N)
    denom = max(1e-6, chan["white"] - chan["black"])
    m = min(0.999, max(0.001, (chan["mid"] - chan["black"]) / denom))
    t = np.arange(N, dtype=np.float64) / (N - 1)
    return mtf(base_shape(t, fn), m).astype(np.float32)

# ---- adjustments (src/core/Adjustments.h) --------------------------------------

def tone_identity(a):
    return (a["blackpoint"] == 0.0 and a["whitepoint"] == 1.0 and a["shadows"] == 0.0
            and a["highlights"] == 0.0 and a["brightness"] == 0.0
            and a["contrast"] == 0.0 and a["gamma"] == 1.0)


MIX_IDENTITY = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]


def color_identity(a):
    return (a["temperature"] == 0.0 and a["tint"] == 0.0 and a["hue"] == 0.0
            and a["saturation"] == 0.0 and a["vibrance"] == 0.0
            and list(a.get("mix", MIX_IDENTITY)) == MIX_IDENTITY)


def apply_tone(y, a):
    """Vectorised applyTone: BP/WP window -> shadows/highlights -> brightness
    -> contrast -> gamma. Computed in float64, returned float32 (as C++)."""
    v = np.asarray(y, dtype=np.float64)
    if a["blackpoint"] != 0.0 or a["whitepoint"] != 1.0:
        d = max(1e-6, a["whitepoint"] - a["blackpoint"])
        v = np.clip((v - a["blackpoint"]) / d, 0.0, 1.0)
    if a["shadows"] != 0.0:
        v = v + a["shadows"] * v * (1.0 - v) * (1.0 - v) * 2.0
    if a["highlights"] != 0.0:
        v = v + a["highlights"] * v * v * (1.0 - v) * 2.0
    if a["brightness"] != 0.0:
        v = v + a["brightness"] * 0.5
    if a["contrast"] != 0.0:
        v = 0.5 + (v - 0.5) * math.tan((a["contrast"] + 1.0) * (math.pi / 4.0))
    v = np.clip(v, 0.0, 1.0)
    if a["gamma"] != 1.0:
        v = np.power(v, 1.0 / a["gamma"])
    return v.astype(np.float32)


def apply_color(r, g, b, a):
    """Vectorised applyColor: mixer -> white balance -> hue rotation -> sat/vibrance."""
    R = r.astype(np.float64); G = g.astype(np.float64); B = b.astype(np.float64)
    mix = list(a.get("mix", MIX_IDENTITY))
    if mix != MIX_IDENTITY:
        M = mix                                     # row-major, out = M . rgb
        nR = M[0]*R + M[1]*G + M[2]*B
        nG = M[3]*R + M[4]*G + M[5]*B
        nB = M[6]*R + M[7]*G + M[8]*B
        R, G, B = nR, nG, nB
    if a["temperature"] != 0.0 or a["tint"] != 0.0:
        R = R * (1.0 + 0.30 * a["temperature"] + 0.15 * a["tint"])
        G = G * (1.0 - 0.30 * a["tint"])
        B = B * (1.0 - 0.30 * a["temperature"] + 0.15 * a["tint"])
    if a["hue"] != 0.0:
        th = a["hue"] * 0.01745329251994329577
        c, s = math.cos(th), math.sin(th)
        nR = R*(0.299+0.701*c+0.168*s) + G*(0.587-0.587*c+0.330*s) + B*(0.114-0.114*c-0.497*s)
        nG = R*(0.299-0.299*c-0.328*s) + G*(0.587+0.413*c+0.035*s) + B*(0.114-0.114*c+0.292*s)
        nB = R*(0.299-0.300*c+1.250*s) + G*(0.587-0.588*c-1.050*s) + B*(0.114+0.886*c-0.203*s)
        R, G, B = nR, nG, nB
    if a["saturation"] != 0.0 or a["vibrance"] != 0.0:
        Y = 0.2126 * R + 0.7152 * G + 0.0722 * B
        f = 1.0 + a["saturation"]
        if a["vibrance"] != 0.0:
            mx = np.maximum(R, np.maximum(G, B))
            mn = np.minimum(R, np.minimum(G, B))
            sat = np.where(mx > 1e-9, (mx - mn) / np.where(mx > 1e-9, mx, 1.0), 0.0)
            f = f * (1.0 + a["vibrance"] * (1.0 - sat))
        R = Y + (R - Y) * f
        G = Y + (G - Y) * f
        B = Y + (B - Y) * f
    return (np.clip(R, 0, 1).astype(np.float32), np.clip(G, 0, 1).astype(np.float32),
            np.clip(B, 0, 1).astype(np.float32))

# ---- the pipeline (src/render/DisplayRenderer.cpp:renderFloat) -----------------

ADJ_DEFAULTS = dict(blackpoint=0.0, whitepoint=1.0, shadows=0.0, highlights=0.0,
                    brightness=0.0, contrast=0.0, gamma=1.0, temperature=0.0,
                    tint=0.0, hue=0.0, saturation=0.0, vibrance=0.0)


def render_float(img, disp, N=4096):
    """img: float32 (C,H,W). Returns float32 (Cout,H,W) in [0,1] — the exact
    counterpart of DisplayRenderer::renderFloat (no dither, no 8-bit)."""
    C = img.shape[0]
    fn = disp["fn"]
    chans = disp["channels"]
    ghs = disp.get("ghs", dict(D=1.6, b=6.0, SP=0.18, LP=0.0, HP=1.0))
    adj = dict(ADJ_DEFAULTS); adj.update(disp.get("adjust", {}))

    # 1. Transfer LUTs indexed by windowed t in [0,1] (GHS: one master LUT).
    if fn == "ghs":
        l0 = build_lut("ghs", chans[0], ghs, N)
        luts = [l0, l0, l0]
    else:
        luts = [build_lut(fn, chans[c], ghs, N) for c in range(3)]
    # 2. Tone adjustments compose into the LUTs.
    if not tone_identity(adj):
        luts = [apply_tone(l, adj) for l in luts]
    # 3. Windowing coefficients: t = v*A + B, clamped.
    A = [0.0] * 3; Bc = [0.0] * 3
    for c in range(3):
        cs = chans[c]
        rng = max(1e-9, cs["hi"] - cs["lo"])
        denomW = max(1e-6, cs["white"] - cs["black"])
        A[c] = 1.0 / (rng * denomW)
        Bc[c] = -(cs["lo"] / rng + cs["black"]) / denomW

    def map_plane(ci, v):
        v = v.astype(np.float32)
        finite = np.isfinite(v)
        t = v.astype(np.float64) * A[ci] + Bc[ci]
        t = np.clip(np.where(finite, t, 0.0), 0.0, 1.0)
        f = t * (N - 1)
        i0 = f.astype(np.int64)                    # C++ int() truncation (f >= 0)
        i1 = np.where(i0 < N - 1, i0 + 1, i0)
        fr = (f - i0).astype(np.float32)           # float(f - i0)
        lut = luts[ci]
        out = lut[i0] * (np.float32(1.0) - fr) + lut[i1] * fr
        return np.where(finite, out, np.float32(0.0)).astype(np.float32)

    if C >= 3:
        planes = [map_plane(c, img[c]) for c in range(3)]
        if not color_identity(adj):
            planes = list(apply_color(*planes, adj))
        return np.stack(planes, axis=0)
    # mono: single plane through channel-0 window (colormap out of scope: gray)
    return map_plane(0, img[0])[None, ...]


def to_png(planes, path):
    from PIL import Image
    q = np.clip(planes * 255.0 + 0.5, 0, 255).astype(np.uint8)   # plain rounding
    if q.shape[0] == 1:
        Image.fromarray(q[0], mode="L").save(path)
    else:
        Image.fromarray(q.transpose(1, 2, 0), mode="RGB").save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image")
    ap.add_argument("sidecar", nargs="?", help="default: <image>_annotation.json")
    ap.add_argument("-o", "--out", help="8-bit PNG output")
    ap.add_argument("--float", dest="float_out", help="Float32 planes as .npy (C,H,W)")
    ap.add_argument("--sidecar-json", help="display block as an inline JSON string")
    a = ap.parse_args()
    sc = a.sidecar or (os.path.splitext(a.image)[0] + "_annotation.json")
    disp = load_display(sc, a.sidecar_json)
    img = load_image(a.image)
    out = render_float(img, disp)
    if a.float_out:
        np.save(a.float_out, out)
    if a.out:
        to_png(out, a.out)
    if not a.out and not a.float_out:
        print("rendered %dx%d, %d plane(s); mean %.6f (use -o / --float to write)"
              % (out.shape[2], out.shape[1], out.shape[0], float(out.mean())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

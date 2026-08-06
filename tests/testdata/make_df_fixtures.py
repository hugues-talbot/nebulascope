#!/usr/bin/env python3
# Regenerate the DisplayFunction test fixtures (df_*.xisf): minimal monolithic
# XISF files with SPCC-like data ranges and known saved STFs, covering the
# import matrix: unlinked far-white (the field case), linked far-white, and
# in-range white (no rebase). Also prints the smoke.nsc assertion lines with
# the EXPECTED post-import stretch values, computed with the same closed-form
# Mobius rebase the app uses (docs/TRANSPORT.md appendix).
import struct, os

HERE = os.path.dirname(os.path.abspath(__file__))
W = H = 16
N = W * H

def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]

def mtf(x, m):
    return ((m - 1.0) * x) / ((2.0 * m - 1.0) * x - m)

def mtf_inv(y, m):
    return m * y / (y * (2.0 * m - 1.0) - m + 1.0)

def channel_plane(lo, hi):
    # Linear ramp: min and max land exactly on lo/hi (as float32).
    return [f32(lo + (hi - lo) * i / (N - 1)) for i in range(N)]

def write_xisf(path, planes, df_m, df_s, df_h):
    nch = len(planes)
    geom = f"{W}:{H}:{nch}"
    cspace = "RGB" if nch == 3 else "Gray"
    data_off = 4096
    data_len = 4 * N * nch
    quad = lambda v: ":".join(f"{x:.17g}" for x in (list(v) + [0.5, 0.0, 1.0][len(v) - 2:4 - 1])) \
        if False else None
    fmt4 = lambda v: ":".join(f"{x:.17g}" for x in (list(v) + [0.5] * (4 - len(v))))
    xml = (f'<?xml version="1.0" encoding="UTF-8"?>'
           f'<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">'
           f'<Image geometry="{geom}" sampleFormat="Float32" bounds="0:1" '
           f'colorSpace="{cspace}" location="attachment:{data_off}:{data_len}">'
           f'<DisplayFunction m="{fmt4(df_m)}" s="{fmt4(df_s)}" '
           f'h="{fmt4(df_h)}" l="0:0:0:0" r="1:1:1:1"/>'
           f'</Image></xisf>').encode()
    pad = data_off - 16 - len(xml)
    assert pad >= 0, "XML too large for the 4096-byte header block"
    with open(path, 'wb') as f:
        f.write(b'XISF0100')
        f.write(struct.pack('<I', data_off - 16))   # header block = up to data
        f.write(b'\x00' * 4)
        f.write(xml + b' ' * pad)
        for p in planes:
            f.write(struct.pack(f'<{N}f', *p))

def expected_import(lo, hi, s, m, h, all_far, S):
    # Mirror MainWindow's import: normalize, then (if all channels far) the
    # common-scale Mobius rebase. Returns RAW black/mid/white.
    b = max(0.0, (s - lo) / (hi - lo))
    w = max(b + 1e-6, (h - lo) / (hi - lo))
    mid = b + m * (w - b)
    if all_far:
        denom = w - b
        mPi = min(0.999, max(1e-6, (mid - b) / denom))
        k = (1.0 - b) / denom
        if 0.0 < k < 1.0:
            xS = mtf_inv(min(S, 0.9999), mPi)
            xH = mtf_inv(0.5 * min(S, 0.9999), mPi)
            w = b + xS * denom
            mid = b + xH * denom
    return (lo + b * (hi - lo), lo + mid * (hi - lo), lo + w * (hi - lo))

def endpoint(lo, hi, s, m, h):
    b = max(0.0, (s - lo) / (hi - lo))
    w = max(b + 1e-6, (h - lo) / (hi - lo))
    mid = b + m * (w - b)
    denom = w - b
    mPi = min(0.999, max(1e-6, (mid - b) / denom))
    k = (1.0 - b) / denom
    return 1.0 if k >= 1.0 else mtf(k, mPi)

CASES = {
    # name: (lo[], hi[], s[], m[], h[])   — SPCC-like tiny linear ranges
    'df_unlinked': ([0.0003, 0.0004, 0.00037], [0.0129, 0.0033, 0.0025],
                    [0.00013, 0.0004, 0.00033], [0.00132, 0.00046, 0.00066],
                    [1.0, 1.0, 1.0]),
    'df_linked':   ([0.0003, 0.0004, 0.00037], [0.0129, 0.0033, 0.0025],
                    [0.0002, 0.0002, 0.0002],  [0.0015, 0.0015, 0.0015],
                    [1.0, 1.0, 1.0]),
    'df_inrange':  ([0.0003, 0.0004, 0.00037], [0.0129, 0.0033, 0.0025],
                    [0.0005, 0.0006, 0.0005],  [0.25, 0.25, 0.25],
                    [0.0116, 0.0030, 0.0022]),
}

for name, (lo, hi, s, m, h) in CASES.items():
    lo = [f32(x) for x in lo]
    hi = [f32(x) for x in hi]
    planes = [channel_plane(lo[c], hi[c]) for c in range(3)]
    write_xisf(os.path.join(HERE, name + '.xisf'), planes, m, s, h)
    far = [((max(b, 0.0) + 1e-6) if False else (h[c] - lo[c]) / (hi[c] - lo[c])) > 1.001
           for c, b in enumerate(s)]
    all_far = all(far)
    S = max(endpoint(lo[c], hi[c], s[c], m[c], h[c]) for c in range(3)) if all_far else 0.0
    print(f"# {name}.xisf  (all_far={all_far}, S={S:.6f})")
    for c in range(3):
        b, mid, w = expected_import(lo[c], hi[c], s[c], m[c], h[c], all_far, S)
        print(f"assert stretch {c} {b:.9g} {mid:.9g} {w:.9g} 2e-7")

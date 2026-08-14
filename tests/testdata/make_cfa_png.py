#!/usr/bin/env python3
"""Generate cfa_gray.png: a raw Bayer mosaic stored as a plain 8-bit
grayscale PNG — the shape planetary/solar capture tools (FireCapture,
SharpCap) produce when saving an OSC sensor's frames undebayered. No CFA
metadata exists in the container, so NebulaScope must (a) sniff the mosaic
statistically and (b) demosaic on a forced pattern.

Scene: constant RGB (200, 120, 60) mosaiced as RGGB. After `debayer rggb`
every interior pixel must read (200, 120, 60)/255 — the values asserted in
smoke.nsc. Pure stdlib; regenerate with:  python3 make_cfa_png.py
"""
import struct, zlib, os

W = H = 64
R, G, B = 200, 120, 60

rows = []
for y in range(H):
    row = bytearray([0])                       # PNG filter type 0 (None)
    for x in range(W):
        if y % 2 == 0:
            row.append(R if x % 2 == 0 else G)  # row 0 of RGGB: R G
        else:
            row.append(G if x % 2 == 0 else B)  # row 1 of RGGB: G B
    rows.append(bytes(row))
raw = b"".join(rows)

def chunk(tag, data):
    c = tag + data
    return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 0, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(raw))
       + chunk(b"IEND", b""))

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cfa_gray.png")
with open(out, "wb") as f:
    f.write(png)
print(f"{out}: {len(png)} bytes, {W}x{H} 8-bit grayscale, RGGB mosaic of "
      f"RGB({R},{G},{B})")
print(f"expected after debayer rggb: {R/255:.5f} {G/255:.5f} {B/255:.5f}")

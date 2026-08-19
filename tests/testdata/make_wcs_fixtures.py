#!/usr/bin/env python3
"""Generate wcs_wide.fits / wcs_narrow.fits: two plate-solved (TAN) frames of
the SAME sky centre at 2"/px unrotated and 0.5"/px rotated 30 deg - the
C11-vs-telephoto situation - for the Match-from-WCS smoke block. Pure stdlib.
Regenerate with:  python3 make_wcs_fixtures.py
"""
import os
import struct, math, random
def card(k, v, c=''):
    return (f"{k:<8}= {v:>20} / {c}".ljust(80)).encode()
def scard(k, v, c=''):
    q = "'" + v.ljust(8) + "'"
    return (f"{k:<8}= {q:<20} / {c}".ljust(80)).encode()
def fits(path, w, h, cards_extra):
    cards=[card('SIMPLE','T'),card('BITPIX','-32'),card('NAXIS','2'),
           card('NAXIS1',str(w)),card('NAXIS2',str(h))] + cards_extra + [b'END'.ljust(80)]
    hdr=b''.join(cards); hdr += b' '*((2880-len(hdr)%2880)%2880)
    random.seed(3)
    vals=[0.01*random.random() for _ in range(w*h)]
    data=struct.pack(f'>{w*h}f', *vals); data += b'\x00'*((2880-len(data)%2880)%2880)
    open(path,'wb').write(hdr+data)
def tan(ra, dec, crpix1, crpix2, scale_arcsec, rot_deg):
    s = scale_arcsec/3600.0; th = math.radians(rot_deg)
    c, sn = math.cos(th), math.sin(th)
    f = lambda v: f"{v:.12E}"
    return [scard('CTYPE1','RA---TAN'), scard('CTYPE2','DEC--TAN'),
            card('CRVAL1',f(ra)), card('CRVAL2',f(dec)),
            card('CRPIX1',f(crpix1)), card('CRPIX2',f(crpix2)),
            card('CD1_1',f(-s*c)), card('CD1_2',f(s*sn)),
            card('CD2_1',f(s*sn)), card('CD2_2',f(s*c))]
d = os.path.dirname(os.path.abspath(__file__))
fits(f'{d}/wcs_wide.fits',   128, 128, tan(150.0, 20.0, 64.5, 64.5, 2.0, 0.0))
fits(f'{d}/wcs_narrow.fits', 128, 128, tan(150.0, 20.0, 64.5, 64.5, 0.5, 30.0))
print(open(f'{d}/wcs_wide.fits','rb').read(80*15)[80*5:80*15].decode())

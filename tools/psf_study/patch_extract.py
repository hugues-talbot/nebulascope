"""Extract a common patch from a folder of registered subs (XISF/FITS).

Two modes, run from the data folder (or with PSF_DATA set):

  patch_extract.py --locate <old_master.fits> <new_master.xisf|fits>
      Registers the NEW integration master onto the OLD raw master
      (same-source stars, sub-pixel) and maps the study ROI centre into
      the new grid; writes patch_rect.json next to this script.

  patch_extract.py <subs_dir> <out_cube.fits> [--rect x y w h]
      Crops the patch from every *.xisf / *.fits in subs_dir (sorted) and
      stacks the crops into a float32 cube [N, h, w] with a JSON manifest
      (filenames, per-sub median/MAD) beside it. Default patch: from
      patch_rect.json (or --rect in new-grid pixel coordinates).

The XISF reader below is deliberately minimal: monolithic XISF 1.0,
Float32/UInt16 images, uncompressed or zlib / zlib+byteshuffle — which
covers PixInsight-written registered frames and masters.
"""
import sys, os, json, glob, re, zlib
import numpy as np
import xml.etree.ElementTree as ET

# ---------------------------------------------------------------- XISF ----

def read_xisf(path):
    """Return (cube[C,H,W] float32, None). Minimal monolithic-XISF reader."""
    with open(path, 'rb') as f:
        sig = f.read(8)
        if sig != b'XISF0100':
            raise ValueError(f'{path}: not a monolithic XISF')
        hlen = int.from_bytes(f.read(4), 'little')
        f.read(4)                                  # reserved
        xml = f.read(hlen).decode('utf-8', 'replace')
        # strip default namespace for painless findall
        xml = re.sub(r'xmlns="[^"]+"', '', xml, count=1)
        root = ET.fromstring(xml)
        img = root.find('.//Image')
        if img is None:
            raise ValueError(f'{path}: no Image element')
        w, h, c = (int(v) for v in img.get('geometry').split(':'))
        fmt = img.get('sampleFormat', 'Float32')
        loc = img.get('location', '')
        if not loc.startswith('attachment:'):
            raise ValueError(f'{path}: inline data not supported')
        pos, size = (int(v) for v in loc.split(':')[1:3])
        comp = img.get('compression')              # e.g. zlib:1234 / zlib+sh:1234:4
        f.seek(pos)
        raw = f.read(size)
    if comp:
        parts = comp.split(':')
        codec = parts[0].replace('+sh', '')
        if codec == 'zlib':
            raw = zlib.decompress(raw)
        elif codec == 'zstd':
            import subprocess
            raw = subprocess.run(['zstd', '-d', '--stdout'], input=raw,
                                 capture_output=True, check=True).stdout
        elif codec.startswith('lz4'):
            import subprocess
            raw = subprocess.run(['lz4', '-d', '-c'], input=raw,
                                 capture_output=True, check=True).stdout
        else:
            raise ValueError(f'unsupported XISF compression: {comp}')
        if parts[0].endswith('+sh'):
            item = int(parts[2])
            n = len(raw) // item
            raw = np.frombuffer(raw, np.uint8).reshape(item, n).T.reshape(-1).tobytes()
    dt = {'Float32': np.float32, 'UInt16': np.uint16, 'Float64': np.float64,
          'UInt8': np.uint8, 'UInt32': np.uint32}[fmt]
    a = np.frombuffer(raw, dt).reshape(c, h, w).astype(np.float32)
    if dt == np.uint16: a /= 65535.0
    elif dt == np.uint8: a /= 255.0
    elif dt == np.uint32: a /= 4294967295.0
    return a, None


def read_any(path):
    if path.lower().endswith(('.xisf',)):
        return read_xisf(path)[0]
    sys.path.insert(0, os.environ.get('PSF_DATA', '.'))
    from star_fwhm import read_fits_f32
    cube, _ = read_fits_f32(path)
    return np.asarray(cube, dtype=np.float32)

# ------------------------------------------------------------- locate ----

def locate(old_master, new_master):
    sys.path.insert(0, os.environ.get('PSF_DATA', '.'))
    from psf_pipeline import bruteforce_similarity, refine_affine, detect_stars
    from linear_deconv import regprep, ROI_CX, ROI_CY
    HERE = os.path.dirname(os.path.abspath(__file__))
    old = read_any(old_master)
    new = read_any(new_master)
    B = 8   # register at bin-8: fits the search canvas, and patch placement
            # only needs the centre good to a few full-res pixels.
    def bin8(a):
        h, w = (a.shape[0]//B)*B, (a.shape[1]//B)*B
        return a[:h, :w].reshape(h//B, B, w//B, B).mean(axis=(1, 3))
    o = regprep(bin8(np.nan_to_num(old[min(1, old.shape[0]-1)].astype(np.float64))))
    n = regprep(bin8(np.nan_to_num(new[min(1, new.shape[0]-1)].astype(np.float64))))
    # Same sky, same sampling: scale ~1, small rotation; widen if it fails.
    A0, score, s0, r0 = bruteforce_similarity(o, n,
                            np.geomspace(0.97, 1.03, 5), range(-6, 7, 2), n=2048)
    So, Sn = detect_stars(o, maxn=600), detect_stars(n, maxn=600)
    A, npairs, med = refine_affine(A0, So, Sn, tol0=4.0)
    print(f'new<-old registration (bin{B}): score {score:.3f}, {npairs} pairs, {med:.2f} bin-px')
    cx = (A[0,0]*(ROI_CX/B) + A[1,0]*(ROI_CY/B) + A[2,0]) * B
    cy = (A[0,1]*(ROI_CX/B) + A[1,1]*(ROI_CY/B) + A[2,1]) * B
    side = 1024
    rect = [int(round(cx)) - side//2, int(round(cy)) - side//2, side, side]
    with open(os.path.join(HERE, 'patch_rect.json'), 'w') as f:
        json.dump({'rect': rect, 'note': 'x y w h in NEW-master grid'}, f)
    print('patch rect (new grid):', rect)

# ------------------------------------------------------------ extract ----

def extract(subs_dir, out_path, rect):
    x, y, w, h = rect
    files = sorted(glob.glob(os.path.join(subs_dir, '*.xisf')) +
                   glob.glob(os.path.join(subs_dir, '*.fits')) +
                   glob.glob(os.path.join(subs_dir, '*.fit')))
    if not files:
        raise SystemExit(f'no subs in {subs_dir}')
    cubes, manifest = [], []
    for i, fp in enumerate(files):
        try:
            a = read_any(fp)
        except Exception as ex:
            # A mid-write disconnect leaves truncated files; skip loudly.
            print(f'  skip (unreadable: {ex}): {os.path.basename(fp)}')
            continue
        plane = a[0] if a.ndim == 3 else a
        crop = plane[y:y+h, x:x+w]
        if crop.shape != (h, w):
            print(f'  skip (patch outside frame): {os.path.basename(fp)}')
            continue
        fin = crop[np.isfinite(crop)]
        med = float(np.median(fin)) if fin.size else float('nan')
        mad = float(np.median(np.abs(fin - med))) if fin.size else float('nan')
        cubes.append(np.nan_to_num(crop, nan=med))
        manifest.append({'file': os.path.basename(fp), 'median': med, 'mad': mad})
        if (i + 1) % 25 == 0:
            print(f'  {i+1}/{len(files)}')
    cube = np.stack(cubes).astype(np.float32)
    sys.path.insert(0, os.environ.get('PSF_DATA', '.'))
    from full_deconv import write_fits_f32
    write_fits_f32(out_path, cube, [f'patch {x},{y} {w}x{h}', f'{len(cubes)} subs'])
    with open(out_path + '.manifest.json', 'w') as f:
        json.dump({'rect': rect, 'subs': manifest}, f, indent=1)
    print(f'{out_path}: {cube.shape}, manifest with {len(manifest)} subs')


def main():
    args = sys.argv[1:]
    if args and args[0] == '--locate':
        locate(args[1], args[2]); return
    if len(args) < 2:
        raise SystemExit(__doc__)
    if '--rect' in args:
        i = args.index('--rect')
        rect = [int(v) for v in args[i+1:i+5]]
    else:
        HERE = os.path.dirname(os.path.abspath(__file__))
        rect = json.load(open(os.path.join(HERE, 'patch_rect.json')))['rect']
    extract(args[0], args[1], rect)

if __name__ == '__main__':
    main()

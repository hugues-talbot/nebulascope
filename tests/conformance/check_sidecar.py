#!/usr/bin/env python3
"""
Sidecar conformance: NebulaScope's renderer vs the reference implementation.

For each display case, NebulaScope (headless, --run) sets the display state
on a fixture, writes the sidecar, and `bake`s its Float32 rendering; then
tools/render_sidecar.py renders the SAME fixture from the SAME sidecar with
no NebulaScope code. The two Float32 outputs must agree to within a few
float32 ULPs at every pixel — which is what "reproducible in other software"
means, made testable.

    python3 tests/conformance/check_sidecar.py <path/to/NebulaScope binary>

Exit code = number of failing cases (0 = conformant). Run by CTest.
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import render_sidecar as ref  # noqa: E402

FIXTURES = {
    "mono": os.path.join(ROOT, "tests", "testdata", "cfa_asiair_rggb.fits"),   # opened debayer OFF -> mono
    "rgb":  os.path.join(ROOT, "tests", "testdata", "pi_f32_plain.xisf"),
}

# Each case: fixture kind, script lines that set the display state.
CASES = [
    ("linear_window",   "mono", ["fn linear"]),
    ("asinh_autostf",   "mono", ["autostf", "fn asinh"]),
    ("log_tone",        "mono", ["autostf", "fn log", "adjust gamma 1.35",
                                 "adjust contrast 0.2", "adjust shadows 0.15"]),
    ("ghs_master",      "mono", ["autostf", "fn ghs"]),
    ("rgb_asinh",       "rgb",  ["autostf", "fn asinh"]),
    ("rgb_ghs_colour",  "rgb",  ["autostf", "fn ghs", "adjust hue -35",
                                 "adjust saturation 0.25", "adjust temperature -0.1",
                                 "adjust tint 0.05", "adjust vibrance 0.3"]),
    ("rgb_extended_win","rgb",  ["window all -0.3 0.35 1.4", "fn asinh"]),   # handles beyond the data
    ("ghs_sp_outside",  "mono", ["autostf", "fn ghs", "axis peak"]),           # SP snapped to the mode
    ("rgb_colour_mix",  "rgb",  ["autostf", "fn asinh",                        # schema 2: 3x3 mixer
                                 "adjust mix 0.62 0.31 0.07 -0.12 0.94 0.18 0.05 -0.4 1.35",
                                 "adjust saturation 0.15"]),
    ("rgb_transportish","rgb",  ["autostf", "fn linear", "adjust hue -0.99995914",
                                 "adjust saturation 0.11033605",
                                 "adjust temperature -0.0738426",
                                 "adjust tint -0.02782801", "adjust gamma 1.3",
                                 "adjust highlights -0.2"]),
]

# Tolerance: NumPy and the C++ both compute in float64 then round to float32
# at the same points; residual differences are a few float32 ULPs from
# transcendental libm vs NumPy implementations. 1e-5 absolute is ~85 ULPs at
# 1.0 and far below one 8-bit level (3.9e-3).
TOL = 1e-5


def run_case(binary, name, kind, lines, workdir):
    fixture = FIXTURES[kind]
    scpath = os.path.join(workdir, name + "_annotation.json")
    baked = os.path.join(workdir, name + "_baked.fits")
    srcfits = os.path.join(workdir, name + "_src.fits")
    script = os.path.join(workdir, name + ".nsc")
    with open(script, "w") as f:
        f.write("open %s\nwaitloaded\n" % fixture)
        if kind == "mono":
            f.write("debayer off\nwaitloaded\n")
        for ln in lines:
            f.write(ln + "\n")
        f.write("sleep 300\n")
        f.write("saveann %s\n" % scpath)
        f.write("bake %s\n" % baked)
        # The exact Float32 data NebulaScope decoded (XISF fixtures etc.), so
        # the reference renders the very same pixels.
        f.write("save %s\n" % srcfits)
        f.write("quit\n")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    r = subprocess.run([binary, "--run", script], env=env, capture_output=True,
                       text=True, timeout=180)
    if r.returncode != 0 or not os.path.exists(baked) or not os.path.exists(scpath):
        return False, "NebulaScope run failed (rc=%d):\n%s" % (r.returncode, r.stdout[-1500:])
    # Reference render from the same fixture + sidecar.
    img = ref.load_image(srcfits)
    disp = ref.load_display(scpath)
    mine = ref.render_float(img, disp)
    theirs, _, _ = ref._read_fits_primary(baked)
    theirs = theirs.astype(np.float32)
    if theirs.ndim == 2:
        theirs = theirs[None, ...]
    if theirs.shape != mine.shape:
        return False, "shape mismatch: NebulaScope %s vs reference %s" % (theirs.shape, mine.shape)
    diff = np.abs(theirs.astype(np.float64) - mine.astype(np.float64))
    mx = float(diff.max())
    n_bad = int((diff > TOL).sum())
    ok = n_bad == 0
    return ok, "max |diff| = %.3e, pixels over tol: %d / %d, fn=%s" % (
        mx, n_bad, diff.size, disp["fn"])


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    binary = sys.argv[1]
    fails = 0
    with tempfile.TemporaryDirectory() as wd:
        for name, kind, lines in CASES:
            ok, msg = run_case(binary, name, kind, lines, wd)
            print("%-18s %s  %s" % (name, "ok  " if ok else "FAIL", msg))
            if not ok:
                fails += 1
    print("conformance: %d failure(s) in %d case(s)" % (fails, len(CASES)))
    return fails


if __name__ == "__main__":
    sys.exit(main())

"""Parametric extended-structure kernel: a circular Moffat fitted by least
squares so that Hubble convolved with it matches the render.

    fwhm_px, beta, rms = moffat_kernel_fit(hst_w, render, rect)

Why: the study's Wiener kernel (wiener_kernel_lin) is regularized with a
lambda anchored to the spectrum's maximum, and its FWHM depends on that
lambda — on the ninth-row means, 2.92"/2.02"/1.96" (H/S/O) at 1e-3 and
2.20"/1.66"/1.59" at 1e-5, the latter already fitting noise. A two-
parameter Moffat can neither be inflated by regularization nor overfit,
so its FWHM is the number to quote for extended-structure resolution.
Both sides are band-passed (scales > 25 px removed) exactly as the
Wiener estimator does, and the match is affine in intensity.
"""
import numpy as np
from scipy import ndimage
from scipy.optimize import least_squares
from scipy.signal import fftconvolve


def moffat_psf(fwhm_px, beta, n=None):
    alpha = fwhm_px / (2.0 * np.sqrt(2.0 ** (1.0 / beta) - 1.0))
    n = n or int(2 * np.ceil(4 * fwhm_px) + 1)
    yy, xx = np.mgrid[0:n, 0:n] - n // 2
    k = (1.0 + (xx ** 2 + yy ** 2) / alpha ** 2) ** (-beta)
    return k / k.sum()


def _bandpass(a):
    return a - ndimage.gaussian_filter(a, 25.0 / 2.355)


def moffat_kernel_fit(hst_w, render, rect, fwhm0=6.0, beta_bounds=(1.5, 8.0), margin=48):
    y0, y1, x0, x1 = rect
    Y0, Y1 = max(0, y0 - margin), min(hst_w.shape[0], y1 + margin)
    X0, X1 = max(0, x0 - margin), min(hst_w.shape[1], x1 + margin)
    h = _bandpass(hst_w[Y0:Y1, X0:X1])
    r = _bandpass(render[Y0:Y1, X0:X1])
    iy, ix = slice(y0 - Y0, y1 - Y0), slice(x0 - X0, x1 - X0)
    rv = r[iy, ix][::2, ::2].ravel()
    ones = np.ones_like(rv)

    def model(p):
        k = moffat_psf(p[0], p[1])
        c = fftconvolve(h, k, mode='same')[iy, ix][::2, ::2].ravel()
        A = np.stack([c, ones], 1)
        sol, *_ = np.linalg.lstsq(A, rv, rcond=None)
        return A @ sol - rv

    fit = least_squares(model, [fwhm0, 3.0], bounds=([1.5, beta_bounds[0]], [30.0, beta_bounds[1]]),
                        max_nfev=60, diff_step=[0.02, 0.05])
    rms = float(np.sqrt(np.mean(fit.fun ** 2)) / max(1e-12, rv.std()))
    return float(fit.x[0]), float(fit.x[1]), rms

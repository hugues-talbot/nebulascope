#pragma once
//
// Stretch — transfer functions that turn linear pixel values into a display
// image. Three classic shapes (Linear with MTF midtone, Log, Asinh) plus the
// Generalised Hyperbolic Stretch (GHS), which focuses contrast at a chosen
// symmetry point with shadow/highlight protection.
//
// All transfers operate on a normalized input x in [0,1] (the pixel value after
// mapping the channel's display range [lo,hi] onto [0,1]) and return y in [0,1].
//
#include <vector>

namespace astro {

enum class StretchFn { Linear, Log, Asinh, GHS };

// Black / midtone / white points, in normalized [0,1] display coordinates.
struct ChannelStretch {
    double black = 0.0;
    double mid   = 0.5;   // absolute position on the same [0,1] axis
    double white = 1.0;
};

// GHS controls (shared across channels — a "master" curve).
struct GHSParams {
    double D  = 1.6;   // stretch strength
    double b  = 6.0;   // local intensity / focus  (b<0 log, ~0 exp, =1 harmonic, >1 hyperbolic)
    double SP = 0.18;  // symmetry point (max contrast)
    double LP = 0.0;   // shadow-protection bound (linear below)
    double HP = 1.0;   // highlight-protection bound (linear above)
};

// PixInsight midtones-transfer function.
double mtf(double x, double m);

// Fit a Linear+MTF channel stretch (black/mid/white over the window lo..hi)
// so that stretching the raw samples best matches `target` display values
// ([0,1]) in least squares. Strided sampling: uses raw[i*stride] pairs.
// Returns the RMSE of the fit; `out` receives the fitted parameters.
// This is how a colour-transport result becomes a NON-DESTRUCTIVE stretch:
// smooth parametric curves cannot posterize and touch no pixel data.
// With intensityWeight, residuals are weighted by (0.05 + target): the
// abundant near-black background no longer dominates the fit — signal
// colours govern it (use starless references to keep stars out entirely).
double fitChannelStretch(const float* raw, const float* target, std::size_t n,
                         std::size_t stride, double lo, double hi,
                         ChannelStretch& out, bool intensityWeight = false);

// Stage 2 of the transport stretch fit: with the per-channel curves fixed,
// fit the CROSS-CHANNEL colour adjustments (temperature, tint, hue,
// saturation) so applyColor(display) matches the target triples — the part
// of an optimal-transport map that separable curves cannot express.
// d*/t* are display/target planes; samples strided; intensity-weighted.
// Returns the overall RMSE after the colour fit; `adj` receives the four
// colour fields (tone fields left identity).
struct AdjustParams;
double fitColorAdjust(const float* dR, const float* dG, const float* dB,
                      const float* tR, const float* tG, const float* tB,
                      std::size_t n, std::size_t stride, AdjustParams& adj);
// Weighted least-squares 3x3 colour mixer M minimizing sum w |M.d - t|^2
// (w = 0.05 + target luma, the same weighting as the other fits). CLOSED
// FORM — one shared 3x3 Gram inversion, no line searches — and strictly more
// expressive than temperature/tint/hue/saturation, all of which are linear.
// Returns the weighted RMSE; M is row-major.
double fitColorMatrix(const float* dR, const float* dG, const float* dB,
                      const float* tR, const float* tG, const float* tB,
                      std::size_t n, std::size_t stride, double M[9]);
// Base curve shape for Linear/Log/Asinh on a black/white-normalized t in [0,1].
double baseShape(double t, StretchFn fn);
// GHS local stretch intensity (the slope of the transfer); max at SP.
double ghsSlope(double x, double D, double b, double SP);

// Sample the transfer SHAPE into an N-entry lookup table indexed by the
// *windowed* coordinate t in [0,1] (0 = black point, 1 = white point). The
// black/white window itself is applied by the caller via windowCoord() so that
// the full LUT resolution spans the window (no posterization when the window is
// a small fraction of the data range).
// Rebase a display function whose white point lies beyond the data maximum
// (normalized window coordinate > 1, PixInsight's [0,1]-container convention)
// onto the data range: returns a stretch with white = 1 whose curve is the
// original restricted to the data and rescaled by 1/f(1) — EXACTLY in the MTF
// family (a Mobius map through (0,0) and (1,1)), no approximation. The
// uniform output scale f(1) is returned via outScale when non-null.
ChannelStretch rebaseFarWhite(const ChannelStretch& cs, double* outScale = nullptr);
// Multi-channel form: display value of the ORIGINAL stretch at the data
// maximum, and rebase against a COMMON output level S (use max over the
// channels' endpoints so the inter-channel balance is preserved exactly).
double farWhiteEndpoint(const ChannelStretch& cs);
ChannelStretch rebaseFarWhiteTo(const ChannelStretch& cs, double S);

std::vector<float> buildLut(StretchFn fn, const ChannelStretch& cs,
                            const GHSParams& ghs, int N);

// Map a raw sample to the windowed coordinate t in [0,1]:
//   x = (v - lo) / (hi - lo);   t = (x - black) / (white - black)   (clamped).
// Values below the black point clamp to 0, above the white point to 1, and the
// span in between uses the whole [0,1] range at full floating-point precision.
inline double windowCoord(double v, double lo, double hi, const ChannelStretch& cs) {
    const double denomR = (hi - lo) > 1e-9 ? (hi - lo) : 1e-9;
    const double x = (v - lo) / denomR;
    const double denomW = (cs.white - cs.black) > 1e-6 ? (cs.white - cs.black) : 1e-6;
    const double t = (x - cs.black) / denomW;
    return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
}

// Single-sample transfer (handy for drawing curves).
double transferAt(double x, StretchFn fn, const ChannelStretch& cs, const GHSParams& ghs);

} // namespace astro

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
// Base curve shape for Linear/Log/Asinh on a black/white-normalized t in [0,1].
double baseShape(double t, StretchFn fn);
// GHS local stretch intensity (the slope of the transfer); max at SP.
double ghsSlope(double x, double D, double b, double SP);

// Sample the transfer SHAPE into an N-entry lookup table indexed by the
// *windowed* coordinate t in [0,1] (0 = black point, 1 = white point). The
// black/white window itself is applied by the caller via windowCoord() so that
// the full LUT resolution spans the window (no posterization when the window is
// a small fraction of the data range).
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

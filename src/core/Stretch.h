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

// Sample the full transfer into an N-entry lookup table over x in [0,1].
std::vector<float> buildLut(StretchFn fn, const ChannelStretch& cs,
                            const GHSParams& ghs, int N);

// Single-sample transfer (handy for drawing curves).
double transferAt(double x, StretchFn fn, const ChannelStretch& cs, const GHSParams& ghs);

} // namespace astro

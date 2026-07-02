#include "core/Stretch.h"
#include <cmath>
#include <algorithm>

namespace astro {

static inline double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

double mtf(double x, double m) {
    if (x <= 0) return 0.0;
    if (x >= 1) return 1.0;
    if (m <= 0) return 1.0;
    if (m >= 1) return 0.0;
    if (std::fabs(x - m) < 1e-12) return 0.5;
    return ((m - 1.0) * x) / (((2.0 * m - 1.0) * x) - m);
}

double baseShape(double t, StretchFn fn) {
    t = clamp01(t);
    switch (fn) {
        case StretchFn::Log:   return std::log1p(t * 500.0) / std::log1p(500.0);
        case StretchFn::Asinh: return std::asinh(t * 50.0) / std::asinh(50.0);
        case StretchFn::Linear:
        default:               return t;
    }
}

double ghsSlope(double x, double D, double b, double SP) {
    const double dist = std::fabs(x - SP);
    if (b > 1e-4)  return D * std::pow(1.0 + b * D * dist, -(1.0 + 1.0 / b));   // hyperbolic
    if (b < -1e-4) { const double bb = -b; return D / (1.0 + bb * D * dist); }  // logarithmic
    return D * std::exp(-D * dist);                                             // exponential
}

static std::vector<float> buildGhsLut(const GHSParams& g, int N) {
    std::vector<float> lut(N);
    auto clampedSlope = [&](double x) {
        const double xx = x < g.LP ? g.LP : (x > g.HP ? g.HP : x);   // linear protection zones
        return ghsSlope(xx, g.D, g.b, g.SP);
    };
    // Cumulative integral of the slope -> monotonic transfer; normalize to [0,1].
    std::vector<double> cum(N);
    cum[0] = 0.0;
    double acc = 0.0, prev = clampedSlope(0.0);
    for (int i = 1; i < N; ++i) {
        const double x = double(i) / (N - 1);
        const double s = clampedSlope(x);
        acc += 0.5 * (prev + s) * (1.0 / (N - 1));
        prev = s;
        cum[i] = acc;
    }
    const bool degenerate = acc <= 1e-12;          // D ~ 0 -> identity
    for (int i = 0; i < N; ++i)
        lut[i] = degenerate ? float(double(i) / (N - 1)) : float(cum[i] / acc);
    return lut;
}

std::vector<float> buildLut(StretchFn fn, const ChannelStretch& cs,
                            const GHSParams& ghs, int N) {
    // The LUT is indexed by the windowed coordinate t in [0,1] (the caller maps
    // raw pixels to t via windowCoord). So here index i corresponds directly to
    // t = i/(N-1) — no black/white remap, which is what previously crushed the
    // usable range down to a few levels for narrow windows.
    if (fn == StretchFn::GHS) {
        // GHS shape is already defined on the windowed [0,1] (SP/LP/HP live there).
        return buildGhsLut(ghs, N);
    }

    const double denom = std::max(1e-6, cs.white - cs.black);
    const double m = std::min(0.999, std::max(0.001, (cs.mid - cs.black) / denom));
    std::vector<float> lut(N);
    for (int i = 0; i < N; ++i) {
        const double t = double(i) / (N - 1);
        lut[i] = float(mtf(baseShape(t, fn), m));
    }
    return lut;
}

double transferAt(double x, StretchFn fn, const ChannelStretch& cs, const GHSParams& ghs) {
    const double denom = std::max(1e-6, cs.white - cs.black);
    if (fn == StretchFn::GHS) {
        static const int N = 1024;
        const auto l = buildGhsLut(ghs, N);
        const double t = clamp01((x - cs.black) / denom);
        return l[int(t * (N - 1))];
    }
    const double m = std::min(0.999, std::max(0.001, (cs.mid - cs.black) / denom));
    return mtf(baseShape(clamp01((x - cs.black) / denom), fn), m);
}

} // namespace astro

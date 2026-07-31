#include "core/Stretch.h"
#include "core/Adjustments.h"
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
    // The curve is normalized to [0,1] below, which cancels any LINEAR scaling
    // of the slope — so a linear D acts only through the b·D product and feels
    // interchangeable with b. Give D an exponential response (like official
    // GHS, where D reaches the thousands): D becomes the "amount" of stretch
    // concentrated at SP, b the falloff "focus"/profile — visibly different.
    const double Deff = std::expm1(g.D);           // ui 0..8 -> 0..~2980
    auto clampedSlope = [&](double x) {
        const double xx = x < g.LP ? g.LP : (x > g.HP ? g.HP : x);   // linear protection zones
        return ghsSlope(xx, Deff, g.b, g.SP);
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

double fitChannelStretch(const float* raw, const float* target, std::size_t n,
                         std::size_t stride, double lo, double hi,
                         ChannelStretch& out, bool intensityWeight) {
    // Collect finite sample pairs (strided).
    std::vector<std::pair<float, float>> s;
    s.reserve(n / std::max<std::size_t>(1, stride) + 1);
    for (std::size_t i = 0; i < n; i += std::max<std::size_t>(1, stride))
        if (std::isfinite(raw[i]) && std::isfinite(target[i]))
            s.push_back({ raw[i], target[i] });
    if (s.size() < 64) { out = ChannelStretch{}; return 1.0; }

    auto mse = [&](double black, double mid, double white) {
        ChannelStretch cs; cs.black = black; cs.mid = mid; cs.white = white;
        const double denom = std::max(1e-6, white - black);
        const double m = std::min(0.999, std::max(0.001, (mid - black) / denom));
        double e = 0.0, wsum = 0.0;
        for (const auto& p : s) {
            const double d = mtf(windowCoord(p.first, lo, hi, cs), m) - p.second;
            // Intensity weighting: the sky is legion; without it the fit
            // matches the background and shrugs at the nebula.
            const double w = intensityWeight ? 0.05 + p.second : 1.0;
            e += w * d * d;
            wsum += w;
        }
        return e / std::max(1e-12, wsum);
    };
    // Golden-section line search on one coordinate.
    auto lineSearch = [&](double a, double b, auto eval) {
        const double phi = 0.6180339887498949;
        double x1 = b - phi * (b - a), x2 = a + phi * (b - a);
        double f1 = eval(x1), f2 = eval(x2);
        for (int it = 0; it < 40 && (b - a) > 1e-5; ++it) {
            if (f1 < f2) { b = x2; x2 = x1; f2 = f1; x1 = b - phi * (b - a); f1 = eval(x1); }
            else         { a = x1; x1 = x2; f1 = f2; x2 = a + phi * (b - a); f2 = eval(x2); }
        }
        return 0.5 * (a + b);
    };

    double black = 0.0, white = 1.0, mid = 0.5;
    for (int sweep = 0; sweep < 4; ++sweep) {           // coordinate descent
        black = lineSearch(0.0, white - 0.02,
                           [&](double v) { return mse(v, mid, white); });
        white = lineSearch(black + 0.02, 1.0,
                           [&](double v) { return mse(black, mid, v); });
        mid   = lineSearch(black + 1e-4, white - 1e-4,
                           [&](double v) { return mse(black, v, white); });
    }
    out.black = black; out.mid = mid; out.white = white;
    return std::sqrt(mse(black, mid, white));
}

double fitColorAdjust(const float* dR, const float* dG, const float* dB,
                      const float* tR, const float* tG, const float* tB,
                      std::size_t n, std::size_t stride, AdjustParams& adj) {
    struct Triple { float dr, dg, db, tr, tg, tb, w; };
    std::vector<Triple> s;
    s.reserve(n / std::max<std::size_t>(1, stride) + 1);
    for (std::size_t i = 0; i < n; i += std::max<std::size_t>(1, stride)) {
        const float vals[6] = { dR[i], dG[i], dB[i], tR[i], tG[i], tB[i] };
        bool ok = true;
        for (float v : vals) ok = ok && std::isfinite(v);
        if (!ok) continue;
        const float luma = 0.2126f * vals[3] + 0.7152f * vals[4] + 0.0722f * vals[5];
        s.push_back({ vals[0], vals[1], vals[2], vals[3], vals[4], vals[5],
                      0.05f + luma });
    }
    adj = AdjustParams{};
    if (s.size() < 64) return 1.0;

    auto mse = [&](const AdjustParams& a) {
        double e = 0.0, wsum = 0.0;
        for (const Triple& p : s) {
            float r = p.dr, g = p.dg, b = p.db;
            applyColor(r, g, b, a);
            const double d = double(r - p.tr) * (r - p.tr)
                           + double(g - p.tg) * (g - p.tg)
                           + double(b - p.tb) * (b - p.tb);
            e += p.w * d;
            wsum += p.w;
        }
        return e / (3.0 * std::max(1e-12, wsum));
    };
    auto lineSearch = [&](double a, double b, auto eval) {
        const double phi = 0.6180339887498949;
        double x1 = b - phi * (b - a), x2 = a + phi * (b - a);
        double f1 = eval(x1), f2 = eval(x2);
        for (int it = 0; it < 32 && (b - a) > 1e-4; ++it) {
            if (f1 < f2) { b = x2; x2 = x1; f2 = f1; x1 = b - phi * (b - a); f1 = eval(x1); }
            else         { a = x1; x1 = x2; f1 = f2; x2 = a + phi * (b - a); f2 = eval(x2); }
        }
        return 0.5 * (a + b);
    };

    double* fields[4] = { &adj.temperature, &adj.tint, &adj.hue, &adj.saturation };
    for (int sweep = 0; sweep < 3; ++sweep)
        for (double* f : fields)
            *f = lineSearch(-1.0, 1.0, [&](double v) {
                const double keep = *f; *f = v;
                const double e = mse(adj);
                *f = keep;
                return e;
            });
    return std::sqrt(mse(adj));
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

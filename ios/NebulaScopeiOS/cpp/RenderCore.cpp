#include "RenderCore.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace nb {

// ---- synthetic galaxy -------------------------------------------------------

FloatImage makeSampleGalaxy(int w, int h) {
    FloatImage img;
    img.w = w; img.h = h; img.ch = 3;
    img.data.assign(std::size_t(w) * h * 3, 0.0f);

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    std::normal_distribution<float> Nz(0.0f, 1.0f);

    const double cx = w * 0.50, cy = h * 0.46;
    const double ang = -0.42, ca = std::cos(ang), sa = std::sin(ang);
    const double R = std::min(w, h) * 0.72;
    const double sky = 0.018;                       // faint background level

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // rotate into galaxy frame; squash vertically for an edge-on disk
            const double dx = x - cx, dy = y - cy;
            const double u =  dx * ca + dy * sa;
            const double v = (-dx * sa + dy * ca) / 0.28;
            const double r = std::sqrt(u * u + v * v);

            double lum = sky + 0.010 * (U(rng));                  // sky + shot noise
            lum += 0.85 * std::exp(-r / (R * 0.16));              // disk/halo
            lum += 0.90 * std::exp(-(u*u + v*v) / (2*(R*0.05)*(R*0.05))); // bright core
            // dust lane: a thin dark band across the midplane
            const double lane = std::exp(-(v * 0.28) * (v * 0.28) / (2 * 9.0));
            lum *= (1.0 - 0.55 * lane * std::exp(-std::fabs(u) / (R * 0.9)));

            double rr = lum * 1.06, gg = lum, bb = lum * 0.94 + 0.05 * std::exp(-r / (R * 0.5));
            img.plane(0)[std::size_t(y) * w + x] = float(rr);
            img.plane(1)[std::size_t(y) * w + x] = float(gg);
            img.plane(2)[std::size_t(y) * w + x] = float(bb);
        }
    }

    // Power-law star field: many faint, few brilliant.
    const int stars = (w * h) / 900;
    for (int i = 0; i < stars; ++i) {
        const int x = int(U(rng) * w), y = int(U(rng) * h);
        const double b = std::pow(U(rng), 3.2);                  // brightness
        const double peak = 0.15 + b * 1.1;
        const double rad = 0.6 + b * 2.2;
        const int ext = int(std::ceil(rad * 3));
        const double tR = b > 0.7 ? 1.06 : 0.98, tB = b > 0.7 ? 0.9 : 1.05;
        for (int oy = -ext; oy <= ext; ++oy)
            for (int ox = -ext; ox <= ext; ++ox) {
                const int px = x + ox, py = y + oy;
                if (px < 0 || py < 0 || px >= w || py >= h) continue;
                const double g = peak * std::exp(-(ox*ox + oy*oy) / (2 * rad * rad));
                const std::size_t idx = std::size_t(py) * w + px;
                img.plane(0)[idx] += float(g * tR);
                img.plane(1)[idx] += float(g);
                img.plane(2)[idx] += float(g * tB);
            }
    }
    return img;
}

// ---- statistics -------------------------------------------------------------

Stats computeStats(const FloatImage& img, int channel, std::size_t maxSamples) {
    Stats s;
    const std::size_t n = img.perChannel();
    if (!n) return s;
    const float* p = img.plane(channel);

    float mn = 0, mx = 0; bool any = false;
    for (std::size_t i = 0; i < n; ++i) {
        const float val = p[i];
        if (!std::isfinite(val)) continue;
        if (!any) { mn = mx = val; any = true; }
        else { if (val < mn) mn = val; if (val > mx) mx = val; }
    }
    if (!any) return s;
    s.mn = mn; s.mx = mx;

    const std::size_t step = n > maxSamples ? n / maxSamples : 1;
    std::vector<float> samp;
    samp.reserve(n / step + 1);
    for (std::size_t i = 0; i < n; i += step)
        if (std::isfinite(p[i])) samp.push_back(p[i]);
    if (samp.empty()) return s;

    const std::size_t mid = samp.size() / 2;
    std::nth_element(samp.begin(), samp.begin() + mid, samp.end());
    const float med = samp[mid];
    s.median = med;
    for (auto& v : samp) v = std::fabs(v - med);
    std::nth_element(samp.begin(), samp.begin() + mid, samp.end());
    s.mad = samp[mid];
    return s;
}

void computeSTF(const Stats& s, astro::ChannelStretch& cs, double& lo, double& hi) {
    lo = s.mn; hi = s.mx;
    const double span = std::max(1e-6, double(s.mx) - double(s.mn));
    const double nMed = (double(s.median) - s.mn) / span;
    double nMad = double(s.mad) / span;
    if (nMad < 1e-6) nMad = 0.01;

    cs.black = std::min(0.5, std::max(0.0, nMed - 2.8 * nMad));
    cs.white = 1.0;

    // Solve the MTF midtone so the background (nMed) displays at ~0.25.
    double xx = (nMed - cs.black) / std::max(1e-6, cs.white - cs.black);
    xx = std::min(0.95, std::max(0.02, xx));
    const double yy = 0.25;
    double m = xx * (yy - 1.0) / ((2.0 * yy * xx) - yy - xx);
    if (!(m > 0.0 && m < 1.0)) m = 0.5;
    cs.mid = cs.black + m * (cs.white - cs.black);
    cs.mid = std::min(cs.white - 1e-3, std::max(cs.black + 1e-3, cs.mid));
}

// ---- render -----------------------------------------------------------------

std::vector<std::uint8_t> renderRGBA(
    const FloatImage& img, astro::StretchFn fn,
    const astro::ChannelStretch cs[3], const double lo[3], const double hi[3],
    const astro::GHSParams& ghs, astro::Colormap cmap, double splitT) {

    const int w = img.w, h = img.h, ch = img.ch;
    const int N = 4096;
    std::vector<std::uint8_t> out(std::size_t(w) * h * 4, 255);

    std::vector<float> lut[3];
    if (fn == astro::StretchFn::GHS) {
        lut[0] = astro::buildLut(fn, cs[0], ghs, N); lut[1] = lut[0]; lut[2] = lut[0];
    } else {
        for (int c = 0; c < 3; ++c) lut[c] = astro::buildLut(fn, cs[c], ghs, N);
    }

    auto mapVal = [&](int c, float v) -> float {
        if (!std::isfinite(v)) return 0.0f;
        const double t = astro::windowCoord(v, lo[c], hi[c], cs[c]);
        int idx = int(t * (N - 1) + 0.5);
        idx = idx < 0 ? 0 : (idx > N - 1 ? N - 1 : idx);
        return lut[c][idx];
    };

    if (ch == 1 && cmap != astro::Colormap::Gray) {
        const int M = 4096;
        const std::vector<std::uint8_t> cm = astro::buildColormapLut(cmap, M, splitT);
        const float* p0 = img.plane(0);
        for (std::size_t i = 0, n = img.perChannel(); i < n; ++i) {
            const float y = mapVal(0, p0[i]);
            int ci = int(y * (M - 1) + 0.5f);
            ci = ci < 0 ? 0 : (ci > M - 1 ? M - 1 : ci);
            out[i*4+0] = cm[ci*3+0]; out[i*4+1] = cm[ci*3+1]; out[i*4+2] = cm[ci*3+2];
        }
        return out;
    }

    const float* p0 = img.plane(0);
    const float* p1 = ch >= 3 ? img.plane(1) : p0;
    const float* p2 = ch >= 3 ? img.plane(2) : p0;
    for (std::size_t i = 0, n = img.perChannel(); i < n; ++i) {
        out[i*4+0] = std::uint8_t(mapVal(0, p0[i]) * 255.0f + 0.5f);
        out[i*4+1] = std::uint8_t(mapVal(1, p1[i]) * 255.0f + 0.5f);
        out[i*4+2] = std::uint8_t(mapVal(2, p2[i]) * 255.0f + 0.5f);
    }
    return out;
}

// ---- histogram --------------------------------------------------------------

std::vector<float> histogram(const FloatImage& img, int channel, int bins,
                             bool logScale, double lo, double hi) {
    std::vector<float> hb(bins, 0.0f);
    const std::size_t n = img.perChannel();
    if (!n) return hb;
    const float* p = img.plane(channel);
    const double span = std::max(1e-9, hi - lo);
    const std::size_t step = n > 400000 ? n / 400000 : 1;
    for (std::size_t i = 0; i < n; i += step) {
        const float v = p[i];
        if (!std::isfinite(v)) continue;
        double x = (double(v) - lo) / span;
        if (x < 0 || x > 1) continue;
        int bi = int(x * (bins - 1));
        hb[bi] += 1.0f;
    }
    float mx = 0.0f;
    for (float& v : hb) { if (logScale) v = std::log1p(v); if (v > mx) mx = v; }
    if (mx > 0) for (float& v : hb) v /= mx;
    return hb;
}

} // namespace nb

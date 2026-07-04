#include "core/ChannelCombine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace astro {

namespace {

struct PlaneStats { float mn = 0, mx = 1, median = 1; };

PlaneStats planeStats(const float* p, std::size_t n) {
    PlaneStats s;
    if (!n) return s;
    float mn = 0, mx = 0; bool any = false;
    for (std::size_t i = 0; i < n; ++i) {
        const float v = p[i];
        if (!std::isfinite(v)) continue;
        if (!any) { mn = mx = v; any = true; }
        else { if (v < mn) mn = v; if (v > mx) mx = v; }
    }
    if (!any) return s;
    s.mn = mn; s.mx = mx;

    const std::size_t maxSamples = 150000;
    const std::size_t step = n > maxSamples ? n / maxSamples : 1;
    std::vector<float> samp;
    samp.reserve(n / step + 1);
    for (std::size_t i = 0; i < n; i += step)
        if (std::isfinite(p[i])) samp.push_back(p[i]);
    if (!samp.empty()) {
        const std::size_t mid = samp.size() / 2;
        std::nth_element(samp.begin(), samp.begin() + mid, samp.end());
        s.median = samp[mid];
    }
    return s;
}

// Map a raw sample to its pre-normalized value under mode `pn`.
inline float normalize(float v, PreNorm pn, const PlaneStats& s) {
    if (!std::isfinite(v)) return 0.0f;
    switch (pn) {
        case PreNorm::Median:   return s.median > 1e-12f ? v / s.median : v;
        case PreNorm::MinMax: { const float d = s.mx - s.mn; return d > 1e-12f ? (v - s.mn) / d : 0.0f; }
        case PreNorm::Pedestal: return v - s.median;
        case PreNorm::None:
        default:                return v;
    }
}

} // namespace

CombineResult combineChannels(int w, int h,
                              const std::vector<CombinePlane>& planes,
                              PreNorm pn,
                              const float* lum, LumMode lm, double lumAmount) {
    CombineResult r;
    if (w <= 0 || h <= 0) { r.error = "Invalid image dimensions"; return r; }
    if (planes.empty() && !(lum && lm != LumMode::None)) {
        r.error = "No channels assigned to R, G or B"; return r;
    }
    const std::size_t n = std::size_t(w) * h;
    for (const auto& p : planes)
        if (!p.data) { r.error = "A channel is missing its pixel data"; return r; }

    // Pre-compute per-plane stats for normalization.
    std::vector<PlaneStats> stats(planes.size());
    for (std::size_t k = 0; k < planes.size(); ++k)
        stats[k] = planeStats(planes[k].data, n);
    PlaneStats lumStats;
    if (lum) lumStats = planeStats(lum, n);

    ImageData out(w, h, 3, SampleFormat::Float32, ColorSpace::RGB);
    out.bytes().resize(out.byteSize());
    float* pr = out.plane<float>(0);
    float* pg = out.plane<float>(1);
    float* pb = out.plane<float>(2);

    for (std::size_t i = 0; i < n; ++i) {
        double R = 0, G = 0, B = 0;
        for (std::size_t k = 0; k < planes.size(); ++k) {
            const float v = normalize(planes[k].data[i], pn, stats[k]);
            R += planes[k].wR * v;
            G += planes[k].wG * v;
            B += planes[k].wB * v;
        }

        if (lum && lm != LumMode::None) {
            const float L = normalize(lum[i], pn, lumStats);
            if (lm == LumMode::Linear) {
                R += lumAmount * L; G += lumAmount * L; B += lumAmount * L;
            } else { // Luminance: scale RGB to L's luma, preserving colour ratios
                const double luma = 0.2126 * R + 0.7152 * G + 0.0722 * B;
                const double scale = luma > 1e-9 ? (lumAmount * L) / luma : 0.0;
                R *= scale; G *= scale; B *= scale;
            }
        }

        pr[i] = float(R); pg[i] = float(G); pb[i] = float(B);
    }

    r.ok = true;
    r.image = std::move(out);
    return r;
}

} // namespace astro

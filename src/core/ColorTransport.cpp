#include "core/ColorTransport.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace astro {

namespace {

// Quantile map from two sorted samples, tabulated at Q knots: value at source
// quantile q maps to the reference value at the same quantile.
struct QuantileMap {
    std::vector<float> srcQ, refQ;      // both size Q, monotonic
    void build(const std::vector<float>& src, const std::vector<float>& ref, int Q) {
        srcQ.resize(Q); refQ.resize(Q);
        for (int k = 0; k < Q; ++k) {
            const double t = double(k) / (Q - 1);
            srcQ[k] = src[std::size_t(t * (src.size() - 1))];
            refQ[k] = ref[std::size_t(t * (ref.size() - 1))];
        }
    }
    float map(float v) const {
        // binary search in srcQ, linear interpolation into refQ
        if (v <= srcQ.front()) return refQ.front();
        if (v >= srcQ.back())  return refQ.back();
        const auto it = std::upper_bound(srcQ.begin(), srcQ.end(), v);
        const std::size_t hi = std::size_t(it - srcQ.begin());
        const std::size_t lo = hi - 1;
        const float d = srcQ[hi] - srcQ[lo];
        const float f = d > 1e-20f ? (v - srcQ[lo]) / d : 0.0f;
        return refQ[lo] + f * (refQ[hi] - refQ[lo]);
    }
};

// Random 3x3 rotation (orthonormal, det +1) via Gram-Schmidt on Gaussian draws.
void randomRotation(std::mt19937& rng, double R[3][3]) {
    std::normal_distribution<double> N(0.0, 1.0);
    double a[3], b[3];
    for (int i = 0; i < 3; ++i) { a[i] = N(rng); b[i] = N(rng); }
    double la = std::sqrt(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
    if (la < 1e-12) { a[0] = 1; la = 1; }
    for (double& v : a) v /= la;
    const double dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
    for (int i = 0; i < 3; ++i) b[i] -= dot * a[i];
    double lb = std::sqrt(b[0]*b[0] + b[1]*b[1] + b[2]*b[2]);
    if (lb < 1e-12) { b[0] = -a[1]; b[1] = a[0]; b[2] = 0; lb = std::sqrt(b[0]*b[0]+b[1]*b[1]) + 1e-30; }
    for (double& v : b) v /= lb;
    // c = a × b
    const double c[3] = { a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0] };
    for (int i = 0; i < 3; ++i) { R[0][i] = a[i]; R[1][i] = b[i]; R[2][i] = c[i]; }
}

// Strided flat-index sample of the pixels inside `roi` (whole image if the
// roi is invalid), capped at maxSamples.
static std::vector<std::size_t> roiIndices(int W, int H, const TransportRoi& roi,
                                           std::size_t maxSamples) {
    int x0 = 0, y0 = 0, rw = W, rh = H;
    if (roi.valid()) {
        x0 = std::max(0, roi.x); y0 = std::max(0, roi.y);
        rw = std::min(W - x0, roi.w); rh = std::min(H - y0, roi.h);
        if (rw <= 0 || rh <= 0) { x0 = y0 = 0; rw = W; rh = H; }   // degenerate: fall back
    }
    const std::size_t total = std::size_t(rw) * rh;
    const std::size_t step = total > maxSamples ? total / maxSamples : 1;
    std::vector<std::size_t> idx;
    idx.reserve(total / step + 1);
    for (std::size_t k = 0; k < total; k += step) {
        const int yy = y0 + int(k / rw);
        const int xx = x0 + int(k % rw);
        idx.push_back(std::size_t(yy) * W + xx);
    }
    return idx;
}

} // namespace

ColorTransportResult transportColors(const ImageData& src, const ImageData& ref,
                                     int iterations, std::size_t maxSamples,
                                     const TransportRoi& srcRoi, const TransportRoi& refRoi,
                                     float saturationCut) {
    ColorTransportResult r;
    if (!src.isValid() || !ref.isValid() ||
        src.format() != SampleFormat::Float32 || ref.format() != SampleFormat::Float32) {
        r.error = "Both images must be loaded Float32 data.";
        return r;
    }
    const std::size_t n = src.samplesPerChannel();
    const std::size_t nr = ref.samplesPerChannel();
    std::vector<std::size_t> idxS = roiIndices(src.width(), src.height(), srcRoi, maxSamples);
    std::vector<std::size_t> idxR = roiIndices(ref.width(), ref.height(), refRoi, maxSamples);

    // Drop saturated pixels (stars) from BOTH distribution estimates: their
    // colours are clipped artefacts and, across modalities (RGB vs HO stars),
    // fundamentally unmatchable. The map still applies to every pixel.
    auto pruneSaturated = [&](std::vector<std::size_t>& idx, const ImageData& img) {
        if (saturationCut >= 1.0f) return;
        const int ch = img.channels();
        const float* p0 = img.plane<float>(0);
        const float* p1 = ch >= 3 ? img.plane<float>(1) : p0;
        const float* p2 = ch >= 3 ? img.plane<float>(2) : p0;
        std::vector<std::size_t> keep;
        keep.reserve(idx.size());
        for (std::size_t i : idx)
            if (p0[i] < saturationCut && p1[i] < saturationCut && p2[i] < saturationCut)
                keep.push_back(i);
        if (keep.size() >= 1024) idx.swap(keep);   // keep the cut only if enough remains
    };
    pruneSaturated(idxS, src);
    pruneSaturated(idxR, ref);

    // Mono pair: 1-D quantile matching.
    if (src.channels() == 1 && ref.channels() == 1) {
        const float* ps = src.plane<float>(0);
        const float* pr = ref.plane<float>(0);
        std::vector<float> ss, rs;
        ss.reserve(idxS.size()); rs.reserve(idxR.size());
        for (std::size_t i : idxS) if (std::isfinite(ps[i])) ss.push_back(ps[i]);
        for (std::size_t i : idxR) if (std::isfinite(pr[i])) rs.push_back(pr[i]);
        std::sort(ss.begin(), ss.end());
        std::sort(rs.begin(), rs.end());
        if (ss.size() < 16 || rs.size() < 16) { r.error = "Not enough finite pixels."; return r; }
        QuantileMap qm; qm.build(ss, rs, 1024);
        r.image = ImageData(src.width(), src.height(), 1, SampleFormat::Float32, ColorSpace::Gray);
        float* o = r.image.plane<float>(0);
        for (std::size_t i = 0; i < n; ++i)
            o[i] = std::isfinite(ps[i]) ? qm.map(ps[i]) : 0.0f;
        r.ok = true;
        return r;
    }
    if (src.channels() < 3 || ref.channels() < 3) {
        r.error = "Colour transport needs two RGB images (or two mono images).";
        return r;
    }

    // Working copy of the source colours.
    std::vector<float> W(3 * n);
    for (int c = 0; c < 3; ++c) {
        const float* p = src.plane<float>(c);
        float* w = W.data() + std::size_t(c) * n;
        for (std::size_t i = 0; i < n; ++i) {
            const float v = p[i];
            w[i] = std::isfinite(v) ? (v < 0 ? 0.0f : (v > 1 ? 1.0f : v)) : 0.0f;
        }
    }
    const float* R0 = ref.plane<float>(0);
    const float* R1 = ref.plane<float>(1);
    const float* R2 = ref.plane<float>(2);

    std::mt19937 rng(20260723u);                 // deterministic result
    std::vector<float> projS, projR, all(n);
    const int Q = 1024;

    for (int it = 0; it < iterations; ++it) {
        double R[3][3];
        if (it == 0) {                           // first sweep: plain channel axes
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) R[i][j] = (i == j) ? 1.0 : 0.0;
        } else {
            randomRotation(rng, R);
        }
        float d0[3] = {0,0,0};                   // per-axis corrections, rotated back below
        std::vector<float> delta[3];
        for (int k = 0; k < 3; ++k) {
            const double rx = R[k][0], ry = R[k][1], rz = R[k][2];
            // project every source pixel; sample-project the reference
            for (std::size_t i = 0; i < n; ++i)
                all[i] = float(rx * W[i] + ry * W[n + i] + rz * W[2 * n + i]);
            projS.clear();
            for (std::size_t i : idxS) projS.push_back(all[i]);
            projR.clear();
            for (std::size_t i : idxR) {
                if (!std::isfinite(R0[i]) || !std::isfinite(R1[i]) || !std::isfinite(R2[i])) continue;
                projR.push_back(float(rx * R0[i] + ry * R1[i] + rz * R2[i]));
            }
            if (projS.size() < 16 || projR.size() < 16) { r.error = "Not enough finite pixels."; return r; }
            std::sort(projS.begin(), projS.end());
            std::sort(projR.begin(), projR.end());
            QuantileMap qm; qm.build(projS, projR, Q);
            delta[k].resize(n);
            for (std::size_t i = 0; i < n; ++i) delta[k][i] = qm.map(all[i]) - all[i];
            (void)d0;
        }
        // v += R^T · delta  (R orthonormal)
        for (std::size_t i = 0; i < n; ++i) {
            const float dk0 = delta[0][i], dk1 = delta[1][i], dk2 = delta[2][i];
            W[i]         += float(R[0][0] * dk0 + R[1][0] * dk1 + R[2][0] * dk2);
            W[n + i]     += float(R[0][1] * dk0 + R[1][1] * dk1 + R[2][1] * dk2);
            W[2 * n + i] += float(R[0][2] * dk0 + R[1][2] * dk1 + R[2][2] * dk2);
        }
    }

    r.image = ImageData(src.width(), src.height(), 3, SampleFormat::Float32, ColorSpace::RGB);
    for (int c = 0; c < 3; ++c) {
        const float* w = W.data() + std::size_t(c) * n;
        float* o = r.image.plane<float>(c);
        for (std::size_t i = 0; i < n; ++i)
            o[i] = w[i] < 0 ? 0.0f : (w[i] > 1 ? 1.0f : w[i]);
    }
    r.ok = true;
    return r;
}

} // namespace astro

#include "core/Deconvolve.h"
#include "core/PsfMeasure.h"
#include "third_party/pocketfft_hdronly.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <thread>

namespace astro {

namespace {

constexpr double kFwhmToSigma = 2.3548200450309493; // 2*sqrt(2*ln 2)

double percentileOf(std::vector<float>& v, double pct) {
    if (v.empty()) return 0.0;
    const double t = pct / 100.0 * double(v.size() - 1);
    const std::size_t k = std::size_t(std::min(double(v.size() - 1), std::max(0.0, t)));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return double(v[k]);
}

std::vector<float> gaussianKernel(double fwhm, int side) {
    const double s = std::max(0.3, fwhm / kFwhmToSigma);
    const int half = side / 2;
    std::vector<float> k(std::size_t(side) * side);
    double sum = 0.0;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const double dx = x - half, dy = y - half;
            const double v = std::exp(-0.5 * (dx * dx + dy * dy) / (s * s));
            k[std::size_t(y) * side + x] = float(v);
            sum += v;
        }
    for (float& v : k) v = float(v / sum);
    return k;
}

void tick(std::atomic<int>* p) { if (p) p->fetch_add(1); }

// Chunked parallel-for over [0, n): fn(begin, end) per worker. The FFTs are
// already threaded (pocketfft); this covers the other hot paths — the
// starlet's separable passes and the big element-wise loops — which were
// single-threaded and dominated the RED iteration wall time.
template <typename F>
void parallelFor(int n, F&& fn) {
    const int nt = std::min<int>(int(std::max(1u, std::thread::hardware_concurrency())), n);
    if (nt <= 1) { fn(0, n); return; }
    std::vector<std::thread> ts;
    ts.reserve(std::size_t(nt));
    const int chunk = (n + nt - 1) / nt;
    for (int t = 0; t < nt; ++t) {
        const int b = t * chunk, e = std::min(n, b + chunk);
        if (b >= e) break;
        ts.emplace_back([&fn, b, e] { fn(b, e); });
    }
    for (auto& th : ts) th.join();
}

// Hot pixels above a percentile of the finite values, as stamp seeds.
std::vector<std::pair<int, int>> hotPixels(const float* in, int w, int h,
                                           double thr) {
    std::vector<std::pair<int, int>> hot;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float v = in[std::size_t(y) * w + x];
            if (std::isfinite(v) && v > thr) hot.emplace_back(x, y);
        }
    return hot;
}

// À-trous smoothing with the B3-spline mask [1 4 6 4 1]/16 dilated by
// `step`, separable, mirrored edges.
void atrousSmooth(const std::vector<float>& in, std::vector<float>& scratch,
                  std::vector<float>& out, int w, int h, int step) {
    static const float h5[5] = { 1.f/16, 4.f/16, 6.f/16, 4.f/16, 1.f/16 };
    auto mirror = [](int i, int n) {
        if (i < 0) i = -i;
        if (i >= n) i = 2 * n - 2 - i;
        return std::min(std::max(i, 0), n - 1);
    };
    scratch.resize(in.size());
    out.resize(in.size());
    parallelFor(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* row = in.data() + std::size_t(y) * w;
            float* srow = scratch.data() + std::size_t(y) * w;
            for (int x = 0; x < w; ++x) {
                double acc = 0.0;
                for (int t = -2; t <= 2; ++t)
                    acc += h5[t + 2] * row[mirror(x + t * step, w)];
                srow[x] = float(acc);
            }
        }
    });
    // Column pass chunked over ROWS of the output (cache-friendly sweep:
    // each worker walks full rows, reading five staggered scratch rows).
    parallelFor(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* orow = out.data() + std::size_t(y) * w;
            const float* r[5];
            for (int t = -2; t <= 2; ++t)
                r[t + 2] = scratch.data() + std::size_t(mirror(y + t * step, h)) * w;
            for (int x = 0; x < w; ++x)
                orow[x] = float(h5[0]*r[0][x] + h5[1]*r[1][x] + h5[2]*r[2][x]
                              + h5[3]*r[3][x] + h5[4]*r[4][x]);
        }
    });
}

// Per-level noise sigma: MAD of a strided sample of the detail plane.
double madSigma(const std::vector<float>& d) {
    const std::size_t stride = std::max<std::size_t>(1, d.size() / 500000);
    std::vector<float> s;
    s.reserve(d.size() / stride + 1);
    for (std::size_t i = 0; i < d.size(); i += stride) s.push_back(std::fabs(d[i]));
    if (s.empty()) return 0.0;
    const std::size_t m = s.size() / 2;
    std::nth_element(s.begin(), s.begin() + m, s.end());
    return double(s[m]) / 0.6745;
}

} // namespace

std::vector<float> coreProtectionMask(const std::vector<std::pair<int, int>>& hot,
                                      int w, int h, double r0, double r1) {
    const int R = int(r1) + 1;
    const int side = 2 * R + 1;
    // Precomputed radial stamp: 1 inside r0, cosine ramp to 0 at r1.
    std::vector<float> stamp(std::size_t(side) * side, 0.0f);
    for (int dy = -R; dy <= R; ++dy)
        for (int dx = -R; dx <= R; ++dx) {
            const double d = std::hypot(double(dx), double(dy));
            double m = 0.0;
            if (d <= r0) m = 1.0;
            else if (d < r1) m = 0.5 + 0.5 * std::cos(M_PI * (d - r0) / (r1 - r0));
            stamp[std::size_t(dy + R) * side + (dx + R)] = float(m);
        }
    std::vector<float> mask(std::size_t(w) * h, 0.0f);
    for (const auto& [hx, hy] : hot)
        for (int dy = -R; dy <= R; ++dy) {
            const int y = hy + dy;
            if (y < 0 || y >= h) continue;
            for (int dx = -R; dx <= R; ++dx) {
                const int x = hx + dx;
                if (x < 0 || x >= w) continue;
                float& m = mask[std::size_t(y) * w + x];
                m = std::max(m, stamp[std::size_t(dy + R) * side + (dx + R)]);
            }
        }
    return mask;
}

std::vector<float> starletDenoise(const float* plane, int w, int h,
                                  int levels, double k, int threshLevels) {
    const std::size_t n = std::size_t(w) * h;
    if (threshLevels < 0) threshLevels = levels;
    std::vector<float> cur(plane, plane + n), next, scratch, out(n, 0.0f);
    for (int j = 0; j < levels; ++j) {
        atrousSmooth(cur, scratch, next, w, h, 1 << j);
        parallelFor(int(n / 4096 + 1), [&](int b, int e) {
            const std::size_t lo = std::size_t(b) * 4096, hi = std::min(n, std::size_t(e) * 4096);
            for (std::size_t i = lo; i < hi; ++i) cur[i] -= next[i];   // detail_j
        });
        if (j < threshLevels) {
            const float thr = float(k * madSigma(cur));
            parallelFor(int(n / 4096 + 1), [&](int b, int e) {
                const std::size_t lo = std::size_t(b) * 4096, hi = std::min(n, std::size_t(e) * 4096);
                for (std::size_t i = lo; i < hi; ++i) {
                    const float d = cur[i];
                    out[i] += d > thr ? d - thr : (d < -thr ? d + thr : 0.0f);
                }
            });
        } else {
            // Coarse detail passes through whole: shrinking it prints the
            // square support edge of the B3 tensor kernel around stars.
            parallelFor(int(n / 4096 + 1), [&](int b, int e) {
                const std::size_t lo = std::size_t(b) * 4096, hi = std::min(n, std::size_t(e) * 4096);
                for (std::size_t i = lo; i < hi; ++i) out[i] += cur[i];
            });
        }
        cur.swap(next);
    }
    parallelFor(int(n / 4096 + 1), [&](int b, int e) {
        const std::size_t lo = std::size_t(b) * 4096, hi = std::min(n, std::size_t(e) * 4096);
        for (std::size_t i = lo; i < hi; ++i) out[i] += cur[i];        // coarse
    });
    return out;
}

std::vector<float> moffatKernel(const DeconvChannelPsf& psf, int& sideOut) {
    const double beta = std::max(1.2, psf.beta);
    const double conv = 2.0 * std::sqrt(std::pow(2.0, 1.0 / beta) - 1.0);
    const double sx = std::max(0.3, psf.fwhmMajPx / conv);
    const double sy = std::max(0.3, psf.fwhmMinPx / conv);
    // Canvas and apodization: a Moffat's power-law wings are still ~1e-5 of
    // the peak 16 px out, and a saturated star sits ~1e6 over the sky — a
    // hard truncation at the canvas edge printed a faint SQUARE ring around
    // every bright core (the filter's impulse response carries the cut).
    // Large canvas + radial cosine taper to a true zero on a CIRCLE: no
    // discontinuity, no geometry to print.
    int side = int(std::ceil(16.0 * std::max(psf.fwhmMajPx, 2.0)));
    side = std::max(65, side | 1);
    sideOut = side;
    const int half = side / 2;
    const double R = half, Ri = 0.75 * half;
    const double th = psf.paDeg * M_PI / 180.0;
    const double ct = std::cos(th), st = std::sin(th);
    std::vector<float> k(std::size_t(side) * side);
    double sum = 0.0;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const double dx = x - half, dy = y - half;
            const double u = std::pow((dx * ct + dy * st) / sx, 2.0)
                           + std::pow((-dx * st + dy * ct) / sy, 2.0);
            const double d = std::hypot(dx, dy);
            double w = 1.0;
            if (d >= R) w = 0.0;
            else if (d > Ri) w = 0.5 + 0.5 * std::cos(M_PI * (d - Ri) / (R - Ri));
            const double v = w * std::pow(1.0 + u, -beta);
            k[std::size_t(y) * side + x] = float(v);
            sum += v;
        }
    for (float& v : k) v = float(v / sum);
    return k;
}

std::vector<float> deconvolveChannel(const float* plane, int w, int h,
                                     const DeconvChannelPsf& psf,
                                     const DeconvOptions& opt,
                                     std::atomic<int>* stepsDone) {
    using namespace pocketfft;
    const std::size_t n = std::size_t(w) * h;

    // Finite working copy, background-subtracted (a pedestal through the
    // near-unit-DC filter would shift, and edges would ring against it).
    std::vector<float> in(plane, plane + n);
    std::vector<float> finiteVals;
    finiteVals.reserve(n);
    for (float v : in) if (std::isfinite(v)) finiteVals.push_back(v);
    if (finiteVals.empty()) return in;
    const double bg = percentileOf(finiteVals, 2.0);
    std::vector<float> work(n);
    for (std::size_t i = 0; i < n; ++i)
        work[i] = std::isfinite(in[i]) ? float(in[i] - bg) : 0.0f;
    tick(stepsDone);

    // Kernels embedded at the frequency-domain origin (wrap-around split of
    // the centred stamp), transformed on the full frame.
    int kSide = 0;
    const std::vector<float> kMoff = moffatKernel(psf, kSide);
    const std::vector<float> kTarg = gaussianKernel(opt.targetFwhmPx, kSide);
    const int kHalf = kSide / 2;
    auto embed = [&](const std::vector<float>& k) {
        std::vector<float> f(n, 0.0f);
        for (int y = 0; y < kSide; ++y)
            for (int x = 0; x < kSide; ++x) {
                const int yy = ((y - kHalf) % h + h) % h;
                const int xx = ((x - kHalf) % w + w) % w;
                f[std::size_t(yy) * w + xx] = k[std::size_t(y) * kSide + x];
            }
        return f;
    };

    const shape_t shape{std::size_t(h), std::size_t(w)};
    const std::size_t wc = std::size_t(w) / 2 + 1;
    const stride_t rs{ptrdiff_t(sizeof(float) * w), sizeof(float)};
    const stride_t cs{ptrdiff_t(sizeof(std::complex<float>) * wc),
                      sizeof(std::complex<float>)};
    const shape_t axes{0, 1};
    const std::size_t nth = std::max(1u, std::thread::hardware_concurrency());

    std::vector<std::complex<float>> Y(std::size_t(h) * wc), K(Y.size());
    r2c(shape, rs, cs, axes, FORWARD, work.data(), Y.data(), 1.0f, nth);
    {
        std::vector<float> kf = embed(kMoff);
        r2c(shape, rs, cs, axes, FORWARD, kf.data(), K.data(), 1.0f, nth);
    }
    tick(stepsDone);

    if (opt.redIterations > 0) {
        // RED fixed point (Romano, Elad & Milanfar 2017): denoise the
        // current estimate, re-solve the Fourier-diagonal data-consistency
        // filter with the denoised image as prior mean,
        //   X = (conj(OTF_k)·Y + beta·FFT(D(x))) / (|OTF_k|^2 + beta),
        // warm-started at the pure-MCS solution (zero prior mean). The
        // declared target is applied at the end by ONE convolution of the
        // sharp estimate — the partial kernel is still never formed.
        const float beta = float(opt.redPriorWeight);
        // Star-neutral prior: near bright stars the denoiser is blended back
        // to the identity (round feathered mask), so the separable wavelet
        // frame cannot imprint its tensor-product geometry around stars —
        // there the data term and the delivered-PSF contract govern alone.
        // Threshold well below saturation: any star strong enough to leave
        // frame-shaped residuals is excluded from the prior.
        // TIERED radii: a fixed exclusion radius fails for the brightest
        // stars, whose halo wings extend far beyond it — their surviving
        // coefficients imprint the frame's ~4*2^j px tensor geometry as
        // faint squares once the target is aggressive. Brighter star,
        // wider neutral zone (pure-MCS treatment there is cheap: photon-
        // rich neighbourhoods hide MCS noise anyway).
        std::vector<float> starMask;
        if (opt.starNeutralPrior) {
            starMask = coreProtectionMask(
                hotPixels(in.data(), w, h, percentileOf(finiteVals, 99.9)),
                w, h, 6.0, 14.0);
            const std::vector<float> mid = coreProtectionMask(
                hotPixels(in.data(), w, h, percentileOf(finiteVals, 99.99)),
                w, h, 14.0, 32.0);
            const std::vector<float> big = coreProtectionMask(
                hotPixels(in.data(), w, h, percentileOf(finiteVals, 99.999)),
                w, h, 28.0, 64.0);
            for (std::size_t i = 0; i < n; ++i)
                starMask[i] = std::max(starMask[i], std::max(mid[i], big[i]));
        } else {
            // Starless input: no stars to stay neutral around — the prior
            // governs everywhere (a nebular knot is exactly what it is for).
            starMask.assign(n, 0.0f);
        }
        std::vector<std::complex<float>> X(Y.size()), Df(Y.size());
        for (std::size_t i = 0; i < Y.size(); ++i)
            X[i] = Y[i] * std::conj(K[i]) / (std::norm(K[i]) + beta);
        std::vector<float> x(n), d;
        c2r(shape, cs, rs, axes, BACKWARD, X.data(), x.data(), 1.0f / float(n), nth);
        const std::size_t nc = Y.size();
        for (int it = 0; it < opt.redIterations; ++it) {
            // Threshold the fine scales only (noise lives there); coarse
            // scales pass whole so the frame cannot print its support
            // geometry around bright stars.
            d = starletDenoise(x.data(), w, h, opt.redLevels,
                               opt.redThresholdK, 3);
            parallelFor(int(n / 4096 + 1), [&](int b, int e) {
                const std::size_t lo = std::size_t(b) * 4096, hi = std::min(n, std::size_t(e) * 4096);
                for (std::size_t i = lo; i < hi; ++i)
                    d[i] = starMask[i] * x[i] + (1.0f - starMask[i]) * d[i];
            });
            r2c(shape, rs, cs, axes, FORWARD, d.data(), Df.data(), 1.0f, nth);
            parallelFor(int(nc / 4096 + 1), [&](int b, int e) {
                const std::size_t lo = std::size_t(b) * 4096, hi = std::min(nc, std::size_t(e) * 4096);
                for (std::size_t i = lo; i < hi; ++i)
                    X[i] = (Y[i] * std::conj(K[i]) + beta * Df[i])
                         / (std::norm(K[i]) + beta);
            });
            c2r(shape, cs, rs, axes, BACKWARD, X.data(), x.data(), 1.0f / float(n), nth);
            tick(stepsDone);
        }
        std::vector<float> tf = embed(kTarg);
        r2c(shape, rs, cs, axes, FORWARD, tf.data(), K.data(), 1.0f, nth);
        for (std::size_t i = 0; i < Y.size(); ++i) X[i] *= K[i];
        c2r(shape, cs, rs, axes, BACKWARD, X.data(), work.data(), 1.0f / float(n), nth);
    } else {
        // X = Y . OTF_t . conj(OTF_k) / (|OTF_k|^2 + lambda) — K is reused
        // for the target OTF to keep peak memory at two complex planes.
        for (std::size_t i = 0; i < Y.size(); ++i) {
            const std::complex<float> k = K[i];
            const float denom = std::norm(k) + float(opt.lambda);
            Y[i] *= std::conj(k) / denom;
        }
        std::vector<float> tf = embed(kTarg);
        r2c(shape, rs, cs, axes, FORWARD, tf.data(), K.data(), 1.0f, nth);
        for (std::size_t i = 0; i < Y.size(); ++i) Y[i] *= K[i];
        c2r(shape, cs, rs, axes, BACKWARD, Y.data(), work.data(), 1.0f / float(n), nth);
    }
    tick(stepsDone);

    // Saturated-core protection: brightest cores are nonlinear — never
    // truth (*) PSF — so they keep the INPUT pixels, feathered in.
    std::vector<float> out(n);
    if (opt.protectCores && !finiteVals.empty()) {
        const double thr = percentileOf(finiteVals, opt.protectPercentile);
        const std::vector<float> mask =
            coreProtectionMask(hotPixels(in.data(), w, h, thr), w, h);
        for (std::size_t i = 0; i < n; ++i) {
            const float m = mask[i];
            out[i] = m * in[i] + (1.0f - m) * (work[i] + float(bg));
        }
    } else {
        for (std::size_t i = 0; i < n; ++i) out[i] = work[i] + float(bg);
    }
    for (std::size_t i = 0; i < n; ++i)
        if (!std::isfinite(in[i])) out[i] = in[i];
    tick(stepsDone);
    return out;
}

namespace {

// Centred single-channel crop for the ladder measurements.
ImageData centerCrop(const ImageData& img, int channel, int cropSize) {
    const int w = img.width(), h = img.height();
    const int cw = std::min(cropSize, w), ch = std::min(cropSize, h);
    const int x0 = (w - cw) / 2, y0 = (h - ch) / 2;
    ImageData crop(cw, ch, 1, SampleFormat::Float32, ColorSpace::Gray);
    const float* src = img.plane<float>(channel);
    float* dst = crop.plane<float>(0);
    for (int y = 0; y < ch; ++y)
        std::copy(src + std::size_t(y0 + y) * w + x0,
                  src + std::size_t(y0 + y) * w + x0 + cw,
                  dst + std::size_t(y) * cw);
    return crop;
}

// Walk a descending regularization ladder over `setReg` and return the
// first (i.e. largest / strongest) value whose delivered FWHM on the crop
// honours the target; fall back to `fallback` when stars run out, and to
// the last rung when none passes.
template <typename SetReg>
double walkLadder(const ImageData& crop, const DeconvChannelPsf& psf,
                  DeconvOptions opt, const double* ladder, int nLadder,
                  double tolFrac, double fallback, SetReg setReg) {
    double last = ladder[0];
    for (int i = 0; i < nLadder; ++i) {
        last = ladder[i];
        setReg(opt, ladder[i]);
        const ImageData dec = deconvolveToTarget(crop, {psf}, opt);
        const PsfChannelReport rep = measurePsf(dec, 0);
        if (rep.nFitted < 10) return fallback;
        if (std::fabs(rep.fwhmGeo - opt.targetFwhmPx) <= tolFrac * opt.targetFwhmPx)
            return ladder[i];
    }
    return last;
}

} // namespace

double selectLambda(const ImageData& img, int channel,
                    const DeconvChannelPsf& psf, double targetFwhmPx,
                    double tolFrac, int cropSize) {
    if (!img.isValid() || img.format() != SampleFormat::Float32 ||
        channel < 0 || channel >= img.channels())
        return 1e-3;
    static const double kLadder[] = { 3e-3, 1e-3, 3e-4, 1e-4, 3e-5 };
    DeconvOptions opt;
    opt.targetFwhmPx = targetFwhmPx;
    opt.protectCores = false;      // measurement crop; the fitter's own
                                   // saturation gate rejects hot cores
    return walkLadder(centerCrop(img, channel, cropSize), psf, opt,
                      kLadder, 5, tolFrac, 1e-3,
                      [](DeconvOptions& o, double v) { o.lambda = v; });
}

double selectRedWeight(const ImageData& img, int channel,
                       const DeconvChannelPsf& psf, double targetFwhmPx,
                       int iterations, double tolFrac, int cropSize) {
    if (!img.isValid() || img.format() != SampleFormat::Float32 ||
        channel < 0 || channel >= img.channels() || iterations < 1)
        return 1e-2;
    static const double kLadder[] = { 1e-1, 3e-2, 1e-2, 3e-3, 1e-3, 3e-4 };
    DeconvOptions opt;
    opt.targetFwhmPx = targetFwhmPx;
    opt.protectCores = false;
    opt.redIterations = iterations;
    return walkLadder(centerCrop(img, channel, cropSize), psf, opt,
                      kLadder, 6, tolFrac, 1e-2,
                      [](DeconvOptions& o, double v) { o.redPriorWeight = v; });
}

double proxyDeliveredFwhm(const ImageData& starry, int channel,
                          const DeconvChannelPsf& psf, const DeconvOptions& opt,
                          int cropSize) {
    if (!starry.isValid() || starry.format() != SampleFormat::Float32 ||
        channel < 0 || channel >= starry.channels())
        return 0.0;
    // The very filter the starless product received, on the sibling that
    // still has stars; the fitter's saturation gate rejects hot cores, so
    // protection is not needed on the audit crop either.
    DeconvOptions audit = opt;
    audit.protectCores = false;
    audit.starNeutralPrior = true;
    const ImageData dec = deconvolveToTarget(centerCrop(starry, channel, cropSize),
                                             {psf}, audit);
    const PsfChannelReport rep = measurePsf(dec, 0);
    return rep.nFitted >= 10 ? rep.fwhmGeo : 0.0;
}

double starlessExcessFraction(const float* starless, const float* starry,
                              int w, int h, int stride) {
    stride = std::max(1, stride);
    std::vector<float> r;
    r.reserve(std::size_t(w / stride + 1) * std::size_t(h / stride + 1));
    for (int y = 0; y < h; y += stride)
        for (int x = 0; x < w; x += stride) {
            const std::size_t i = std::size_t(y) * w + x;
            if (std::isfinite(starless[i]) && std::isfinite(starry[i]))
                r.push_back(starry[i] - starless[i]);
        }
    if (r.size() < 16) return 0.0;
    std::vector<float> a(r);
    const std::size_t m = a.size() / 2;
    std::nth_element(a.begin(), a.begin() + m, a.end());
    const float med = a[m];
    for (float& v : a) v = std::fabs(v - med);
    std::nth_element(a.begin(), a.begin() + m, a.end());
    const double sigma = 1.4826 * a[m];
    if (!(sigma > 0.0)) return 0.0;              // identical frames: nothing negative
    std::size_t neg = 0;
    for (float v : r) if (v < -5.0 * sigma) ++neg;
    return double(neg) / double(r.size());
}

ImageData deconvolveToTarget(const ImageData& img,
                             const std::vector<DeconvChannelPsf>& psfs,
                             const DeconvOptions& opt,
                             std::atomic<int>* stepsDone) {
    ImageData out = img;
    if (psfs.empty() || img.format() != SampleFormat::Float32) return out;
    const int nch = img.channels();
    for (int c = 0; c < nch; ++c) {
        const DeconvChannelPsf& p =
            psfs[std::size_t(std::min(c, int(psfs.size()) - 1))];
        std::vector<float> plane =
            deconvolveChannel(img.plane<float>(c), img.width(), img.height(),
                              p, opt, stepsDone);
        std::copy(plane.begin(), plane.end(), out.plane<float>(c));
    }
    return out;
}

} // namespace astro

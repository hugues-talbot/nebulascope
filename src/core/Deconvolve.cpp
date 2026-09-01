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

// Separable running-max (binary dilation when fed 0/1) and running-mean
// (feathering) — small local copies; the frame-scale versions live in
// PsfMeasure and are not exposed.
void sepFilter(std::vector<float>& img, int w, int h, int half, bool isMax) {
    std::vector<float> tmp(img.size());
    for (int y = 0; y < h; ++y) {
        const float* row = img.data() + std::size_t(y) * w;
        float* trow = tmp.data() + std::size_t(y) * w;
        for (int x = 0; x < w; ++x) {
            double acc = isMax ? -1e30 : 0.0;
            for (int i = -half; i <= half; ++i) {
                const float v = row[std::min(std::max(x + i, 0), w - 1)];
                if (isMax) acc = std::max(acc, double(v)); else acc += v;
            }
            trow[x] = float(isMax ? acc : acc / (2 * half + 1));
        }
    }
    for (int x = 0; x < w; ++x)
        for (int y = 0; y < h; ++y) {
            double acc = isMax ? -1e30 : 0.0;
            for (int i = -half; i <= half; ++i) {
                const float v = tmp[std::size_t(std::min(std::max(y + i, 0), h - 1)) * w + x];
                if (isMax) acc = std::max(acc, double(v)); else acc += v;
            }
            img[std::size_t(y) * w + x] = float(isMax ? acc : acc / (2 * half + 1));
        }
}

void tick(std::atomic<int>* p) { if (p) p->fetch_add(1); }

} // namespace

std::vector<float> moffatKernel(const DeconvChannelPsf& psf, int& sideOut) {
    const double beta = std::max(1.2, psf.beta);
    const double conv = 2.0 * std::sqrt(std::pow(2.0, 1.0 / beta) - 1.0);
    const double sx = std::max(0.3, psf.fwhmMajPx / conv);
    const double sy = std::max(0.3, psf.fwhmMinPx / conv);
    int side = int(std::ceil(8.0 * std::max(psf.fwhmMajPx, 2.0)));
    side = std::max(33, side | 1);
    sideOut = side;
    const int half = side / 2;
    const double th = psf.paDeg * M_PI / 180.0;
    const double ct = std::cos(th), st = std::sin(th);
    std::vector<float> k(std::size_t(side) * side);
    double sum = 0.0;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const double dx = x - half, dy = y - half;
            const double u = std::pow((dx * ct + dy * st) / sx, 2.0)
                           + std::pow((-dx * st + dy * ct) / sy, 2.0);
            const double v = std::pow(1.0 + u, -beta);
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

    // X = Y . OTF_t . conj(OTF_k) / (|OTF_k|^2 + lambda) — K is reused for
    // the target OTF to keep peak memory at two complex planes.
    {
        for (std::size_t i = 0; i < Y.size(); ++i) {
            const std::complex<float> k = K[i];
            const float denom = std::norm(k) + float(opt.lambda);
            Y[i] *= std::conj(k) / denom;
        }
        std::vector<float> tf = embed(kTarg);
        r2c(shape, rs, cs, axes, FORWARD, tf.data(), K.data(), 1.0f, nth);
        for (std::size_t i = 0; i < Y.size(); ++i) Y[i] *= K[i];
    }
    c2r(shape, cs, rs, axes, BACKWARD, Y.data(), work.data(), 1.0f / float(n), nth);
    tick(stepsDone);

    // Saturated-core protection: brightest cores are nonlinear — never
    // truth (*) PSF — so they keep the INPUT pixels, feathered in.
    std::vector<float> out(n);
    if (opt.protectCores && !finiteVals.empty()) {
        const double thr = percentileOf(finiteVals, opt.protectPercentile);
        std::vector<float> mask(n, 0.0f);
        for (std::size_t i = 0; i < n; ++i)
            if (std::isfinite(in[i]) && in[i] > thr) mask[i] = 1.0f;
        sepFilter(mask, w, h, 5, true);    // dilate r=5
        sepFilter(mask, w, h, 3, false);   // feather (two mean passes)
        sepFilter(mask, w, h, 3, false);
        for (std::size_t i = 0; i < n; ++i) {
            const float m = std::min(1.0f, std::max(0.0f, mask[i]));
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

double selectLambda(const ImageData& img, int channel,
                    const DeconvChannelPsf& psf, double targetFwhmPx,
                    double tolFrac, int cropSize) {
    if (!img.isValid() || img.format() != SampleFormat::Float32 ||
        channel < 0 || channel >= img.channels())
        return 1e-3;
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

    static const double kLadder[] = { 3e-3, 1e-3, 3e-4, 1e-4, 3e-5 };
    DeconvOptions opt;
    opt.targetFwhmPx = targetFwhmPx;
    opt.protectCores = false;      // measurement crop; the fitter's own
                                   // saturation gate rejects hot cores
    double last = kLadder[0];
    for (double lam : kLadder) {
        last = lam;
        opt.lambda = lam;
        const ImageData dec = deconvolveToTarget(crop, {psf}, opt);
        const PsfChannelReport rep = measurePsf(dec, 0);
        if (rep.nFitted < 10) return 1e-3;
        if (std::fabs(rep.fwhmGeo - targetFwhmPx) <= tolFrac * targetFwhmPx)
            return lam;
    }
    return last;
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

// Deconvolve tests: the delivered-PSF contract from the PSF study, in CI.
// A field blurred by a known elliptical Moffat is deconvolved to a declared
// circular Gaussian; the result's stars are then re-measured by PsfMeasure
// and must exhibit the declared FWHM with the elongation gone.
#include "nstest.h"
#include "core/Deconvolve.h"
#include "core/PsfMeasure.h"
#include "core/ImageData.h"
#include <atomic>
#include <cmath>

using namespace astro;

namespace {
unsigned int g_seed = 8675309;
double urand() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return double(g_seed >> 8) / double(1u << 24);
}

// Synthetic star field with exact Moffat truth (same recipe as test_psf).
ImageData makeField(int W, int H, double fmaj, double fmin, double paDeg,
                    double beta) {
    const double kfac = 2.0 * std::sqrt(std::pow(2.0, 1.0 / beta) - 1.0);
    const double sx = fmaj / kfac, sy = fmin / kfac;
    const double th = paDeg * M_PI / 180.0;
    const double ct = std::cos(th), st = std::sin(th);
    ImageData img(W, H, 1, SampleFormat::Float32, ColorSpace::Gray);
    float* p = img.plane<float>(0);
    for (int i = 0; i < W * H; ++i)
        p[i] = float(0.001 + 2e-4 * (urand() - 0.5));
    for (int gy = 30; gy < H - 30; gy += 44)
        for (int gx = 30; gx < W - 30; gx += 44) {
            const double cx = gx + 6.0 * (urand() - 0.5);
            const double cy = gy + 6.0 * (urand() - 0.5);
            const double A = 0.02 + 0.3 * urand();
            for (int y = int(cy) - 16; y <= int(cy) + 16; ++y)
                for (int x = int(cx) - 16; x <= int(cx) + 16; ++x) {
                    const double dx = x - cx, dy = y - cy;
                    const double u = std::pow((dx * ct + dy * st) / sx, 2)
                                   + std::pow((-dx * st + dy * ct) / sy, 2);
                    p[y * W + x] += float(A * std::pow(1.0 + u, -beta));
                }
        }
    return img;
}
} // namespace

NS_TEST(deconv_delivers_declared_psf) {
    // Truth: 4.2 x 3.4 px at PA 30, beta 2.5. Declared target: 2.6 px round.
    const double fmaj = 4.2, fmin = 3.4;
    ImageData img = makeField(512, 512, fmaj, fmin, 30.0, 2.5);

    DeconvChannelPsf psf;
    psf.fwhmMajPx = fmaj; psf.fwhmMinPx = fmin;
    psf.paDeg = 30.0; psf.beta = 2.5;
    DeconvOptions opt;
    opt.targetFwhmPx = 2.6;
    opt.protectCores = false;    // nothing here is saturated

    // Contract-first regularization: the ladder must land on a lambda whose
    // delivered PSF honours the declaration (here the top rungs over-smooth
    // by 12-20% and must be walked past).
    opt.lambda = selectLambda(img, 0, psf, opt.targetFwhmPx);
    NS_CHECK(opt.lambda <= 3e-4);

    std::atomic<int> steps{0};
    const ImageData out = deconvolveToTarget(img, {psf}, opt, &steps);
    NS_CHECK(steps.load() == 4);

    const PsfChannelReport rep = measurePsf(out, 0);
    NS_CHECK(rep.nFitted > 60);
    // The contract: delivered geometric-mean FWHM within 6% of declaration
    // (5% selection tolerance + the Moffat fitter's beta ceiling on a
    // near-Gaussian result), and the field elongation at least halved.
    NS_CHECK(std::fabs(rep.fwhmGeo - opt.targetFwhmPx) < 0.06 * opt.targetFwhmPx);
    NS_CHECK(rep.ecc < 0.35);    // truth was 0.59; full removal needs lambda->0
}

NS_TEST(deconv_core_protection_and_nan) {
    ImageData img = makeField(256, 256, 4.0, 4.0, 0.0, 2.5);
    float* p = img.plane<float>(0);
    // One "saturated" plateau and one NaN hole.
    for (int y = 100; y < 104; ++y)
        for (int x = 100; x < 104; ++x) p[y * 256 + x] = 60000.0f;
    p[40 * 256 + 40] = std::nanf("");

    DeconvChannelPsf psf; psf.fwhmMajPx = 4.0; psf.fwhmMinPx = 4.0;
    DeconvOptions opt; opt.targetFwhmPx = 2.5; opt.lambda = 1e-3;
    opt.protectCores = true; opt.protectPercentile = 99.9;

    const ImageData out = deconvolveToTarget(img, {psf}, opt);
    const float* q = out.plane<float>(0);
    NS_CHECK(std::isnan(q[40 * 256 + 40]));               // NaN passes through
    NS_CHECK(std::fabs(q[101 * 256 + 101] - 60000.0f) < 1.0f); // core kept input
    int finite = 0;
    for (int i = 0; i < 256 * 256; ++i) finite += std::isfinite(q[i]) ? 1 : 0;
    NS_CHECK(finite == 256 * 256 - 1);
}

NS_TEST(deconv_moffat_kernel_shape) {
    DeconvChannelPsf psf;
    psf.fwhmMajPx = 5.0; psf.fwhmMinPx = 4.0; psf.paDeg = 25.0; psf.beta = 2.2;
    int side = 0;
    const std::vector<float> k = moffatKernel(psf, side);
    NS_CHECK(side >= 33 && (side & 1) == 1);
    double sum = 0.0; int argmax = 0;
    for (int i = 0; i < side * side; ++i) {
        sum += k[i];
        if (k[i] > k[argmax]) argmax = i;
    }
    NS_CHECK(std::fabs(sum - 1.0) < 1e-5);
    NS_CHECK(argmax == (side / 2) * side + side / 2);      // centred peak
}

int main() { return nstest::runAll(); }

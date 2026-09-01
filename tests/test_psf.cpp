// PsfMeasure tests: elliptical-Moffat recovery on a synthetic star field with
// exact ground truth — the same contract the PSF study's Python instrument
// was validated against (3% of PixInsight's FWHMEccentricity).
#include "nstest.h"
#include "core/PsfMeasure.h"
#include "core/ImageData.h"
#include <atomic>
#include <cmath>

using namespace astro;

namespace {
unsigned int g_seed = 24601;
double urand() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return double(g_seed >> 8) / double(1u << 24);
}
}

NS_TEST(psf_measure_recovers_elliptical_moffat) {
    // Truth: fwhm 4.2 x 3.4 px at PA 30 deg, beta 2.5, on a flat background
    // with mild deterministic noise. Stars on a jittered grid, isolated.
    const int W = 512, H = 512;
    const double fmaj = 4.2, fmin = 3.4, paDeg = 30.0, beta = 2.5;
    const double kfac = 2.0 * std::sqrt(std::pow(2.0, 1.0 / beta) - 1.0);
    const double sx = fmaj / kfac, sy = fmin / kfac;
    const double th = paDeg * M_PI / 180.0;
    const double ct = std::cos(th), st = std::sin(th);

    ImageData img(W, H, 1, SampleFormat::Float32, ColorSpace::Gray);
    float* p = img.plane<float>(0);
    for (int i = 0; i < W * H; ++i)
        p[i] = float(0.001 + 2e-4 * (urand() - 0.5));      // bg + noise
    int placed = 0;
    for (int gy = 30; gy < H - 30; gy += 44)
        for (int gx = 30; gx < W - 30; gx += 44) {
            const double cx = gx + 6.0 * (urand() - 0.5);
            const double cy = gy + 6.0 * (urand() - 0.5);
            const double A = 0.02 + 0.3 * urand();
            for (int y = std::max(0, int(cy) - 16); y <= std::min(H - 1, int(cy) + 16); ++y)
                for (int x = std::max(0, int(cx) - 16); x <= std::min(W - 1, int(cx) + 16); ++x) {
                    const double dx = x - cx, dy = y - cy;
                    const double u = std::pow((dx * ct + dy * st) / sx, 2)
                                   + std::pow((-dx * st + dy * ct) / sy, 2);
                    p[y * W + x] += float(A * std::pow(1.0 + u, -beta));
                }
            ++placed;
        }
    NS_CHECK(placed > 100);

    std::atomic<int> done{0}, total{0};
    const PsfChannelReport rep = measurePsf(img, 0, &done, &total);
    NS_CHECK(total.load() > 60 && done.load() == total.load());
    NS_CHECK(rep.nFitted > 60);
    NS_CHECK(std::fabs(rep.fwhmMaj - fmaj) < 0.15);
    NS_CHECK(std::fabs(rep.fwhmMin - fmin) < 0.15);
    NS_CHECK(std::fabs(rep.paDeg - paDeg) < 3.0);
    NS_CHECK(std::fabs(rep.beta - beta) < 0.3);
    const double eccTruth = std::sqrt(1.0 - (fmin / fmaj) * (fmin / fmaj));
    NS_CHECK(std::fabs(rep.ecc - eccTruth) < 0.05);
    // Zones populated across the field (uniform truth => uniform map).
    int zonesOk = 0;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
            if (rep.zone[r][c].nStars >= 5 &&
                std::fabs(rep.zone[r][c].fwhmGeo - std::sqrt(fmaj * fmin)) < 0.25)
                ++zonesOk;
    NS_CHECK(zonesOk >= 10);
}

NS_TEST(psf_single_cutout_exact) {
    // One noiseless star dead-centre: near-exact parameter recovery.
    const int side = 27, half = 13;
    const double fmaj = 5.0, fmin = 4.0, beta = 3.0;
    const double kfac = 2.0 * std::sqrt(std::pow(2.0, 1.0 / beta) - 1.0);
    const double sx = fmaj / kfac, sy = fmin / kfac;
    const double th = -40.0 * M_PI / 180.0;
    const double ct = std::cos(th), st = std::sin(th);
    std::vector<float> cut(side * side);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const double dx = x - half - 0.3, dy = y - half + 0.2;
            const double u = std::pow((dx * ct + dy * st) / sx, 2)
                           + std::pow((-dx * st + dy * ct) / sy, 2);
            cut[y * side + x] = float(0.01 + 0.5 * std::pow(1.0 + u, -beta));
        }
    PsfStar s;
    NS_CHECK(fitMoffatCutout(cut.data(), side, 1e9, s));
    NS_CHECK(std::fabs(s.fwhmMaj - fmaj) < 0.05);
    NS_CHECK(std::fabs(s.fwhmMin - fmin) < 0.05);
    NS_CHECK(std::fabs(s.paDeg - (-40.0)) < 1.0);
    NS_CHECK(std::fabs(s.beta - beta) < 0.1);
    NS_CHECK(std::fabs(s.x - 0.3) < 0.05 && std::fabs(s.y - (-0.2)) < 0.05);
}

int main() { return nstest::runAll(); }

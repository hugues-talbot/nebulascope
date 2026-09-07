// StarRemove tests: analytic star removal on a field with exact truth — the
// nebula must come back within the noise, stars (a clipped one included)
// must be gone, and a nebular knot must survive.
#include "nstest.h"
#include "core/StarRemove.h"
#include "core/PsfMeasure.h"
#include "core/ImageData.h"
#include <cmath>

using namespace astro;

namespace {
unsigned int g_seed = 424242;
double urand() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return double(g_seed >> 8) / double(1u << 24);
}
}

NS_TEST(starremove_recovers_nebula) {
    const int W = 512, H = 512;
    const double fmaj = 4.2, fmin = 3.4, paDeg = 30.0, beta = 2.5;
    const double kfac = 2.0 * std::sqrt(std::pow(2.0, 1.0 / beta) - 1.0);
    const double sx = fmaj / kfac, sy = fmin / kfac;
    const double th = paDeg * M_PI / 180.0, ct = std::cos(th), st = std::sin(th);
    const double noiseAmp = 1e-3, sigma = noiseAmp / std::sqrt(12.0);

    // Truth: sky + wide nebulosity + one broad knot (FWHM 16 px, not a star).
    std::vector<double> truth(W * H, 0.001);
    const double blobs[][4] = { {120, 110, 70, 0.15}, {390, 140, 80, 0.10},
                                {130, 400, 60, 0.20}, {400, 390, 75, 0.12},
                                {256, 256, 16.0 / 2.3548, 0.05} };
    for (const auto& b : blobs)
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const double dx = x - b[0], dy = y - b[1];
                truth[y * W + x] += b[3] * std::exp(-0.5 * (dx * dx + dy * dy) / (b[2] * b[2]));
            }
    // Stars: a grid of faint-to-moderate ones plus three bright, one clipped.
    struct S { double x, y, A; };
    std::vector<S> stars;
    for (int gy = 30; gy < H - 30; gy += 44)
        for (int gx = 30; gx < W - 30; gx += 44) {
            if (std::hypot(gx - 256.0, gy - 256.0) < 40) continue;   // keep the knot clean
            stars.push_back({ gx + 6.0 * (urand() - 0.5), gy + 6.0 * (urand() - 0.5),
                              0.02 + 0.3 * urand() });
        }
    stars.push_back({ 300.3, 60.6, 3.0 });          // all three clipped at 1.0 below;
    stars.push_back({ 60.2, 250.7, 8.0 });          // 20x the clip level is a bright
    stars.push_back({ 460.4, 260.3, 20.0 });        // master's worst case
    ImageData img(W, H, 1, SampleFormat::Float32, ColorSpace::Gray);
    float* p = img.plane<float>(0);
    for (int i = 0; i < W * H; ++i) p[i] = float(truth[i] + noiseAmp * (urand() - 0.5));
    for (const S& s : stars)
        for (int y = std::max(0, int(s.y) - 120); y <= std::min(H - 1, int(s.y) + 120); ++y)
            for (int x = std::max(0, int(s.x) - 120); x <= std::min(W - 1, int(s.x) + 120); ++x) {
                const double dx = x - s.x, dy = y - s.y;
                const double u = std::pow((dx * ct + dy * st) / sx, 2) + std::pow((-dx * st + dy * ct) / sy, 2);
                p[y * W + x] += float(s.A * std::pow(1.0 + u, -beta));
            }
    for (int i = 0; i < W * H; ++i) p[i] = std::min(p[i], 1.0f);   // saturation
    p[10 * W + 10] = std::nanf("");

    const PsfChannelReport shape = measurePsf(img, 0);
    NS_CHECK(shape.nFitted > 50);

    std::atomic<int> done{0}, total{0};
    const StarRemoveResult res = removeStars(p, W, H, shape, {}, &done, &total);
    NS_CHECK(done.load() == total.load() && total.load() > 0);
    NS_CHECK(res.nRemoved >= int(0.9 * stars.size()));
    NS_CHECK(res.nClipped >= 1);
    NS_CHECK(res.nInpainted >= 3);
    NS_CHECK(res.nFlagged == 0);
    NS_CHECK(res.noiseSigma > 0.7 * sigma && res.noiseSigma < 1.5 * sigma);

    const std::vector<float>& sl = res.starless;
    NS_CHECK(std::isnan(sl[10 * W + 10]));
    // Nebula recovered: RMS against truth at the noise, worst deviation a
    // few sigmas everywhere — inpainted cores included (the harmonic fill
    // of gentle nebulosity is a few-sigma guess at worst here).
    double s2 = 0, worst = 0; int cnt = 0, finite = 0;
    for (int i = 0; i < W * H; ++i) {
        if (!std::isfinite(sl[i])) continue;
        ++finite;
        const double d = sl[i] - truth[i];
        s2 += d * d; ++cnt;
        worst = std::max(worst, std::fabs(d));
    }
    NS_CHECK(finite == W * H - 1);
    NS_CHECK(std::sqrt(s2 / cnt) < 1.3 * sigma);
    NS_CHECK(worst < 8.0 * sigma);
    // Every star site is flat: no star, no crater.
    for (const S& s : stars) {
        const int x = int(std::lround(s.x)), y = int(std::lround(s.y));
        NS_CHECK(std::fabs(sl[y * W + x] - truth[y * W + x]) < 8.0 * sigma);
    }
    // The knot survived.
    NS_CHECK(std::fabs(sl[256 * W + 256] - truth[256 * W + 256]) < 4.0 * sigma);
    // Stars-only is the complement by construction (starless + stars = input
    // outside the inpainted cores).
    const RemovedStar& b = res.stars[0];
    NS_CHECK(b.clipped && b.amp > 17.0 && b.amp < 23.0 && std::hypot(b.x - 460.4, b.y - 260.3) < 0.5);

    // Whole-image wrapper agrees with the plane call.
    std::vector<StarRemoveResult> reps;
    const ImageData out = removeStarsImage(img, {shape}, {}, &reps);
    NS_CHECK(reps.size() == 1 && reps[0].nRemoved == res.nRemoved);
    const float* q = out.plane<float>(0);
    double diff = 0;
    for (int i = 0; i < W * H; ++i)
        if (std::isfinite(q[i])) diff = std::max(diff, std::fabs(double(q[i]) - double(sl[i])));
    NS_CHECK(diff == 0.0);
}

int main() { return nstest::runAll(); }

// Debayer tests: pattern parsing (keywords + offsets), exact reconstruction
// on constant fields for every pattern × method, near-exact bilinear/RCD on
// smooth gradients, and detection + demosaic of a real ASIAIR CFA fixture.
#include "nstest.h"
#include "core/Debayer.h"
#include "core/ImageHeader.h"
#include "io/ImageReader.h"
#include <cmath>

using namespace astro;

static const BayerPattern kPatterns[] = {
    BayerPattern::RGGB, BayerPattern::BGGR, BayerPattern::GRBG, BayerPattern::GBRG };
static const DebayerMethod kMethods[] = {
    DebayerMethod::Superpixel, DebayerMethod::Bilinear, DebayerMethod::RCD };

// Mosaic an RGB truth image into a CFA plane for `p`.
static ImageData mosaic(const ImageData& rgb, BayerPattern p) {
    const char* n = bayerPatternName(p);
    auto chanAt = [n](int x, int y) {
        const char c = n[(y & 1) * 2 + (x & 1)];
        return c == 'R' ? 0 : c == 'G' ? 1 : 2;
    };
    ImageData cfa(rgb.width(), rgb.height(), 1, SampleFormat::Float32, ColorSpace::Gray);
    float* d = cfa.plane<float>(0);
    for (int y = 0; y < rgb.height(); ++y)
        for (int x = 0; x < rgb.width(); ++x) {
            const std::size_t o = std::size_t(y) * rgb.width() + x;
            d[o] = rgb.plane<float>(chanAt(x, y))[o];
        }
    return cfa;
}

NS_TEST(bayer_pattern_parsing) {
    NS_CHECK(bayerPatternFromString("RGGB") == BayerPattern::RGGB);
    NS_CHECK(bayerPatternFromString("'rggb    '") == BayerPattern::RGGB);
    NS_CHECK(bayerPatternFromString("BGGR") == BayerPattern::BGGR);
    NS_CHECK(bayerPatternFromString("junk") == BayerPattern::None);

    ImageHeader h;
    h.cards.push_back({ "BAYERPAT", "'RGGB    '", QString() });
    NS_CHECK(bayerPatternFromHeader(h) == BayerPattern::RGGB);
    // Odd offsets shift the phase: x → GRBG, y → GBRG, both → BGGR.
    h.cards.push_back({ "XBAYROFF", "1", QString() });
    NS_CHECK(bayerPatternFromHeader(h) == BayerPattern::GRBG);
    h.cards.push_back({ "YBAYROFF", "3", QString() });
    NS_CHECK(bayerPatternFromHeader(h) == BayerPattern::BGGR);
}

NS_TEST(debayer_constant_field_exact) {
    // A constant-colour scene must reconstruct exactly under every pattern
    // and every method (no interpolation can disturb a constant).
    ImageData rgb(32, 32, 3, SampleFormat::Float32, ColorSpace::RGB);
    const float val[3] = { 0.8f, 0.5f, 0.2f };
    for (int c = 0; c < 3; ++c)
        for (std::size_t i = 0; i < rgb.samplesPerChannel(); ++i)
            rgb.plane<float>(c)[i] = val[c];
    for (BayerPattern p : kPatterns) {
        ImageData cfa = mosaic(rgb, p);
        for (DebayerMethod m : kMethods) {
            ImageData out = debayer(cfa, p, m);
            NS_CHECK(out.isValid() && out.channels() == 3);
            double worst = 0;
            for (int c = 0; c < 3; ++c)
                for (std::size_t i = 0; i < out.samplesPerChannel(); ++i)
                    worst = std::max(worst, double(std::fabs(out.plane<float>(c)[i] - val[c])));
            NS_CHECK(worst < 1e-5);
        }
    }
}

NS_TEST(debayer_gradient_close) {
    // Smooth linear gradients: full-resolution methods must track the truth
    // closely in the interior (borders use mirrored samples).
    const int N = 64;
    ImageData rgb(N, N, 3, SampleFormat::Float32, ColorSpace::RGB);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const std::size_t o = std::size_t(y) * N + x;
            rgb.plane<float>(0)[o] = 0.1f + 0.6f * x / (N - 1);
            rgb.plane<float>(1)[o] = 0.2f + 0.5f * y / (N - 1);
            rgb.plane<float>(2)[o] = 0.3f + 0.2f * (x + y) / (2 * N - 2);
        }
    ImageData cfa = mosaic(rgb, BayerPattern::RGGB);
    for (DebayerMethod m : { DebayerMethod::Bilinear, DebayerMethod::RCD }) {
        ImageData out = debayer(cfa, BayerPattern::RGGB, m);
        double worst = 0;
        for (int c = 0; c < 3; ++c)
            for (int y = 4; y < N - 4; ++y)
                for (int x = 4; x < N - 4; ++x) {
                    const std::size_t o = std::size_t(y) * N + x;
                    worst = std::max(worst, double(std::fabs(
                        out.plane<float>(c)[o] - rgb.plane<float>(c)[o])));
                }
        NS_CHECK(worst < 5e-3);
    }
}

NS_TEST(debayer_superpixel_geometry) {
    ImageData rgb(30, 22, 3, SampleFormat::Float32, ColorSpace::RGB);
    for (int c = 0; c < 3; ++c)
        for (std::size_t i = 0; i < rgb.samplesPerChannel(); ++i)
            rgb.plane<float>(c)[i] = 0.5f;
    ImageData out = debayer(mosaic(rgb, BayerPattern::GBRG),
                            BayerPattern::GBRG, DebayerMethod::Superpixel);
    NS_CHECK(out.width() == 15 && out.height() == 11 && out.channels() == 3);
}

NS_TEST(debayer_real_asiair_fixture) {
    // 128×128 crop of a real ASI 533MC light (ASIAIR-style header).
    io::LoadResult lr = io::loadImage(QStringLiteral(NS_TESTDATA_DIR) + "/cfa_asiair_rggb.fits");
    NS_CHECK(lr.ok && lr.image.isValid() && lr.image.channels() == 1);
    NS_CHECK(bayerPatternFromHeader(lr.header) == BayerPattern::RGGB);
    ImageData out = debayer(lr.image, BayerPattern::RGGB, DebayerMethod::RCD);
    NS_CHECK(out.isValid() && out.channels() == 3 &&
             out.width() == 128 && out.height() == 128);
    // Sky-background sanity: all finite, and the channel means sit within a
    // plausible factor of each other (a raw light is not wildly colour-cast).
    double mean[3] = {0, 0, 0};
    for (int c = 0; c < 3; ++c) {
        const float* p = out.plane<float>(c);
        for (std::size_t i = 0; i < out.samplesPerChannel(); ++i) {
            NS_CHECK(std::isfinite(p[i]));
            mean[c] += p[i];
        }
        mean[c] /= double(out.samplesPerChannel());
        NS_CHECK(mean[c] > 0.0);
    }
    NS_CHECK(mean[1] / mean[0] > 0.2 && mean[1] / mean[0] < 5.0);
    NS_CHECK(mean[1] / mean[2] > 0.2 && mean[1] / mean[2] < 5.0);
}

int main() { return nstest::runAll(); }

// Core math tests: stretch/LUT invariants, adjustments, geometry, stats.
#include "nstest.h"
#include "core/Stretch.h"
#include "core/Adjustments.h"
#include "core/Transform.h"
#include "core/ImageStats.h"
#include "core/ImageData.h"
#include <algorithm>
#include <cmath>

using namespace astro;

static ImageData makeRamp(int w, int h, float lo, float hi) {
    ImageData img(w, h, 1, SampleFormat::Float32, ColorSpace::Gray);
    float* p = img.plane<float>(0);
    const std::size_t n = img.samplesPerChannel();
    for (std::size_t i = 0; i < n; ++i)
        p[i] = lo + (hi - lo) * float(i) / float(n - 1);
    return img;
}

NS_TEST(lut_monotone_all_fns) {
    // Every transfer shape must be monotone non-decreasing over the window.
    ChannelStretch cs; cs.black = 0.1; cs.mid = 0.4; cs.white = 0.9;
    GHSParams g;
    for (StretchFn fn : { StretchFn::Linear, StretchFn::Log, StretchFn::Asinh, StretchFn::GHS }) {
        auto lut = buildLut(fn, cs, g, 4096);
        NS_CHECK(lut.size() == 4096);
        for (std::size_t i = 1; i < lut.size(); ++i)
            NS_CHECK(lut[i] + 1e-6f >= lut[i - 1]);
        NS_CHECK(lut.front() >= 0.0f && lut.back() <= 1.0f + 1e-6f);
    }
}

NS_TEST(window_uses_full_range) {
    // A narrow window must still span the whole output range — the
    // anti-posterization invariant (LUT holds the shape, windowCoord windows).
    ChannelStretch cs;                      // identity B/M/W inside the window
    const double lo = 0.0, hi = 65535.0;
    // narrow window: black at 1000/65535, white at 1200/65535
    cs.black = 1000.0 / 65535.0; cs.white = 1200.0 / 65535.0; cs.mid = 0.5 * (cs.black + cs.white);
    NS_CHECK_NEAR(windowCoord(1000.0, lo, hi, cs), 0.0, 1e-9);
    NS_CHECK_NEAR(windowCoord(1200.0, lo, hi, cs), 1.0, 1e-9);
    NS_CHECK_NEAR(windowCoord(1100.0, lo, hi, cs), 0.5, 1e-3);
    NS_CHECK(windowCoord(500.0, lo, hi, cs) == 0.0);     // clamps
    NS_CHECK(windowCoord(60000.0, lo, hi, cs) == 1.0);
}

NS_TEST(mtf_fixed_points) {
    NS_CHECK_NEAR(mtf(0.0, 0.3), 0.0, 1e-12);
    NS_CHECK_NEAR(mtf(1.0, 0.3), 1.0, 1e-12);
    NS_CHECK_NEAR(mtf(0.5, 0.5), 0.5, 1e-12);   // m = x -> 0.5
    NS_CHECK_NEAR(mtf(0.3, 0.3), 0.5, 1e-12);   // midtone maps to half
}

NS_TEST(ghs_symmetry_point_max_slope) {
    // ghsSlope must peak at SP.
    const double D = 2.0, b = 6.0, SP = 0.25;
    const double peak = ghsSlope(SP, D, b, SP);
    for (double x = 0.0; x <= 1.0; x += 0.01)
        NS_CHECK(ghsSlope(x, D, b, SP) <= peak + 1e-9);
}

NS_TEST(adjust_tone_identity_and_pins) {
    AdjustParams a;                          // identity
    NS_CHECK(a.identity());
    for (float v : { 0.0f, 0.25f, 0.5f, 0.9f, 1.0f })
        NS_CHECK_NEAR(applyTone(v, a), v, 1e-6);
    // Shadows/highlights pin both ends.
    a.shadows = 0.7; a.highlights = -0.5;
    NS_CHECK_NEAR(applyTone(0.0f, a), 0.0, 1e-6);
    NS_CHECK_NEAR(applyTone(1.0f, a), 1.0, 1e-6);
    // Tone stays monotone for documented ranges.
    AdjustParams m; m.contrast = 0.8; m.gamma = 2.2; m.shadows = 0.5; m.highlights = 0.5;
    float prev = -1.0f;
    for (int i = 0; i <= 1000; ++i) {
        const float y = applyTone(float(i) / 1000.0f, m);
        NS_CHECK(y + 1e-5f >= prev);
        prev = y;
    }
}

NS_TEST(adjust_color_neutral_under_satvib) {
    // Saturation/vibrance must not shift a neutral (gray) pixel.
    AdjustParams a; a.saturation = 0.8; a.vibrance = 0.5;
    float r = 0.4f, g = 0.4f, b = 0.4f;
    applyColor(r, g, b, a);
    NS_CHECK_NEAR(r, 0.4, 1e-4);
    NS_CHECK_NEAR(g, 0.4, 1e-4);
    NS_CHECK_NEAR(b, 0.4, 1e-4);
}

NS_TEST(rotate90_roundtrip_exact) {
    ImageData img = makeRamp(31, 17, 0.0f, 100.0f);     // odd sizes on purpose
    ImageData r = rotate90(img, true);
    NS_CHECK(r.width() == 17 && r.height() == 31);
    ImageData back = rotate90(r, false);
    NS_CHECK(back.width() == 31 && back.height() == 17);
    const float* p0 = img.plane<float>(0);
    const float* p1 = back.plane<float>(0);
    for (std::size_t i = 0; i < img.samplesPerChannel(); ++i)
        NS_CHECK(p0[i] == p1[i]);                        // lossless, bit-exact
}

NS_TEST(flips_are_involutions) {
    ImageData img = makeRamp(23, 9, -5.0f, 5.0f);
    for (auto flip : { flipHorizontal, flipVertical }) {
        ImageData once = flip(img);
        ImageData twice = flip(once);
        const float* p0 = img.plane<float>(0);
        const float* p1 = twice.plane<float>(0);
        for (std::size_t i = 0; i < img.samplesPerChannel(); ++i)
            NS_CHECK(p0[i] == p1[i]);
    }
}

NS_TEST(rotate_arbitrary_geometry) {
    ImageData img = makeRamp(100, 50, 0.0f, 1.0f);
    ImageData r = rotateArbitrary(img, 90.0);            // bbox of 90° = swapped dims
    NS_CHECK(std::abs(r.width() - 50) <= 1);
    NS_CHECK(std::abs(r.height() - 100) <= 1);
    ImageData r45 = rotateArbitrary(img, 45.0);
    NS_CHECK(r45.width() > 100 && r45.height() > 50);    // canvas grows
    // Corners of the expanded canvas are NaN blanks.
    NS_CHECK(std::isnan(r45.plane<float>(0)[0]));
}

NS_TEST(stats_ramp) {
    ImageData img = makeRamp(200, 100, 10.0f, 20.0f);
    auto st = computeStats(img);
    NS_CHECK(st.size() == 1);
    NS_CHECK_NEAR(st[0].min, 10.0, 1e-4);
    NS_CHECK_NEAR(st[0].max, 20.0, 1e-4);
    NS_CHECK_NEAR(st[0].median, 15.0, 0.1);
    NS_CHECK(st[0].p99 > 19.5f && st[0].p99 <= 20.0f);
}

NS_TEST(fit_channel_stretch_recovers_parameters) {
    // Generate display values with a KNOWN Linear+MTF stretch, then fit —
    // the recovered curve must reproduce the display to tight tolerance
    // (this is the "colour transport as stretch" fitting core).
    const std::size_t n = 20000;
    std::vector<float> raw(n), disp(n);
    ChannelStretch truth; truth.black = 0.15; truth.mid = 0.35; truth.white = 0.85;
    const double m = (truth.mid - truth.black) / (truth.white - truth.black);
    for (std::size_t i = 0; i < n; ++i) {
        raw[i] = float(i) / (n - 1);
        disp[i] = float(mtf(windowCoord(raw[i], 0.0, 1.0, truth), m));
    }
    ChannelStretch fit;
    const double rmse = fitChannelStretch(raw.data(), disp.data(), n, 1, 0.0, 1.0, fit);
    NS_CHECK(rmse < 5e-3);
    // Functional agreement matters more than parameter identity.
    const double fm = (fit.mid - fit.black) / std::max(1e-6, fit.white - fit.black);
    double worst = 0;
    for (std::size_t i = 0; i < n; i += 97) {
        const double d = mtf(windowCoord(raw[i], 0.0, 1.0, fit), fm);
        worst = std::max(worst, std::fabs(d - disp[i]));
    }
    NS_CHECK(worst < 0.02);
}

NS_TEST(fit_channel_stretch_intensity_weighting) {
    // A corrupted background must not drag the fit away from the signal:
    // build a target that follows a known curve for bright pixels but is
    // biased in the dark region — the intensity-weighted fit should track
    // the bright-region curve clearly better than the uniform fit.
    const std::size_t n = 20000;
    std::vector<float> raw(n), tgt(n);
    ChannelStretch truth; truth.black = 0.1; truth.mid = 0.4; truth.white = 0.9;
    const double m = (truth.mid - truth.black) / (truth.white - truth.black);
    for (std::size_t i = 0; i < n; ++i) {
        raw[i] = float(i) / (n - 1);
        double d = mtf(windowCoord(raw[i], 0.0, 1.0, truth), m);
        if (raw[i] < 0.25) d = std::min(1.0, d + 0.08);   // background bias
        tgt[i] = float(d);
    }
    ChannelStretch fitU, fitW;
    fitChannelStretch(raw.data(), tgt.data(), n, 1, 0.0, 1.0, fitU, false);
    fitChannelStretch(raw.data(), tgt.data(), n, 1, 0.0, 1.0, fitW, true);
    auto brightErr = [&](const ChannelStretch& cs) {
        const double fm = (cs.mid - cs.black) / std::max(1e-6, cs.white - cs.black);
        double worst = 0;
        for (std::size_t i = 0; i < n; i += 61) {
            if (raw[i] < 0.4) continue;
            const double want = mtf(windowCoord(raw[i], 0.0, 1.0, truth), m);
            const double got  = mtf(windowCoord(raw[i], 0.0, 1.0, cs), fm);
            worst = std::max(worst, std::fabs(got - want));
        }
        return worst;
    };
    NS_CHECK(brightErr(fitW) < brightErr(fitU));
    NS_CHECK(brightErr(fitW) < 0.03);
}

NS_TEST(fit_color_adjust_recovers_cross_channel) {
    // Targets built by a KNOWN colour adjustment of varied display triples:
    // the stage-2 fitter must recover a functionally equivalent adjustment.
    const std::size_t n = 12000;
    std::vector<float> dR(n), dG(n), dB(n), tR(n), tG(n), tB(n);
    AdjustParams truth;
    truth.temperature = 0.30; truth.saturation = 0.35; truth.tint = -0.15;
    for (std::size_t i = 0; i < n; ++i) {
        const float u = float(i) / (n - 1);
        dR[i] = u;
        dG[i] = 0.2f + 0.6f * u;
        dB[i] = 0.9f - 0.5f * u;
        float r = dR[i], g = dG[i], b = dB[i];
        applyColor(r, g, b, truth);
        tR[i] = r; tG[i] = g; tB[i] = b;
    }
    AdjustParams fit;
    const double rmse = fitColorAdjust(dR.data(), dG.data(), dB.data(),
                                       tR.data(), tG.data(), tB.data(), n, 1, fit);
    NS_CHECK(rmse < 0.01);
    double worst = 0;
    for (std::size_t i = 0; i < n; i += 53) {
        float r = dR[i], g = dG[i], b = dB[i];
        applyColor(r, g, b, fit);
        worst = std::max({ worst, double(std::fabs(r - tR[i])),
                           double(std::fabs(g - tG[i])), double(std::fabs(b - tB[i])) });
    }
    NS_CHECK(worst < 0.03);
}

NS_TEST(screen_blend_math) {
    // The star-recomposition op: 1-(1-a)(1-b). Identity with black stars,
    // saturation-safe, commutative.
    auto screen = [](double a, double b) { return 1.0 - (1.0 - a) * (1.0 - b); };
    NS_CHECK_NEAR(screen(0.3, 0.0), 0.3, 1e-12);
    NS_CHECK_NEAR(screen(0.0, 0.7), 0.7, 1e-12);
    NS_CHECK(screen(0.9, 0.9) <= 1.0);
    NS_CHECK_NEAR(screen(0.4, 0.6), screen(0.6, 0.4), 1e-12);
    NS_CHECK(screen(0.4, 0.6) >= 0.6);                   // never darkens
}

int main() { return nstest::runAll(); }

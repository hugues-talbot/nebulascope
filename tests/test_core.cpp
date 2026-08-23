// Core math tests: stretch/LUT invariants, adjustments, geometry, stats.
#include "nstest.h"
#include "core/Stretch.h"
#include "core/Adjustments.h"
#include "core/Transform.h"
#include "core/ImageStats.h"
#include "core/ImageData.h"
#include "render/StretchModel.h"
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

// The sidecar "display" block must reproduce the FULL appearance — the case
// that motivated it was a non-destructive transport fit (per-channel window
// + colour adjustments) that came back as adjustments-over-a-fresh-auto-STF.
NS_TEST(display_state_json_round_trip) {
    StretchModel::State s;
    s.valid = true;
    s.fn = StretchFn::GHS;
    s.count = 3;
    for (int c = 0; c < 3; ++c) {
        s.chan[c].black = 0.01 * (c + 1);
        s.chan[c].mid   = 0.20 + 0.05 * c;
        s.chan[c].white = 0.90 - 0.03 * c;
        s.lo[c] = 0.001 * c;
        s.hi[c] = 0.5 + 0.1 * c;
    }
    s.ghs = GHSParams{ 2.5, 3.0, 0.22, 0.05, 0.95 };
    s.cmap = Colormap::Ds9Cool;
    s.cmapInvert = true;
    s.cmapSplit = true;
    s.split = 0.33;
    s.adj.hue = -0.9999591436509916;          // the field-report values
    s.adj.saturation = 0.11033604567339211;
    s.adj.temperature = -0.07384259927655563;
    s.adj.tint = -0.027828011089251314;
    s.adj.gamma = 1.2;

    const StretchModel::State r =
        StretchModel::stateFromJson(StretchModel::stateToJson(s));
    NS_CHECK(r.valid);
    NS_CHECK(r.fn == StretchFn::GHS);
    NS_CHECK(r.count == 3);
    for (int c = 0; c < 3; ++c) {
        NS_CHECK(r.chan[c].black == s.chan[c].black);
        NS_CHECK(r.chan[c].mid   == s.chan[c].mid);
        NS_CHECK(r.chan[c].white == s.chan[c].white);
        NS_CHECK(r.lo[c] == s.lo[c] && r.hi[c] == s.hi[c]);
    }
    NS_CHECK(r.ghs.D == 2.5 && r.ghs.b == 3.0 && r.ghs.SP == 0.22 &&
             r.ghs.LP == 0.05 && r.ghs.HP == 0.95);
    NS_CHECK(r.cmap == Colormap::Ds9Cool && r.cmapInvert && r.cmapSplit);
    NS_CHECK(r.split == 0.33);
    NS_CHECK(r.adj.hue == s.adj.hue && r.adj.saturation == s.adj.saturation);
    NS_CHECK(r.adj.temperature == s.adj.temperature && r.adj.tint == s.adj.tint);
    NS_CHECK(r.adj.gamma == 1.2);
    // Garbage / legacy sidecars (no "display") never yield a valid state.
    NS_CHECK(!StretchModel::stateFromJson(QJsonObject()).valid);
}

// GHS with the symmetry point / protection points OUTSIDE the window: the
// slope function is positive everywhere, so the normalized cumulative curve
// stays a valid monotone transfer 0 -> 1. SP below the window gives the
// log-like, steepest-at-the-left shape a clipped-away mode needs.
NS_TEST(ghs_symmetry_point_outside_window) {
    const int N = 1024;
    ChannelStretch cs;                               // identity window
    for (double sp : { -0.4, -0.05, 1.3, 1.9 }) {
        GHSParams g; g.D = 2.0; g.b = 4.0; g.SP = sp; g.LP = std::min(0.0, sp - 0.1); g.HP = std::max(1.0, sp + 0.1);
        const auto lut = buildLut(StretchFn::GHS, cs, g, N);
        NS_CHECK(lut.size() == std::size_t(N));
        NS_CHECK(std::abs(lut[0]) < 1e-6);
        NS_CHECK(std::abs(lut[N - 1] - 1.0f) < 1e-6);
        bool mono = true;
        for (int i = 1; i < N; ++i) mono = mono && lut[i] >= lut[i - 1];
        NS_CHECK(mono);
        // SP left of the window: concave (slope decreasing) -> first half rises
        // more than the second; SP right of it: the reverse.
        const float firstHalf = lut[N / 2] - lut[0], secondHalf = lut[N - 1] - lut[N / 2];
        if (sp < 0) NS_CHECK(firstHalf > secondHalf);
        else        NS_CHECK(secondHalf > firstHalf);
    }
}

// Out-of-range window handles are well-defined for the renderer's affine
// windowing: black below the data minimum lifts the floor (a value at the
// minimum no longer maps to 0), white above the maximum leaves headroom.
NS_TEST(window_handles_outside_data_range) {
    ChannelStretch cs; cs.black = -0.5; cs.mid = 0.5; cs.white = 1.5;
    // windowCoord: t = (u - black)/(white - black) for u normalized on [lo,hi]
    const double lo = 0.0, hi = 1.0;
    const double tMin = windowCoord(0.0, lo, hi, cs);   // data minimum
    const double tMax = windowCoord(1.0, lo, hi, cs);   // data maximum
    NS_CHECK(std::abs(tMin - 0.25) < 1e-12);
    NS_CHECK(std::abs(tMax - 0.75) < 1e-12);
}

// The stretch fit searches the full handle domain: a target rendered with a
// black point BELOW the data minimum (lifted floor) and a white ABOVE the
// maximum (headroom) must be matched — the old [0,1] bounds could not
// reach either end (source min always displayed as 0, max always as 1).
NS_TEST(fit_channel_stretch_reaches_outside_data_range) {
    const std::size_t n = 20000;
    std::vector<float> raw(n), disp(n);
    ChannelStretch truth; truth.black = -0.3; truth.mid = 0.45; truth.white = 1.4;
    const double m = (truth.mid - truth.black) / (truth.white - truth.black);
    for (std::size_t i = 0; i < n; ++i) {
        raw[i] = float(i) / (n - 1);
        disp[i] = float(mtf(windowCoord(raw[i], 0.0, 1.0, truth), m));
    }
    NS_CHECK(disp[0] > 0.05);                // lifted floor: the minimum is not black
    NS_CHECK(disp[n - 1] < 0.95);            // headroom: the maximum is not white
    ChannelStretch fit;
    const double rmse = fitChannelStretch(raw.data(), disp.data(), n, 1, 0.0, 1.0, fit);
    NS_CHECK(rmse < 5e-3);
    NS_CHECK(fit.black < 0.0);               // it used the room below the data
    NS_CHECK(fit.white > 1.0);               // ... and above it
    const double fm = (fit.mid - fit.black) / std::max(1e-6, fit.white - fit.black);
    double worst = 0;
    for (std::size_t i = 0; i < n; i += 97)
        worst = std::max(worst, std::fabs(mtf(windowCoord(raw[i], 0.0, 1.0, fit), fm) - disp[i]));
    NS_CHECK(worst < 0.02);
    NS_CHECK(std::fabs(mtf(windowCoord(0.0f, 0.0, 1.0, fit), fm) - disp[0]) < 0.02);   // the floor itself
}

int main() { return nstest::runAll(); }

// DisplayFunction import regime: a PI STF whose white point sits ~80x beyond
// the data maximum rebases onto the data range in closed form — the rebased
// curve equals the original restricted to the data, rescaled by 1/f(1),
// EXACTLY (the renormalized Mobius stays in the MTF family).
NS_TEST(display_function_rebase_far_white) {
    ChannelStretch pi;                              // normalized window coords
    pi.black = 0.0289;
    pi.white = 77.5;                                // raw 1.0 on 0.0129-max data
    pi.mid   = pi.black + 0.00132 * (pi.white - pi.black);
    const double mPi = (pi.mid - pi.black) / (pi.white - pi.black);
    const double k = (1.0 - pi.black) / (pi.white - pi.black);
    double f1 = 0.0;
    const ChannelStretch rb = rebaseFarWhite(pi, &f1);
    NS_CHECK(rb.white <= 1.0 + 1e-9);               // controls stay on the plot
    NS_CHECK(rb.black == pi.black && rb.mid > rb.black && rb.mid < rb.white);
    NS_CHECK(f1 > 0.5 && f1 < 1.0);                 // original topped out below 1
    const double m2 = (rb.mid - rb.black) / (rb.white - rb.black);
    double worst = 0.0;
    for (int i = 0; i <= 2000; ++i) {
        const double t = double(i) / 2000;          // data-range coordinate
        const double target = mtf(k * t, mPi) / f1; // renormalized original
        const double ours = mtf(t, m2);
        worst = std::max(worst, std::abs(ours - target));
    }
    NS_CHECK(worst < 1e-9);                         // exact, not approximate
}

// Common-scale rebase across channels: rescaling every channel by the SAME
// S preserves inter-channel display ratios exactly (SPCC balance survives),
// while each rebased curve stays exactly in the MTF family.
NS_TEST(display_function_rebase_common_scale) {
    ChannelStretch r, g;                            // per-channel normalized
    r.black = 0.03; r.white = 77.5;  r.mid = r.black + 0.0013 * (r.white - r.black);
    g.black = 0.10; g.white = 300.0; g.mid = g.black + 0.0009 * (g.white - g.black);
    const double S = std::max(farWhiteEndpoint(r), farWhiteEndpoint(g));
    const ChannelStretch r2 = rebaseFarWhiteTo(r, S);
    const ChannelStretch g2 = rebaseFarWhiteTo(g, S);
    const double mR = (r.mid - r.black) / (r.white - r.black);
    const double mG = (g.mid - g.black) / (g.white - g.black);
    const double mR2 = (r2.mid - r2.black) / (r2.white - r2.black);
    const double mG2 = (g2.mid - g2.black) / (g2.white - g2.black);
    double worst = 0.0;
    for (int i = 1; i < 2000; ++i) {
        const double t = double(i) / 2000;          // shared position inside each NEW window
        const double xR = r2.black + t * (r2.white - r2.black);   // channel-normalized value
        const double xG = g2.black + t * (g2.white - g2.black);
        const double origR = mtf((xR - r.black) / (r.white - r.black), mR);
        const double origG = mtf((xG - g.black) / (g.white - g.black), mG);
        const double newR = mtf(t, mR2), newG = mtf(t, mG2);
        worst = std::max(worst, std::abs(newR * S - origR));      // same S for both:
        worst = std::max(worst, std::abs(newG * S - origG));      // ratios preserved
    }
    NS_CHECK(worst < 1e-6);
    NS_CHECK(r2.white <= 1.0 + 1e-6);               // brightest channel: at its data max
}

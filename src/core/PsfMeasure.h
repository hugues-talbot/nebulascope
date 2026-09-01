#pragma once
//
// PsfMeasure — stellar PSF measurement: detect isolated stars and fit each
// with an ELLIPTICAL MOFFAT profile in linear space (Moffat rather than
// Gaussian because real seeing profiles carry wings a Gaussian misfits,
// biasing FWHM low). The aggregate — median FWHM along the major/minor axes,
// eccentricity and its position angle, the Moffat beta, and a 3×4 field map —
// separates the classic failure modes: a uniform elongation axis across the
// field is one-axis drift (guiding/flexure); an axis that rotates toward the
// corners is optics (tilt, coma, collimation).
//
// The method is the in-app port of the PSF study's star_fwhm instrument
// (docs/PSF-STUDY.md), validated there within 3 % of PixInsight's
// FWHMEccentricity on ~2 700 stars.
//
#include "core/ImageData.h"
#include <atomic>
#include <vector>

namespace astro {

struct PsfStar {
    double x = 0, y = 0;               // fitted centre, image px
    double fwhmMaj = 0, fwhmMin = 0;   // px
    double paDeg = 0;                  // major-axis angle from +x, (-90, 90]
    double ecc = 0;                    // sqrt(1 - (min/maj)^2)
    double beta = 0;                   // Moffat exponent
    double amp = 0;                    // fitted amplitude (sorting/highlight)
};

struct PsfZone {
    int    nStars = 0;
    double fwhmGeo = 0, ecc = 0, paDeg = 0;   // zone medians
};

struct PsfChannelReport {
    int nDetected = 0;                 // isolated candidate peaks
    int nFitted = 0;                   // fits passing the quality gate
    double fwhmMaj = 0, fwhmMin = 0, fwhmGeo = 0;   // medians, px
    double ecc = 0, paDeg = 0, beta = 0;
    PsfZone zone[3][4];                // rows × cols across the frame
    std::vector<PsfStar> stars;       // good fits, brightest first
};

// Measure one channel. Deterministic; runs at full resolution. Safe to call
// from a worker thread (touches only the given plane). The optional atomic
// counters report progress across threads: `total` is increased by the
// number of stars this call will fit (known once detection finishes),
// `done` ticks up as each fit completes — both cumulative, so one pair can
// aggregate several channels.
PsfChannelReport measurePsf(const ImageData& img, int channel,
                            std::atomic<int>* done = nullptr,
                            std::atomic<int>* total = nullptr);

// Fit a single 2N+1-square cutout (row-major, side `side`) with an elliptical
// Moffat; returns false when the fit fails the quality gate. Exposed for
// tests and for future callers that bring their own detection.
bool fitMoffatCutout(const float* cut, int side, double satLevel, PsfStar& out);

} // namespace astro

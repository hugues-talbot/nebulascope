#pragma once
//
// StarRemove — analytic star removal with a stated construction: the first
// half of the original MCS decomposition (Magain, Courbin & Sohy 1998), where
// the point sources are fitted and taken out and the smooth component kept.
//
// Per star, brightest first, on the running residual image:
//   1. fit an elliptical Moffat of the channel's MEASURED median shape
//      (Measure PSF) with free position, amplitude, local background and
//      gradient, plus its own width scale and exponent when bright enough
//      to set them, on the unclipped pixels of a brightness-scaled disc.
//      A clipped core contributes nothing: the flux comes from the wings,
//      as PSF photometry does with saturated pixels masked;
//   2. subtract the model out to where it drops under the noise;
//   3. inside the CORE — where the model still exceeds `coreSigma` noise
//      sigmas, so that a few-percent fit residual would show, GROWN until
//      the ring outside it is quiet (a bright star's wings are rounder
//      than its core; the fill covers what the model could not explain) —
//      replace the pixels by the harmonic continuation of the surrounding
//      ring (the Poisson integral on a round disc: exact, no iteration, no
//      geometry to print), plus noise at the measured sigma so the
//      statistics stay uniform. Faint stars need no core: subtraction
//      alone lands below the noise.
// The tool certifies its own work: stars removed, cores inpainted, and per
// star the rms residual left in the ring around its core relative to a
// control ring further out (nebular texture sits in both, the star's
// residual only in the inner one) — flagging what a Moffat cannot describe
// (ghosts, halos, spikes).
//
// What sits under an inpainted core is a smooth guess, as it is for any
// starless tool. For the deconvolution that follows (core/Deconvolve.h,
// starless path) only smoothness and low contrast matter, and the whole
// chain is now a stated operation rather than learned weights.
//
#include "core/ImageData.h"
#include "core/PsfMeasure.h"
#include <atomic>
#include <vector>

namespace astro {

struct StarRemoveOptions {
    double detectSigma = 5.0;    // detection threshold over the noise (high-passed)
    double coreSigma = 100.0;    // inpaint where the model exceeds this many sigmas
    bool   fillNoise = true;     // inpainted cores get noise at the measured sigma
    unsigned seed = 0x5EED;      // for that noise (deterministic)
};

struct RemovedStar {
    double x = 0, y = 0;         // fitted centre, image px
    double amp = 0;              // fitted amplitude over the local background
    double scale = 1;            // width scale against the channel median shape
    double coreRadius = 0;       // inpainted disc radius, px (0 = subtraction only), after growth
    double beta = 2.5;           // Moffat exponent (its own when bright enough to set it)
    double residualSigma = 0;    // what the ring around the core still holds, relative to a control
                                 // ring further out: the larger of its scatter ratio and its coherent
                                 // offset in noise sigmas (1 = nothing left)
    bool   clipped = false;      // had saturated pixels (flux from the wings)
};

struct StarRemoveResult {
    std::vector<float> starless;  // the plane with the stars removed
    double noiseSigma = 0;
    int nCandidates = 0, nRemoved = 0, nInpainted = 0, nClipped = 0, nFlagged = 0;
    double maxResidualSigma = 0;
    std::vector<RemovedStar> stars;   // brightest first
};

// Ring residual (RemovedStar::residualSigma) beyond this, after the disc
// reached its cap, flags a star as not fully removed.
constexpr double kStarRemoveFlagSigma = 3.0;

// Remove the stars of one plane. `shape` is the channel's measured median
// PSF (measurePsf); with fewer than 5 fitted stars a circular default is
// used and the per-star width scale does the adapting. NaNs pass through.
StarRemoveResult removeStars(const float* plane, int w, int h,
                             const PsfChannelReport& shape,
                             const StarRemoveOptions& opt = {},
                             std::atomic<int>* done = nullptr,
                             std::atomic<int>* total = nullptr);

// Whole image (Float32 planes): one shape per channel (the last is reused
// beyond shapes.size()); per-channel reports out through `results`.
ImageData removeStarsImage(const ImageData& img,
                           const std::vector<PsfChannelReport>& shapes,
                           const StarRemoveOptions& opt = {},
                           std::vector<StarRemoveResult>* results = nullptr,
                           std::atomic<int>* done = nullptr,
                           std::atomic<int>* total = nullptr);

} // namespace astro

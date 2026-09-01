#pragma once
//
// Deconvolve — calibrated deconvolution with a stated model, the in-app port
// of the PSF study's full_deconv instrument (docs/PSF-STUDY.md).
//
//   * Kernel per channel: the measured ELLIPTICAL MOFFAT stellar PSF
//     (from Tools > Measure PSF). Measured PSF in.
//   * Target: a declared CIRCULAR GAUSSIAN — the field elongation is
//     corrected by construction. Declared PSF out.
//   * Solver: the MCS one-filter transform (Magain, Courbin & Sohy 1998):
//         X = Y . OTF_t . conj(OTF_k) / (|OTF_k|^2 + lambda)
//     — deconvolve by the measured OTF and reconvolve by the target in a
//     single linear pass. The partial kernel k (/) t is never formed: its
//     Fourier division explodes (Moffat spectra decay polynomially, the
//     Gaussian target's super-exponentially); the target's own decay tames
//     the high band here instead.
//   * Saturated-core protection: the brightest cores are nonlinear — never
//     truth (*) PSF — so those pixels retain the INPUT values, feathered.
//
// Every output pixel is a stated linear functional of the input (outside the
// declared protected fraction). The delivered PSF is auditable: re-measure
// the result's stars and compare with the target.
//
#include "core/ImageData.h"
#include <atomic>
#include <vector>

namespace astro {

struct DeconvChannelPsf {
    double fwhmMajPx = 0, fwhmMinPx = 0;   // measured stellar PSF
    double paDeg = 0;
    double beta = 2.5;
};

struct DeconvOptions {
    double targetFwhmPx = 0;               // declared circular Gaussian FWHM
    double lambda = 1e-3;                  // MCS regularization (kernels unit-sum)
    bool   protectCores = true;            // saturated cores keep input pixels
    double protectPercentile = 99.995;     // per-channel core threshold
    // Starlet-RED noise prior (Regularization by Denoising, Romano, Elad &
    // Milanfar 2017, fixed-point variant). 0 iterations = pure MCS. With
    // iterations > 0, each step denoises the estimate with a starlet
    // (à-trous B3-spline) soft-threshold and re-solves the Fourier-diagonal
    // data-consistency filter with the denoised image as prior mean:
    //   X = (conj(OTF_k)·Y + beta·FFT(D(x))) / (|OTF_k|^2 + beta)
    // The declared target is applied by one final convolution — the partial
    // kernel is still never formed, and the delivered-PSF audit still runs
    // on the product. beta plays lambda's role and obeys the same
    // contract-first ladder (selectRedWeight).
    int    redIterations = 0;
    double redPriorWeight = 1e-2;          // beta; resolve 'auto' before calling
    int    redLevels = 5;                  // starlet detail scales
    double redThresholdK = 3.0;            // soft threshold, in noise sigmas
};

// Progress steps deconvolveChannel will tick for these options (4 for pure
// MCS; 4 + one per RED iteration otherwise).
inline int deconvSteps(const DeconvOptions& opt) {
    return 4 + std::max(0, opt.redIterations);
}

// Deconvolve one channel plane in place-shape (returns a new plane). NaNs
// pass through untouched. `stepsDone` ticks deconvSteps(opt) coarse steps.
std::vector<float> deconvolveChannel(const float* plane, int w, int h,
                                     const DeconvChannelPsf& psf,
                                     const DeconvOptions& opt,
                                     std::atomic<int>* stepsDone = nullptr);

// Whole image (Float32 planes, like measurePsf), per-channel PSFs (the last
// entry is reused beyond psfs.size()). Progress: deconvSteps(opt) per channel.
ImageData deconvolveToTarget(const ImageData& img,
                             const std::vector<DeconvChannelPsf>& psfs,
                             const DeconvOptions& opt,
                             std::atomic<int>* stepsDone = nullptr);

// The elliptical Moffat kernel itself (unit sum, side = odd(max(33, 8*fwhmMaj))),
// exposed for tests.
std::vector<float> moffatKernel(const DeconvChannelPsf& psf, int& sideOut);

// Contract-first regularization: walk a descending lambda ladder
// {3e-3 ... 3e-5} and return the LARGEST value whose DELIVERED stellar FWHM
// — the deconvolved crop re-measured by PsfMeasure — honours the target
// within tolFrac. More regularization is safer, so the first rung that
// keeps the promise wins; if none does, the smallest rung is returned (and
// the caller's delivered-PSF report will say so). Runs on a centred crop of
// at most cropSize for speed. Returns opt-style lambda; falls back to 1e-3
// when the crop yields too few stars to measure.
double selectLambda(const ImageData& img, int channel,
                    const DeconvChannelPsf& psf, double targetFwhmPx,
                    double tolFrac = 0.05, int cropSize = 1536);

// Contract-first prior weight for the starlet-RED mode: same descending
// ladder discipline as selectLambda, but the delivered FWHM is measured on
// a RED run (with `iterations` steps) and the winner is the STRONGEST prior
// honouring the target. Smaller crop: RED runs cost iterations x FFTs.
double selectRedWeight(const ImageData& img, int channel,
                       const DeconvChannelPsf& psf, double targetFwhmPx,
                       int iterations, double tolFrac = 0.05,
                       int cropSize = 1024);

// Starlet (undecimated à-trous, B3-spline) soft-threshold denoiser — the
// RED prior, exposed for tests. Thresholds are k x the per-level noise
// sigma, estimated per detail scale by MAD. k = 0 reproduces the input
// exactly (the transform is a tight partition). Finite input only.
std::vector<float> starletDenoise(const float* plane, int w, int h,
                                  int levels, double k);

} // namespace astro

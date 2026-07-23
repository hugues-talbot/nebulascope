#pragma once
//
// ColorTransport — transfer the colour distribution of a reference image onto a
// source image via sliced optimal transport (iterative distribution transfer,
// Pitié et al.). Works on display-ready [0,1] RGB data: each iteration picks a
// random 3-D rotation, matches the three projected 1-D marginals by quantile
// mapping, and rotates the correction back. Distribution-based — the images
// need not be aligned or the same size; alignment only sharpens the intent.
//
// Mono pair: plain 1-D quantile matching (the 1-channel special case).
//
#include "core/ImageData.h"
#include <string>

namespace astro {

struct ColorTransportResult {
    bool        ok = false;
    std::string error;
    ImageData   image;          // Float32, same geometry/channels as the source
};

// src/ref: Float32, display-ready [0,1]; 3-channel RGB (or both 1-channel).
// iterations: sliced-OT sweeps (10-20 typical). maxSamples: per-image sample
// cap for estimating the quantile maps (the mapping is applied to every pixel).
ColorTransportResult transportColors(const ImageData& src, const ImageData& ref,
                                     int iterations = 15,
                                     std::size_t maxSamples = 200000);

} // namespace astro

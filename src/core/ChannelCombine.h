#pragma once
//
// ChannelCombine — merge several mono frames into one linear RGB image via a
// weighted linear combination (pixel math), the SHO/HOO/LRGB workflow.
//
// The engine is Qt-free and domain-agnostic: callers pass float planes that are
// EITHER raw-linear or already display-stretched (the dialog decides), all the
// same width/height. Each colour plane contributes to R, G and B through its own
// weights; an optional luminance plane is applied afterwards. Per-plane
// pre-normalization keeps wildly different narrowband scales comparable.
//
#include "core/ImageData.h"
#include <vector>
#include <string>

namespace astro {

enum class PreNorm {
    None,       // raw values × weights
    Median,     // divide by the plane's median (equalises background level)
    MinMax,     // (v-min)/(max-min) → [0,1]
    Pedestal    // subtract the median (background → 0), keep signal above it
};

enum class LumMode {
    None,
    Linear,     // add amount·L into R,G,B equally (fits the matrix model)
    Luminance   // ratio-preserving LRGB: RGB ×= L / luma(RGB)  (proper-ish colour)
};

// One colour-bearing input and its contribution to each output channel.
struct CombinePlane {
    const float* data = nullptr;
    double wR = 0.0, wG = 0.0, wB = 0.0;
};

struct CombineResult {
    bool        ok = false;
    std::string error;
    ImageData   image;      // Float32, 3-channel, RGB
};

// Combine `planes` (each w*h floats) into an RGB ImageData. `lum` (optional,
// w*h floats) is applied per `lm` with strength `lumAmount`. All pointers must
// reference buffers of exactly w*h samples.
CombineResult combineChannels(int w, int h,
                              const std::vector<CombinePlane>& planes,
                              PreNorm pn,
                              const float* lum, LumMode lm, double lumAmount);

} // namespace astro

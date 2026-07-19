#pragma once
//
// ImageStats — per-channel statistics used for auto-stretch and histogram
// scaling. Assumes the image is Float32 (the loader's promoteToFloat default).
//
#include "core/ImageData.h"
#include <vector>
#include <cstddef>

namespace astro {

struct ChannelStats {
    float min = 0.0f;
    float max = 1.0f;
    float median = 0.0f;
    float mad = 0.0f;   // median absolute deviation (robust spread)
    float p99 = 1.0f;   // 99th percentile — upper bound for the gentle default window
};

// Min/max are exact; median/MAD are estimated from up to `maxSamples` pixels.
std::vector<ChannelStats> computeStats(const ImageData& img, std::size_t maxSamples = 200000);

} // namespace astro

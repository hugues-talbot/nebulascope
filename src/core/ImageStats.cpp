#include "core/ImageStats.h"
#include <algorithm>
#include <cmath>

namespace astro {

static ChannelStats statForPlane(const float* p, std::size_t n, std::size_t maxSamples) {
    ChannelStats s;
    if (!n) return s;

    // Min/max over FINITE pixels only. NaN/Inf "blank" values are common in
    // calibrated FP32 frames; letting a NaN through here poisons the whole
    // display range and collapses the image to black.
    float mn = 0, mx = 0;
    bool any = false;
    for (std::size_t i = 0; i < n; ++i) {
        const float v = p[i];
        if (!std::isfinite(v)) continue;
        if (!any) { mn = mx = v; any = true; }
        else { if (v < mn) mn = v; if (v > mx) mx = v; }
    }
    if (!any) return s;            // all blank → leave zeros
    s.min = mn;
    s.max = mx;
    s.p99 = mx;                    // fallback if sampling below comes up empty

    const std::size_t step = n > maxSamples ? n / maxSamples : 1;
    std::vector<float> samp;
    samp.reserve(n / step + 1);
    for (std::size_t i = 0; i < n; i += step)
        if (std::isfinite(p[i])) samp.push_back(p[i]);
    if (samp.empty()) return s;

    const std::size_t mid = samp.size() / 2;
    std::nth_element(samp.begin(), samp.begin() + mid, samp.end());
    const float med = samp[mid];
    s.median = med;

    // 99th percentile (before samp is repurposed for the MAD below).
    const std::size_t i99 = std::min(samp.size() - 1, std::size_t(0.99 * double(samp.size() - 1) + 0.5));
    std::nth_element(samp.begin(), samp.begin() + i99, samp.end());
    s.p99 = samp[i99];

    for (auto& v : samp) v = std::fabs(v - med);
    std::nth_element(samp.begin(), samp.begin() + mid, samp.end());
    s.mad = samp[mid];
    return s;
}

std::vector<ChannelStats> computeStats(const ImageData& img, std::size_t maxSamples) {
    std::vector<ChannelStats> out;
    if (img.format() != SampleFormat::Float32) return out;   // load with promoteToFloat
    const std::size_t n = img.samplesPerChannel();
    for (int c = 0; c < img.channels(); ++c)
        out.push_back(statForPlane(img.plane<float>(c), n, maxSamples));
    return out;
}

} // namespace astro

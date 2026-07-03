#pragma once
//
// RenderCore — the Qt-free heart of the iOS proof-of-concept. It reuses the
// desktop Stretch + Colormap math verbatim and adds everything the desktop's
// Qt-dependent pieces used to provide, but with no framework dependency:
//
//   * FloatImage        — planar Float32 pixel container (mono or RGB)
//   * makeSampleGalaxy  — a synthetic edge-on galaxy so the app runs with no file
//   * computeStats      — per-channel min/max/median/MAD (finite pixels only)
//   * computeSTF        — auto-stretch (background → ~0.25 via MTF midtone)
//   * renderRGBA        — window + transfer + colormap → 8-bit RGBA buffer
//   * histogram         — log/linear frequency bins over the display window
//
// The Objective-C++ bridge (NebulaBridge.mm) is a thin shell over this.
//
#include <vector>
#include <cstdint>
#include <cstddef>
#include "Stretch.h"
#include "Colormap.h"

namespace nb {

struct FloatImage {
    int w = 0, h = 0, ch = 1;
    std::vector<float> data;                       // planar: plane c starts at c*w*h
    const float* plane(int c) const { return data.data() + std::size_t(c) * w * h; }
    float*       plane(int c)       { return data.data() + std::size_t(c) * w * h; }
    std::size_t  perChannel() const { return std::size_t(w) * h; }
};

struct Stats { float mn = 0, mx = 1, median = 0, mad = 0; };

// A synthetic 3-channel edge-on galaxy with a bright core, dust lane, faint
// halo and a power-law star field — enough dynamic range to exercise the
// stretch controls just like a real light frame.
FloatImage makeSampleGalaxy(int w, int h);

Stats computeStats(const FloatImage& img, int channel, std::size_t maxSamples = 150000);

// Astro auto-stretch (STF): fills the display range [lo,hi] and the black/mid/
// white points so the sky background lands near 0.25 of the display.
void computeSTF(const Stats& s, astro::ChannelStretch& cs, double& lo, double& hi);

// Window + transfer (+ colormap for mono) → tightly-packed RGBA8 (w*h*4).
std::vector<std::uint8_t> renderRGBA(
    const FloatImage& img, astro::StretchFn fn,
    const astro::ChannelStretch cs[3], const double lo[3], const double hi[3],
    const astro::GHSParams& ghs, astro::Colormap cmap, double splitT);

// Normalized [0,1] histogram of the display window for one channel.
std::vector<float> histogram(const FloatImage& img, int channel, int bins,
                             bool logScale, double lo, double hi);

} // namespace nb

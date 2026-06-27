#pragma once
//
// Colormap — false-colour lookup for single-channel (mono) images. A colormap
// maps a stretched display value in [0,1] to an RGB triplet. RGB images ignore
// it. Maps are defined by a handful of anchor colours and interpolated to a
// 256-entry table at build time.
//
#include <vector>
#include <cstdint>

namespace astro {

enum class Colormap { Gray, Heat, Viridis, Magma, Inferno, Cividis };

constexpr int kColormapCount = 6;

const char* colormapName(Colormap c);

// 256*3 interpolated RGB table (row-major: r,g,b,r,g,b, ...).
std::vector<std::uint8_t> buildColormapLut(Colormap c, int n = 256);

} // namespace astro

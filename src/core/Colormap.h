#pragma once
//
// Colormap — false-colour lookup for single-channel (mono) images. A base map
// turns a stretched display value in [0,1] into an RGB triplet; two orthogonal
// MODIFIERS reshape the input coordinate before lookup, so they compose with
// every base map:
//   * invert — reverse the ramp (0<->1). Gray+invert is a photo negative.
//   * split  — fold at a threshold: inverted ramp below it, normal above, so
//              faint background reads with reversed contrast while sources stay
//              positive. Works on any base map (Gray+split = the classic view).
// RGB images ignore all of this.
//
#include <vector>
#include <cstdint>

namespace astro {

// Gray..Cividis: NebulaScope's own maps. Ds9*: SAOImage DS9's classic
// palettes, reproduced from DS9's reference control points so renditions
// match DS9 exactly (a/b/bb/he/cool/rainbow/standard are piecewise-linear;
// i8/aips0/sls are stepped colour tables, as in DS9).
enum class Colormap { Gray, Heat, Viridis, Magma, Inferno, Cividis,
                      Ds9A, Ds9B, Ds9BB, Ds9HE, Ds9Cool, Ds9Rainbow,
                      Ds9Standard, Ds9I8, Ds9AIPS0, Ds9SLS };

constexpr int kColormapCount = 16;

const char* colormapName(Colormap c);

// Modifiers applied to the input coordinate t before the base-map lookup.
struct ColormapMods {
    bool   invert = false;
    bool   split  = false;
    double splitT = 0.25;   // break point (0..1) for `split`
};

// True when the map is anything other than plain Gray (i.e. the renderer must
// take the colour-LUT path rather than the fast grayscale path).
inline bool colormapActive(Colormap c, const ColormapMods& m) {
    return c != Colormap::Gray || m.invert || m.split;
}

// n*3 interpolated RGB table (row-major: r,g,b,r,g,b, ...), base map + modifiers.
std::vector<std::uint8_t> buildColormapLut(Colormap c, const ColormapMods& mods, int n = 256);

} // namespace astro

#include "Colormap.h"
#include <array>
#include <algorithm>

namespace astro {

// Each map = 9 anchor colours at t = 0, 1/8, ... , 1. The perceptual maps
// (Viridis/Magma/Inferno/Cividis) use sampled values from the originals; good
// enough for display without embedding full 256-entry tables.
struct Anchors { std::array<std::array<int,3>,9> c; };

static const Anchors& anchorsFor(Colormap m) {
    static const Anchors gray = {{{ {{0,0,0}},{{32,32,32}},{{64,64,64}},{{96,96,96}},
        {{128,128,128}},{{160,160,160}},{{192,192,192}},{{224,224,224}},{{255,255,255}} }}};
    static const Anchors heat = {{{ {{0,0,0}},{{60,0,0}},{{120,0,0}},{{200,30,0}},
        {{255,90,0}},{{255,150,0}},{{255,210,40}},{{255,245,140}},{{255,255,255}} }}};
    static const Anchors viridis = {{{ {{68,1,84}},{{70,50,127}},{{54,92,141}},{{39,127,142}},
        {{31,161,135}},{{74,194,109}},{{159,218,58}},{{220,227,42}},{{253,231,37}} }}};
    static const Anchors magma = {{{ {{0,0,4}},{{28,16,68}},{{79,18,123}},{{129,37,129}},
        {{181,54,122}},{{229,80,100}},{{251,135,97}},{{254,194,135}},{{252,253,191}} }}};
    static const Anchors inferno = {{{ {{0,0,4}},{{31,12,72}},{{85,15,109}},{{136,34,106}},
        {{186,54,85}},{{227,89,51}},{{249,140,10}},{{249,201,50}},{{252,255,164}} }}};
    static const Anchors cividis = {{{ {{0,32,76}},{{0,42,102}},{{47,68,105}},{{86,91,105}},
        {{124,114,106}},{{165,139,99}},{{208,166,84}},{{247,196,64}},{{255,233,69}} }}};
    switch (m) {
        case Colormap::Heat:    return heat;
        case Colormap::Viridis: return viridis;
        case Colormap::Magma:   return magma;
        case Colormap::Inferno: return inferno;
        case Colormap::Cividis: return cividis;
        case Colormap::Gray:
        default:                return gray;
    }
}

const char* colormapName(Colormap c) {
    switch (c) {
        case Colormap::Gray:     return "Gray";
        case Colormap::Heat:     return "Heat";
        case Colormap::Viridis:  return "Viridis";
        case Colormap::Magma:    return "Magma";
        case Colormap::Inferno:  return "Inferno";
        case Colormap::Cividis:  return "Cividis";
        case Colormap::Inverted: return "Gray (inv)";
        case Colormap::Split:    return "Split";
    }
    return "Gray";
}

std::vector<std::uint8_t> buildColormapLut(Colormap c, int n, double splitT) {
    std::vector<std::uint8_t> lut(std::size_t(n) * 3);

    // Fully-inverted grayscale: 0 -> white, 1 -> black.
    if (c == Colormap::Inverted) {
        for (int i = 0; i < n; ++i) {
            const double t = (n == 1) ? 0.0 : double(i) / (n - 1);
            const std::uint8_t g = std::uint8_t(std::clamp(int((1.0 - t) * 255 + 0.5), 0, 255));
            lut[i*3+0] = lut[i*3+1] = lut[i*3+2] = g;
        }
        return lut;
    }

    // Split grayscale: above the break, normal (break=black -> 1=white); below
    // the break, inverted (0=white -> break=black). The break sits at the sky
    // level, so faint background structure is rendered with an inverted ramp
    // while sources above it read as a normal positive image.
    if (c == Colormap::Split) {
        const double T = std::clamp(splitT, 0.0, 1.0);
        for (int i = 0; i < n; ++i) {
            const double t = (n == 1) ? 0.0 : double(i) / (n - 1);
            double lum;
            if (t >= T) lum = (T >= 1.0) ? 0.0 : (t - T) / (1.0 - T);
            else        lum = (T <= 0.0) ? 0.0 : (T - t) / T;
            const std::uint8_t g = std::uint8_t(std::clamp(int(lum * 255 + 0.5), 0, 255));
            lut[i*3+0] = lut[i*3+1] = lut[i*3+2] = g;
        }
        return lut;
    }

    const Anchors& a = anchorsFor(c);
    for (int i = 0; i < n; ++i) {
        const double t = (n == 1) ? 0.0 : double(i) / (n - 1);   // [0,1]
        const double seg = t * 8.0;                              // 8 intervals
        int s = int(seg);
        if (s > 7) s = 7;
        const double f = seg - s;
        for (int k = 0; k < 3; ++k) {
            const double v = a.c[s][k] + (a.c[s + 1][k] - a.c[s][k]) * f;
            lut[i * 3 + k] = std::uint8_t(std::clamp(int(v + 0.5), 0, 255));
        }
    }
    return lut;
}

} // namespace astro

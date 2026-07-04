#include "render/DisplayRenderer.h"
#include "core/Stretch.h"
#include "core/Colormap.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace astro {

// Deterministic value-noise hash → float in [0,1). Stable per (pixel,channel) so
// re-rendering the same frame gives an identical pattern (no shimmer).
static inline std::uint32_t hashU(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x;
}
// Triangular-PDF dither in [-1,1): the standard shape for breaking up banding
// when quantising a smooth signal to 8 bits, with minimal added variance.
static inline float triDither(std::size_t i, int c) {
    const std::uint32_t h1 = hashU(std::uint32_t(i) * 3u + std::uint32_t(c) + 0x9e3779b9u);
    const std::uint32_t h2 = hashU(std::uint32_t(i >> 11) * 3u + std::uint32_t(c) * 2u + 0x85ebca6bu);
    const float r1 = (h1 >> 8) * (1.0f / 16777216.0f);
    const float r2 = (h2 >> 8) * (1.0f / 16777216.0f);
    return r1 + r2 - 1.0f;
}
// Dither amplitude, in 8-bit LSBs. ~0.6 LSB breaks bands while staying visually
// clean; the combined result of high-SNR data no longer posterizes.
static constexpr float kDither = 0.6f;

QImage DisplayRenderer::render(const ImageData& img, const StretchModel& model) {
    const int w = img.width(), h = img.height();
    if (w <= 0 || h <= 0) return QImage();
    const int ch = img.channels();
    const int N = 4096;

    // Per-channel transfer LUTs. GHS is a shared master curve.
    std::vector<float> lut[3];
    if (model.fn() == StretchFn::GHS) {
        lut[0] = buildLut(StretchFn::GHS, model.channel(0), model.ghs(), N);
        lut[1] = lut[0];
        lut[2] = lut[0];
    } else {
        for (int c = 0; c < 3; ++c)
            lut[c] = buildLut(model.fn(), model.channel(c), model.ghs(), N);
    }

    // Stretched display value in [0,1] for channel c (window + transfer).
    auto mapNorm = [&](int c, float v) -> float {
        if (!std::isfinite(v)) return 0.0f;              // NaN/Inf blanks → black
        const int ci = (ch == 1) ? 0 : c;
        // Window in float, then index the shape LUT by the windowed coordinate:
        // full LUT + output resolution now span the [black,white] window.
        const double t = windowCoord(v, model.lo(ci), model.hi(ci), model.channel(ci));
        int idx = int(t * (N - 1) + 0.5);
        idx = idx < 0 ? 0 : (idx > N - 1 ? N - 1 : idx);
        return lut[ci][idx];
    };
    // Quantise to 8 bits with sub-LSB triangular dither so smooth gradients in
    // high-SNR data (e.g. channel combines) don't posterize on the 8-bit display.
    auto to8 = [&](float y, std::size_t i, int c) -> uchar {
        const int o = int(y * 255.0f + 0.5f + triDither(i, c) * kDither);
        return uchar(o < 0 ? 0 : (o > 255 ? 255 : o));
    };

    QImage out(w, h, QImage::Format_RGB888);
    const float* p0 = img.plane<float>(0);
    const float* p1 = ch >= 3 ? img.plane<float>(1) : p0;
    const float* p2 = ch >= 3 ? img.plane<float>(2) : p0;

    // Mono + a non-Gray colormap: stretch once, then look up false colour.
    if (ch == 1 && model.colormap() != Colormap::Gray) {
        const int M = 4096;                              // colormap resolution (smooth gradient)
        const std::vector<std::uint8_t> cmap = buildColormapLut(model.colormap(), M, model.splitThreshold());
        const auto& l = lut[0];
        for (int y = 0; y < h; ++y) {
            uchar* row = out.scanLine(y);
            const std::size_t off = std::size_t(y) * w;
            for (int x = 0; x < w; ++x) {
                const float v = p0[off + x];
                int ci;
                if (!std::isfinite(v)) { ci = 0; }
                else {
                    const double t = windowCoord(v, model.lo(0), model.hi(0), model.channel(0));
                    int idx = int(t * (N - 1) + 0.5);
                    idx = idx < 0 ? 0 : (idx > N - 1 ? N - 1 : idx);
                    // Dither the colormap index so the false-colour gradient is smooth.
                    float ceff = l[idx] * (M - 1) + triDither(off + x, 0) * kDither;
                    ci = int(ceff + 0.5f);
                    ci = ci < 0 ? 0 : (ci > M - 1 ? M - 1 : ci);
                }
                row[x * 3 + 0] = cmap[ci * 3 + 0];
                row[x * 3 + 1] = cmap[ci * 3 + 1];
                row[x * 3 + 2] = cmap[ci * 3 + 2];
            }
        }
        return out;
    }

    for (int y = 0; y < h; ++y) {
        uchar* row = out.scanLine(y);
        const std::size_t off = std::size_t(y) * w;
        for (int x = 0; x < w; ++x) {
            const std::size_t i = off + x;
            row[x * 3 + 0] = to8(mapNorm(0, p0[i]), i, 0);
            row[x * 3 + 1] = to8(mapNorm(1, p1[i]), i, 1);
            row[x * 3 + 2] = to8(mapNorm(2, p2[i]), i, 2);
        }
    }
    return out;
}

} // namespace astro

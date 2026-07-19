#include "render/DisplayRenderer.h"
#include "core/Stretch.h"
#include "core/Colormap.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

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

// Run fn(y0, y1) on row ranges across the hardware threads. The QImage buffer is
// freshly allocated and unshared, so rows can be written concurrently.
template <typename Fn>
static void parallelRows(int h, Fn&& fn) {
    unsigned nt = std::thread::hardware_concurrency();
    nt = std::min(nt ? nt : 1u, 8u);
    if (nt <= 1 || h < 64) { fn(0, h); return; }
    std::vector<std::thread> pool;
    pool.reserve(nt);
    const int chunk = (h + int(nt) - 1) / int(nt);
    for (unsigned k = 0; k < nt; ++k) {
        const int y0 = int(k) * chunk;
        const int y1 = std::min(h, y0 + chunk);
        if (y0 >= y1) break;
        pool.emplace_back([&fn, y0, y1] { fn(y0, y1); });
    }
    for (auto& t : pool) t.join();
}

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

    // Hoist the windowing out of the inner loop: windowCoord(v) is affine in v,
    //   t = ((v-lo)/(hi-lo) - black) / (white-black)  =  v*A + B  (then clamp),
    // so each pixel costs one multiply-add instead of two divisions.
    double A[3], B[3], inDith[3];
    for (int c = 0; c < 3; ++c) {
        const ChannelStretch cs = model.channel(c);
        const double range = std::max(1e-9, model.hi(c) - model.lo(c));
        const double denomW = std::max(1e-6, cs.white - cs.black);
        A[c] = 1.0 / (range * denomW);
        B[c] = -(model.lo(c) / range + cs.black) / denomW;
        // Input dither amplitude: ±½ of a 16-bit data quantum, expressed in
        // windowed-t units. Integer sensor data (ADU steps) is genuinely
        // quantised; when the display window is only tens of ADU wide, each step
        // spans several output levels and bands. Dithering the INPUT by half a
        // quantum breaks that staircase (the true flux lies within ±½ ADU of
        // the recorded integer); for continuous float data the added noise is a
        // 16-bit sub-quantum — invisible.
        inDith[c] = 0.5 / (65535.0 * denomW);
    }
    auto mapNorm = [&](int c, float v, std::size_t i) -> float {
        if (!std::isfinite(v)) return 0.0f;              // NaN/Inf blanks → black
        const int ci = (ch == 1) ? 0 : c;
        double t = double(v) * A[ci] + B[ci];
        t += double(triDither(i, ci + 3)) * inDith[ci];  // break data quantisation
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        return lut[ci][int(t * (N - 1) + 0.5)];
    };
    // Quantise to 8 bits with sub-LSB triangular dither so smooth gradients in
    // high-SNR data (e.g. channel combines) don't posterize on the 8-bit display.
    auto to8 = [&](float y, std::size_t i, int c) -> uchar {
        const int o = int(y * 255.0f + 0.5f + triDither(i, c) * kDither);
        return uchar(o < 0 ? 0 : (o > 255 ? 255 : o));
    };

    QImage out(w, h, QImage::Format_RGB888);
    uchar* base = out.bits();
    const qsizetype bpl = out.bytesPerLine();
    const float* p0 = img.plane<float>(0);
    const float* p1 = ch >= 3 ? img.plane<float>(1) : p0;
    const float* p2 = ch >= 3 ? img.plane<float>(2) : p0;

    // Mono + an active colormap (non-Gray base, or invert/split): stretch once,
    // then look up false colour.
    if (ch == 1 && colormapActive(model.colormap(), model.cmapMods())) {
        const int M = 4096;                              // colormap resolution (smooth gradient)
        const std::vector<std::uint8_t> cmap = buildColormapLut(model.colormap(), model.cmapMods(), M);
        const auto& l = lut[0];
        parallelRows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                uchar* row = base + qsizetype(y) * bpl;
                const std::size_t off = std::size_t(y) * w;
                for (int x = 0; x < w; ++x) {
                    const float v = p0[off + x];
                    int ci;
                    if (!std::isfinite(v)) { ci = 0; }
                    else {
                        double t = double(v) * A[0] + B[0];
                        t += double(triDither(off + x, 3)) * inDith[0];   // break data quantisation
                        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                        // Dither the colormap index so the false-colour gradient is smooth.
                        const float ceff = l[int(t * (N - 1) + 0.5)] * (M - 1) + triDither(off + x, 0) * kDither;
                        ci = int(ceff + 0.5f);
                        ci = ci < 0 ? 0 : (ci > M - 1 ? M - 1 : ci);
                    }
                    row[x * 3 + 0] = cmap[ci * 3 + 0];
                    row[x * 3 + 1] = cmap[ci * 3 + 1];
                    row[x * 3 + 2] = cmap[ci * 3 + 2];
                }
            }
        });
        return out;
    }

    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            uchar* row = base + qsizetype(y) * bpl;
            const std::size_t off = std::size_t(y) * w;
            for (int x = 0; x < w; ++x) {
                const std::size_t i = off + x;
                row[x * 3 + 0] = to8(mapNorm(0, p0[i], i), i, 0);
                row[x * 3 + 1] = to8(mapNorm(1, p1[i], i), i, 1);
                row[x * 3 + 2] = to8(mapNorm(2, p2[i], i), i, 2);
            }
        }
    });
    return out;
}

} // namespace astro

#include "render/DisplayRenderer.h"
#include "core/Stretch.h"
#include "core/Colormap.h"
#include <algorithm>
#include <cmath>

namespace astro {

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

    auto mapVal = [&](int c, float v) -> int {
        if (!std::isfinite(v)) return 0;                 // NaN/Inf blanks → black
        const int ci = (ch == 1) ? 0 : c;
        // Window in float, then index the shape LUT by the windowed coordinate:
        // full LUT + output resolution now span the [black,white] window.
        const double t = windowCoord(v, model.lo(ci), model.hi(ci), model.channel(ci));
        int idx = int(t * (N - 1) + 0.5);
        idx = idx < 0 ? 0 : (idx > N - 1 ? N - 1 : idx);
        const float y = lut[ci][idx];
        const int o = int(y * 255.0f + 0.5f);
        return o < 0 ? 0 : (o > 255 ? 255 : o);
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
                    ci = int(l[idx] * (M - 1) + 0.5f);
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
            row[x * 3 + 0] = uchar(mapVal(0, p0[i]));
            row[x * 3 + 1] = uchar(mapVal(1, p1[i]));
            row[x * 3 + 2] = uchar(mapVal(2, p2[i]));
        }
    }
    return out;
}

} // namespace astro

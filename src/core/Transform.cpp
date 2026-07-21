#include "core/Transform.h"
#include <cstring>
#include <cmath>
#include <limits>

namespace astro {

// All operations copy sample-by-sample using the byte width of one sample, so
// they are format-agnostic (UInt8 ... Float64) and channel-count agnostic.

ImageData rotate90(const ImageData& src, bool clockwise) {
    if (!src.isValid()) return src;
    const int w = src.width(), h = src.height(), ch = src.channels();
    const std::size_t bs = bytesPerSample(src.format());
    ImageData dst(h, w, ch, src.format(), src.colorSpace());   // dimensions swap

    const std::size_t srcPlane = src.samplesPerChannel();
    const std::size_t dstPlane = dst.samplesPerChannel();
    for (int c = 0; c < ch; ++c) {
        const std::uint8_t* sp = src.bytes().data() + std::size_t(c) * srcPlane * bs;
        std::uint8_t* dp = dst.bytes().data() + std::size_t(c) * dstPlane * bs;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int nx, ny;                                    // dst is h wide, w tall
                if (clockwise) { nx = h - 1 - y; ny = x; }
                else           { nx = y;         ny = w - 1 - x; }
                const std::size_t si = (std::size_t(y) * w + x) * bs;
                const std::size_t di = (std::size_t(ny) * h + nx) * bs;
                std::memcpy(dp + di, sp + si, bs);
            }
        }
    }
    return dst;
}

ImageData flipHorizontal(const ImageData& src) {
    if (!src.isValid()) return src;
    const int w = src.width(), h = src.height(), ch = src.channels();
    const std::size_t bs = bytesPerSample(src.format());
    ImageData dst(w, h, ch, src.format(), src.colorSpace());
    const std::size_t plane = src.samplesPerChannel();
    for (int c = 0; c < ch; ++c) {
        const std::uint8_t* sp = src.bytes().data() + std::size_t(c) * plane * bs;
        std::uint8_t* dp = dst.bytes().data() + std::size_t(c) * plane * bs;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                std::memcpy(dp + (std::size_t(y) * w + (w - 1 - x)) * bs,
                            sp + (std::size_t(y) * w + x) * bs, bs);
    }
    return dst;
}

ImageData flipVertical(const ImageData& src) {
    if (!src.isValid()) return src;
    const int w = src.width(), h = src.height(), ch = src.channels();
    const std::size_t bs = bytesPerSample(src.format());
    ImageData dst(w, h, ch, src.format(), src.colorSpace());
    const std::size_t plane = src.samplesPerChannel();
    const std::size_t rowBytes = std::size_t(w) * bs;
    for (int c = 0; c < ch; ++c) {
        const std::uint8_t* sp = src.bytes().data() + std::size_t(c) * plane * bs;
        std::uint8_t* dp = dst.bytes().data() + std::size_t(c) * plane * bs;
        for (int y = 0; y < h; ++y)
            std::memcpy(dp + std::size_t(h - 1 - y) * rowBytes,
                        sp + std::size_t(y) * rowBytes, rowBytes);
    }
    return dst;
}

ImageData rotateArbitrary(const ImageData& src, double angleDeg) {
    if (!src.isValid() || src.format() != SampleFormat::Float32) return src;
    const double th = angleDeg * M_PI / 180.0;
    const double c = std::cos(th), s = std::sin(th);
    const int w = src.width(), h = src.height(), ch = src.channels();
    const int nw = std::max(1, int(std::ceil(w * std::fabs(c) + h * std::fabs(s))));
    const int nh = std::max(1, int(std::ceil(w * std::fabs(s) + h * std::fabs(c))));
    ImageData dst(nw, nh, ch, src.format(), src.colorSpace());

    // Rotation about the canvas centre; pixel centres at integer coordinates.
    // Forward map (old -> new): p' = M(p - cOld) + cNew, M = [[c,s],[-s,c]]
    // (positive angle = visually CCW in y-down screen coords; matches rotate90).
    const double cox = (w - 1) / 2.0,   coy = (h - 1) / 2.0;
    const double cnx = (nw - 1) / 2.0,  cny = (nh - 1) / 2.0;
    const float nanv = std::numeric_limits<float>::quiet_NaN();

    for (int cc = 0; cc < ch; ++cc) {
        const float* sp = src.plane<float>(cc);
        float* dp = dst.plane<float>(cc);
        for (int y = 0; y < nh; ++y) {
            const double dy = y - cny;
            for (int x = 0; x < nw; ++x) {
                const double dx = x - cnx;
                // Inverse map (new -> old): p = M^T(q - cNew) + cOld.
                const double sx = c * dx - s * dy + cox;
                const double sy = s * dx + c * dy + coy;
                float out = nanv;
                const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
                if (x0 >= -1 && y0 >= -1 && x0 <= w - 1 && y0 <= h - 1) {
                    const double fx = sx - x0, fy = sy - y0;
                    // Bilinear over the finite neighbours, renormalizing the
                    // weights so edges and NaN blanks don't darken the result.
                    double acc = 0, wsum = 0;
                    const double wts[4] = { (1-fx)*(1-fy), fx*(1-fy), (1-fx)*fy, fx*fy };
                    const int xs[4] = { x0, x0+1, x0, x0+1 };
                    const int ys[4] = { y0, y0, y0+1, y0+1 };
                    for (int k = 0; k < 4; ++k) {
                        if (xs[k] < 0 || ys[k] < 0 || xs[k] >= w || ys[k] >= h) continue;
                        const float v = sp[std::size_t(ys[k]) * w + xs[k]];
                        if (!std::isfinite(v)) continue;
                        acc += wts[k] * v; wsum += wts[k];
                    }
                    if (wsum > 0.5) out = float(acc / wsum);
                }
                dp[std::size_t(y) * nw + x] = out;
            }
        }
    }
    return dst;
}

} // namespace astro

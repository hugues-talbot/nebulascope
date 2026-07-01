#include "core/Transform.h"
#include <cstring>

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

} // namespace astro

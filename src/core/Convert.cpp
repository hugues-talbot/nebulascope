#include "core/Convert.h"

namespace astro {
namespace {

template <typename T>
void copyAsFloat(const ImageData& s, float* out, double scale) {
    const T* in = reinterpret_cast<const T*>(s.bytes().data());
    const std::size_t n = s.sampleCount();
    if (scale == 1.0) {
        for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<float>(in[i]);
    } else {
        const double inv = 1.0 / scale;
        for (std::size_t i = 0; i < n; ++i)
            out[i] = static_cast<float>(static_cast<double>(in[i]) * inv);
    }
}

} // namespace

ImageData toFloat32(const ImageData& src, bool normalizeIntegers) {
    if (src.format() == SampleFormat::Float32) return src;

    ImageData dst(src.width(), src.height(), src.channels(),
                  SampleFormat::Float32, src.colorSpace());
    float* out = reinterpret_cast<float*>(dst.bytes().data());

    const double u8  = normalizeIntegers ? 255.0        : 1.0;
    const double u16 = normalizeIntegers ? 65535.0      : 1.0;
    const double u32 = normalizeIntegers ? 4294967295.0 : 1.0;

    switch (src.format()) {
        case SampleFormat::UInt8:   copyAsFloat<std::uint8_t> (src, out, u8);  break;
        case SampleFormat::UInt16:  copyAsFloat<std::uint16_t>(src, out, u16); break;
        case SampleFormat::UInt32:  copyAsFloat<std::uint32_t>(src, out, u32); break;
        case SampleFormat::Int16:   copyAsFloat<std::int16_t> (src, out, 1.0); break;
        case SampleFormat::Int32:   copyAsFloat<std::int32_t> (src, out, 1.0); break;
        case SampleFormat::Float64: copyAsFloat<double>       (src, out, 1.0); break;
        case SampleFormat::Float32: break; // handled above
    }
    return dst;
}

} // namespace astro

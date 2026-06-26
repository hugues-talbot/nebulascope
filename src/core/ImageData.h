#pragma once
//
// ImageData — the shared, format-agnostic pixel container.
//
// Pixels are kept in their NATIVE type (never down-converted to 8-bit for
// storage) because astronomical data spans a huge dynamic range. Channels are
// stored PLANAR: all samples of channel 0, then all of channel 1, ...  This
// matches both FITS image cubes (which are band-sequential) and XISF (which is
// natively planar), so a decoder can memcpy straight into the buffer.
//
#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>

namespace astro {

enum class SampleFormat { UInt8, Int16, UInt16, Int32, UInt32, Float32, Float64 };
enum class ColorSpace  { Gray, RGB };

inline std::size_t bytesPerSample(SampleFormat f) {
    switch (f) {
        case SampleFormat::UInt8:                        return 1;
        case SampleFormat::Int16:
        case SampleFormat::UInt16:                       return 2;
        case SampleFormat::Int32:
        case SampleFormat::UInt32:
        case SampleFormat::Float32:                      return 4;
        case SampleFormat::Float64:                      return 8;
    }
    return 1;
}

class ImageData {
public:
    ImageData() = default;
    ImageData(int width, int height, int channels, SampleFormat fmt, ColorSpace cs)
        : m_w(width), m_h(height), m_ch(channels), m_fmt(fmt), m_cs(cs),
          m_bytes(byteSize()) {}

    int  width()      const { return m_w; }
    int  height()     const { return m_h; }
    int  channels()   const { return m_ch; }
    SampleFormat format()     const { return m_fmt; }
    ColorSpace   colorSpace() const { return m_cs; }
    bool isValid()    const { return m_w > 0 && m_h > 0 && m_ch > 0; }

    std::size_t samplesPerChannel() const { return std::size_t(m_w) * std::size_t(m_h); }
    std::size_t sampleCount()       const { return samplesPerChannel() * std::size_t(m_ch); }
    std::size_t byteSize()          const { return sampleCount() * bytesPerSample(m_fmt); }

    std::vector<std::uint8_t>&       bytes()       { return m_bytes; }
    const std::vector<std::uint8_t>& bytes() const { return m_bytes; }

    // Typed pointer to the first sample of channel `c`.
    template <typename T> T* plane(int c) {
        return reinterpret_cast<T*>(m_bytes.data()) + std::size_t(c) * samplesPerChannel();
    }
    template <typename T> const T* plane(int c) const {
        return reinterpret_cast<const T*>(m_bytes.data()) + std::size_t(c) * samplesPerChannel();
    }

private:
    int m_w = 0, m_h = 0, m_ch = 0;
    SampleFormat m_fmt = SampleFormat::Float32;
    ColorSpace   m_cs  = ColorSpace::Gray;
    std::vector<std::uint8_t> m_bytes;
};

} // namespace astro

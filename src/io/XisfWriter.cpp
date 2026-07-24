#include "io/XisfWriter.h"

#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <libxisf.h>   // from https://gitea.nouspiro.space/nou/libXISF

// -----------------------------------------------------------------------------
// As in XisfReader.cpp: libXISF's exact identifiers (method names, enum
// spellings, compression API) vary between releases. The calls below follow the
// common API shape; adjust the marked lines to your installed headers if needed.
// Everything funnels through the shared ImageData model, so any change is local.
// -----------------------------------------------------------------------------

namespace astro::io {

static LibXISF::Image::SampleFormat toXisfFormat(SampleFormat f) {
    using SF = LibXISF::Image::SampleFormat;
    switch (f) {
        case SampleFormat::UInt8:   return SF::UInt8;
        case SampleFormat::UInt16:  return SF::UInt16;
        case SampleFormat::UInt32:  return SF::UInt32;
        case SampleFormat::Float32: return SF::Float32;
        case SampleFormat::Float64: return SF::Float64;
        // XISF has no signed-integer sample types; callers should promote first.
        case SampleFormat::Int16:
        case SampleFormat::Int32:   return SF::Float32;
    }
    return SF::Float32;
}

static LibXISF::DataBlock::CompressionCodec toXisfCodec(SaveOptions::Compression c) {
    using C = LibXISF::DataBlock::CompressionCodec;
    switch (c) {
        case SaveOptions::Compression::None:  return C::None;
        case SaveOptions::Compression::Zlib:  return C::Zlib;
        case SaveOptions::Compression::LZ4:   return C::LZ4;
        case SaveOptions::Compression::LZ4HC: return C::LZ4HC;
        // Zstd was added in newer libXISF; fall back where it's unavailable.
        case SaveOptions::Compression::Zstd:  return C::LZ4HC;
    }
    return C::LZ4;
}

bool XisfWriter::canWrite(const QString& path) const {
    return QFileInfo(path).suffix().toLower() == "xisf";
}

SaveResult XisfWriter::save(const QString& path, const ImageData& image,
                            const ImageHeader& header, const SaveOptions& opts) const {
    SaveResult r;
    if (!image.isValid()) { r.error = QStringLiteral("Empty image"); return r; }

    try {
        const int ch = image.channels();
        LibXISF::Image im(image.width(), image.height(),
                          ch,
                          toXisfFormat(image.format()),
                          ch == 3 ? LibXISF::Image::RGB : LibXISF::Image::Gray);

        // PixInsight expects floating-point XISF samples normalized to [0,1]
        // (the spec's bounds convention); out-of-range floats render there as
        // noise. Rescale float data at save and record the mapping in FITS
        // keywords so the original range is recoverable.
        const bool isFloat = image.format() == SampleFormat::Float32 ||
                             image.format() == SampleFormat::Float64;
        double lo = 0.0, hi = 1.0;
        bool rescale = false;
        if (isFloat) {
            lo = std::numeric_limits<double>::infinity();
            hi = -std::numeric_limits<double>::infinity();
            const std::size_t n = image.samplesPerChannel();
            for (int c = 0; c < ch; ++c) {
                const float* p = image.plane<float>(c);
                for (std::size_t i = 0; i < n; ++i) {
                    const float v = p[i];
                    if (!std::isfinite(v)) continue;
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                }
            }
            if (!std::isfinite(lo) || hi <= lo) { lo = 0.0; hi = 1.0; }
            rescale = lo < 0.0 || hi > 1.0;
        }

        if (rescale && image.format() == SampleFormat::Float32) {
            const double s = 1.0 / (hi - lo);
            const std::size_t n = image.samplesPerChannel();
            float* dst = reinterpret_cast<float*>(im.imageData());
            for (int c = 0; c < ch; ++c) {
                const float* p = image.plane<float>(c);
                float* o = dst + std::size_t(c) * n;
                for (std::size_t i = 0; i < n; ++i)
                    o[i] = std::isfinite(p[i]) ? float((p[i] - lo) * s) : 0.0f;
            }
        } else {
            // Planar layout matches on both sides -> straight copy.
            const std::size_t n = std::min<std::size_t>(image.byteSize(), im.imageDataSize());
            std::memcpy(im.imageData(), image.bytes().data(), n);
        }

        if (opts.writeHeader) {
            for (const auto& c : header.cards) {
                LibXISF::FITSKeyword kw;
                kw.name    = c.key.toStdString();
                kw.value   = c.value.toStdString();
                kw.comment = c.comment.toStdString();
                im.addFITSKeyword(kw);
            }
        }
        if (rescale) {
            // v_original = NSSCALE * v_stored + NSZERO
            LibXISF::FITSKeyword k1;
            k1.name = "NSSCALE"; k1.value = std::to_string(hi - lo);
            k1.comment = "NebulaScope: original = NSSCALE*stored + NSZERO";
            im.addFITSKeyword(k1);
            LibXISF::FITSKeyword k2;
            k2.name = "NSZERO"; k2.value = std::to_string(lo);
            k2.comment = "NebulaScope: float data normalized to [0,1] for XISF";
            im.addFITSKeyword(k2);
        }

        // Compression is set per data block on the image before writing.
        im.setCompression(toXisfCodec(opts.xisfCompression), opts.compressionLevel);

        LibXISF::XISFWriter writer;
        writer.writeImage(im);
        writer.save(path.toStdString());
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = QString::fromUtf8(e.what());
    }
    return r;
}

} // namespace astro::io

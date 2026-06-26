#include "io/XisfWriter.h"

#include <QFileInfo>
#include <algorithm>
#include <cstring>

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

        // Planar layout matches on both sides -> straight copy.
        const std::size_t n = std::min<std::size_t>(image.byteSize(), im.imageDataSize());
        std::memcpy(im.imageData(), image.bytes().data(), n);

        if (opts.writeHeader) {
            for (const auto& c : header.cards) {
                LibXISF::FITSKeyword kw;
                kw.name    = c.key.toStdString();
                kw.value   = c.value.toStdString();
                kw.comment = c.comment.toStdString();
                im.addFITSKeyword(kw);
            }
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

#include "io/XisfReader.h"

#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cstring>
#include "core/Convert.h"

#include <libxisf.h>   // from https://gitea.nouspiro.space/nou/libXISF

// -----------------------------------------------------------------------------
// IMPORTANT: libXISF's exact identifiers (method names, enum spellings) can vary
// between releases. The calls below follow the common API shape; if your
// installed headers differ, adjust the marked lines — the surrounding logic is
// unaffected because everything funnels into the shared ImageData model.
// -----------------------------------------------------------------------------

namespace astro::io {

static SampleFormat xisfFormat(LibXISF::Image::SampleFormat f) {
    using SF = LibXISF::Image::SampleFormat;
    switch (f) {
        case SF::UInt8:   return SampleFormat::UInt8;
        case SF::UInt16:  return SampleFormat::UInt16;
        case SF::UInt32:  return SampleFormat::UInt32;
        case SF::Float32: return SampleFormat::Float32;
        case SF::Float64: return SampleFormat::Float64;
        default:          return SampleFormat::Float32;   // UInt64 / Complex* unhandled here
    }
}

static QString xisfTypeString(LibXISF::Image::SampleFormat f) {
    using SF = LibXISF::Image::SampleFormat;
    switch (f) {
        case SF::UInt8:   return "8-bit unsigned int";
        case SF::UInt16:  return "16-bit unsigned int";
        case SF::UInt32:  return "32-bit unsigned int";
        case SF::Float32: return "32-bit float";
        case SF::Float64: return "64-bit float";
        default:          return "unsupported sample format";
    }
}

bool XisfReader::canRead(const QString& path) const {
    if (QFileInfo(path).suffix().toLower() == "xisf") return true;
    // Magic: a monolithic XISF file starts with the ASCII signature "XISF0100".
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        return f.read(8) == QByteArray("XISF0100", 8);
    return false;
}

LoadResult XisfReader::load(const QString& path, const LoadOptions& opts) const {
    LoadResult r;
    try {
        LibXISF::XISFReader reader;
        reader.open(path.toStdString());                 // parses header, mmaps blocks

        if (reader.imagesCount() == 0) {
            r.error = QStringLiteral("XISF contains no images");
            return r;
        }

        // libXISF decompresses (zlib/LZ4/Zstd), un-shuffles and normalises to
        // native byte order, returning planar pixel data — a direct match for
        // ImageData's layout.
        const LibXISF::Image& im = reader.getImage(0);

        const int w  = im.width();
        const int h  = im.height();
        const int ch = im.channelCount();
        const SampleFormat fmt = xisfFormat(im.sampleFormat());
        const ColorSpace   cs  = (im.colorSpace() == LibXISF::Image::RGB)
                                     ? ColorSpace::RGB : ColorSpace::Gray;

        ImageData img(w, h, ch, fmt, cs);
        const std::size_t n = std::min<std::size_t>(img.byteSize(), im.imageDataSize());
        std::memcpy(img.bytes().data(), im.imageData(), n);

        // FITS keywords are embedded in XISF — surface them via the same header.
        // Unquote/trim values (PI writes them as FITS-card strings) so numeric
        // parsing (e.g. the WCS solution) sees clean numbers.
        for (const auto& kw : im.fitsKeywords()) {
            QString v = QString::fromStdString(kw.value).trimmed();
            if (v.startsWith('\'') && v.endsWith('\'') && v.size() >= 2)
                v = v.mid(1, v.size() - 2).trimmed();
            r.header.cards.push_back({ QString::fromStdString(kw.name),
                                       v,
                                       QString::fromStdString(kw.comment) });
        }

        // Richer XISF typed properties (exposure, camera, processing history...).
        for (const auto& p : im.imageProperties())
            r.header.properties.insert(QString::fromStdString(p.id),
                                       QString::fromStdString(p.value.toString()));

        // Promote integer images to Float32 (XISF integers normalize to [0,1]).
        if (opts.promoteToFloat && img.format() != SampleFormat::Float32)
            img = toFloat32(img, /*normalizeIntegers=*/true);

        // Orientation info for the Info panel.
        r.header.container = "XISF";
        r.header.nativeType = xisfTypeString(im.sampleFormat());
        r.header.structure << QStringLiteral("Image 0: %1\u00d7%2\u00d7%3, %4, %5")
                                  .arg(w).arg(h).arg(ch)
                                  .arg(xisfTypeString(im.sampleFormat()),
                                       cs == ColorSpace::RGB ? "RGB" : "Gray");
        if (reader.imagesCount() > 1)
            r.header.structure << QStringLiteral("(+ %1 more image%2 in file)")
                                      .arg(reader.imagesCount() - 1)
                                      .arg(reader.imagesCount() - 1 == 1 ? "" : "s");

        r.image = std::move(img);
        r.ok    = true;
    } catch (const std::exception& e) {
        r.error = QString::fromUtf8(e.what());
    }
    return r;
}

} // namespace astro::io

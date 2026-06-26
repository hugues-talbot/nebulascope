#include "io/FitsWriter.h"

#include <QFileInfo>
#include <QSet>
#include <valarray>
#include <vector>
#include <memory>
#include <cstring>

#include <CCfits/CCfits>   // pulls in <fitsio.h>

namespace astro::io {

// --- SampleFormat -> FITS BITPIX -------------------------------------------
// USHORT_IMG / ULONG_IMG are CFITSIO's unsigned image codes; CCfits writes the
// matching BZERO offset automatically, so unsigned data round-trips.
static long formatToBitpix(SampleFormat f) {
    switch (f) {
        case SampleFormat::UInt8:   return BYTE_IMG;     //   8
        case SampleFormat::Int16:   return SHORT_IMG;    //  16
        case SampleFormat::UInt16:  return USHORT_IMG;   //  16 + BZERO=32768
        case SampleFormat::Int32:   return LONG_IMG;     //  32
        case SampleFormat::UInt32:  return ULONG_IMG;    //  32 + BZERO
        case SampleFormat::Float32: return FLOAT_IMG;    // -32
        case SampleFormat::Float64: return DOUBLE_IMG;   // -64
    }
    return FLOAT_IMG;
}

template <typename T>
static void writePlanar(CCfits::PHDU& phdu, const ImageData& img) {
    const std::size_t n = img.sampleCount();
    std::valarray<T> buf(n);
    if (n) std::memcpy(&buf[0], img.bytes().data(), n * sizeof(T));
    phdu.write(1, long(n), buf);     // band-sequential == our planar layout
}

// Keywords that describe FITS structure are owned by CFITSIO; never copy them
// back in from a header we are re-emitting.
static bool isStructural(const QString& key) {
    static const QSet<QString> reserved = {
        "SIMPLE","BITPIX","EXTEND","BZERO","BSCALE","END","COMMENT","HISTORY","XTENSION","PCOUNT","GCOUNT"
    };
    const QString k = key.toUpper();
    return key.isEmpty() || k.startsWith("NAXIS") || reserved.contains(k);
}

static void writeHeader(CCfits::PHDU& phdu, const ImageHeader& header) {
    for (const auto& c : header.cards) {
        if (isStructural(c.key)) continue;
        try {
            phdu.addKey(c.key.toStdString(), c.value.toStdString(), c.comment.toStdString());
        } catch (...) { /* skip a single bad card, keep going */ }
    }
}

bool FitsWriter::canWrite(const QString& path) const {
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == "fits" || ext == "fit" || ext == "fts";
}

SaveResult FitsWriter::save(const QString& path, const ImageData& image,
                            const ImageHeader& header, const SaveOptions& opts) const {
    SaveResult r;
    if (!image.isValid()) { r.error = QStringLiteral("Empty image"); return r; }

    try {
        const int ch  = image.channels();
        long naxis    = (ch == 3) ? 3 : 2;
        std::vector<long> naxes = { image.width(), image.height() };
        if (naxis == 3) naxes.push_back(ch);

        // Leading '!' tells CFITSIO to overwrite an existing file.
        auto file = std::make_unique<CCfits::FITS>(
            "!" + path.toStdString(), formatToBitpix(image.format()), naxis, naxes.data());

        CCfits::PHDU& phdu = file->pHDU();
        switch (image.format()) {
            case SampleFormat::UInt8:   writePlanar<std::uint8_t> (phdu, image); break;
            case SampleFormat::Int16:   writePlanar<std::int16_t> (phdu, image); break;
            case SampleFormat::UInt16:  writePlanar<std::uint16_t>(phdu, image); break;
            case SampleFormat::Int32:   writePlanar<std::int32_t> (phdu, image); break;
            case SampleFormat::UInt32:  writePlanar<std::uint32_t>(phdu, image); break;
            case SampleFormat::Float32: writePlanar<float>        (phdu, image); break;
            case SampleFormat::Float64: writePlanar<double>       (phdu, image); break;
        }

        if (opts.writeHeader) writeHeader(phdu, header);
        r.ok = true;
    } catch (CCfits::FitsException& e) {
        r.error = QString::fromStdString(e.message());
    }
    return r;
}

} // namespace astro::io

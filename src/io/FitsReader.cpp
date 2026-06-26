#include "io/FitsReader.h"

#include <QFile>
#include <QFileInfo>
#include <valarray>
#include <memory>
#include <cstring>

#include <CCfits/CCfits>   // pulls in <fitsio.h>

namespace astro::io {

// --- BITPIX -> SampleFormat -------------------------------------------------
// NOTE on unsigned data: a 16-bit FITS image with BZERO=32768 encodes UInt16 as
// signed shorts. If you read into the matching integer type you get the RAW
// stored values and must apply BZERO/BSCALE yourself. If you instead read into
// float/double, CCfits applies the scaling for you. For a science viewer the
// simplest robust choice is often to promote integer images to Float32 on load;
// here we keep the native type and leave scaling to the caller. Tune to taste.
static SampleFormat bitpixToFormat(int bitpix) {
    switch (bitpix) {
        case BYTE_IMG:   return SampleFormat::UInt8;    //   8
        case SHORT_IMG:  return SampleFormat::Int16;    //  16  (see note)
        case LONG_IMG:   return SampleFormat::Int32;    //  32
        case FLOAT_IMG:  return SampleFormat::Float32;  // -32
        case DOUBLE_IMG: return SampleFormat::Float64;  // -64
        default:         return SampleFormat::Float32;
    }
}

template <typename T>
static void readPlanar(CCfits::PHDU& phdu, ImageData& img) {
    std::valarray<T> buf;
    phdu.read(buf);                                  // band-sequential == planar
    const std::size_t n = std::min<std::size_t>(buf.size(), img.sampleCount());
    if (n) std::memcpy(img.bytes().data(), &buf[0], n * sizeof(T));
}

static ImageHeader extractHeader(CCfits::PHDU& phdu) {
    ImageHeader h;
    phdu.readAllKeys();
    for (const auto& kv : phdu.keyWord()) {
        CCfits::Keyword* k = kv.second;
        std::string val;
        try { k->value(val); } catch (...) { /* non-string keyword */ }
        h.cards.push_back({ QString::fromStdString(k->name()),
                            QString::fromStdString(val),
                            QString::fromStdString(k->comment()) });
    }
    return h;
}

bool FitsReader::canRead(const QString& path) const {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "fits" || ext == "fit" || ext == "fts") return true;
    // Magic: every FITS primary header starts with the "SIMPLE" keyword.
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        return f.read(6) == QByteArray("SIMPLE", 6);
    return false;
}

LoadResult FitsReader::load(const QString& path, const LoadOptions& opts) const {
    LoadResult r;
    try {
        auto file = std::make_unique<CCfits::FITS>(
            path.toStdString(), CCfits::Read, /*readDataNow=*/false);
        CCfits::PHDU& phdu = file->pHDU();

        const int naxis = phdu.axes();
        if (naxis < 2) { r.error = QStringLiteral("No 2-D image in primary HDU"); return r; }

        const int w  = int(phdu.axis(0));
        const int h  = int(phdu.axis(1));
        const int ch = (naxis >= 3) ? int(phdu.axis(2)) : 1;   // RGB cube -> 3 planes
        const SampleFormat nativeFmt = bitpixToFormat(phdu.bitpix());

        // With promoteToFloat (default) read straight into float: CCfits applies
        // BSCALE/BZERO for us, so unsigned-16 (BZERO=32768) and any scaled
        // integer image come back as correct physical values automatically.
        const SampleFormat storeFmt = opts.promoteToFloat ? SampleFormat::Float32 : nativeFmt;

        ImageData img(w, h, ch, storeFmt, ch == 3 ? ColorSpace::RGB : ColorSpace::Gray);
        if (opts.promoteToFloat) {
            readPlanar<float>(phdu, img);
        } else {
            switch (nativeFmt) {
                case SampleFormat::UInt8:   readPlanar<std::uint8_t>(phdu, img);  break;
                case SampleFormat::Int16:   readPlanar<std::int16_t>(phdu, img);  break;
                case SampleFormat::UInt16:  readPlanar<std::uint16_t>(phdu, img); break;
                case SampleFormat::Int32:   readPlanar<std::int32_t>(phdu, img);  break;
                case SampleFormat::UInt32:  readPlanar<std::uint32_t>(phdu, img); break;
                case SampleFormat::Float32: readPlanar<float>(phdu, img);         break;
                case SampleFormat::Float64: readPlanar<double>(phdu, img);        break;
            }
        }

        r.header = extractHeader(phdu);
        r.image  = std::move(img);
        r.ok     = true;
    } catch (CCfits::FitsException& e) {
        r.error = QString::fromStdString(e.message());
    }
    return r;
}

} // namespace astro::io

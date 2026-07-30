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

// Write one card with its NATURAL type: quoted-string cards stay strings,
// bools stay logical, and bare numbers become numeric cards — a FITS
// FOCALLEN= '1672.4416' string frustrates every tool that reads it as a
// number (PixInsight included).
static void addTypedKey(CCfits::PHDU& phdu, const QString& key,
                        const QString& rawValue, const QString& comment) {
    QString v = rawValue.trimmed();
    const bool wasQuoted = v.size() >= 2 && v.startsWith('\'') && v.endsWith('\'');
    if (wasQuoted) v = v.mid(1, v.size() - 2).trimmed();
    const std::string k = key.toStdString(), cm = comment.toStdString();
    if (!wasQuoted) {
        if (v == QLatin1String("T")) { phdu.addKey(k, true, cm);  return; }
        if (v == QLatin1String("F")) { phdu.addKey(k, false, cm); return; }
        bool ok = false;
        const qlonglong i = v.toLongLong(&ok);
        if (ok) { phdu.addKey(k, static_cast<long>(i), cm); return; }
        const double d = v.toDouble(&ok);
        if (ok) { phdu.addKey(k, d, cm); return; }
    }
    phdu.addKey(k, v.toStdString(), cm);
}

// Standard keywords synthesized from XISF properties when the card itself is
// missing — PixInsight-native files often carry Observation:*/Instrument:*
// properties and no embedded FITS keywords at all; without this mapping a
// XISF → FITS save silently drops the observation metadata.
static void appendCardsFromProperties(const ImageHeader& header,
                                      std::vector<HeaderCard>& cards) {
    QSet<QString> have;
    for (const auto& c : cards) have.insert(c.key.toUpper());
    auto prop = [&header](const char* id) {
        return header.properties.value(QLatin1String(id)).toString();
    };
    auto add = [&](const char* key, const QString& value, const char* comment) {
        if (value.isEmpty() || have.contains(QLatin1String(key))) return;
        cards.push_back({ QLatin1String(key), value, QLatin1String(comment) });
    };
    auto scaled = [&prop](const char* id, double factor) -> QString {
        const QString v = prop(id);
        if (v.isEmpty()) return {};
        bool ok = false;
        const double d = v.toDouble(&ok);
        return ok ? QString::number(d * factor, 'g', 10) : QString();
    };
    QString t = prop("Observation:Time:Start");
    if (t.endsWith(QLatin1Char('Z'))) t.chop(1);          // FITS DATE-OBS is bare ISO
    add("DATE-OBS", t.isEmpty() ? t : QLatin1Char('\'') + t + QLatin1Char('\''),
        "Start of observation (from XISF Observation:Time:Start)");
    QString te = prop("Observation:Time:End");
    if (te.endsWith(QLatin1Char('Z'))) te.chop(1);
    add("DATE-END", te.isEmpty() ? te : QLatin1Char('\'') + te + QLatin1Char('\''),
        "End of observation (from XISF)");
    add("EXPTIME",  prop("Instrument:ExposureTime"), "Exposure time (s, from XISF)");
    add("FOCALLEN", scaled("Instrument:Telescope:FocalLength", 1000.0),
        "Focal length (mm, from XISF; property is metres)");
    add("APTDIA",   scaled("Instrument:Telescope:Aperture", 1000.0),
        "Aperture diameter (mm, from XISF)");
    add("XPIXSZ",   prop("Instrument:Sensor:XPixelSize"), "Pixel size X (um, from XISF)");
    add("YPIXSZ",   prop("Instrument:Sensor:YPixelSize"), "Pixel size Y (um, from XISF)");
    add("XBINNING", prop("Instrument:Camera:XBinning"), "Binning X (from XISF)");
    add("YBINNING", prop("Instrument:Camera:YBinning"), "Binning Y (from XISF)");
    add("GAIN",     prop("Instrument:Camera:Gain"), "Camera gain (from XISF)");
    const QString cam = prop("Instrument:Camera:Name");
    add("INSTRUME", cam.isEmpty() ? cam : QLatin1Char('\'') + cam + QLatin1Char('\''),
        "Camera (from XISF)");
    const QString tel = prop("Instrument:Telescope:Name");
    add("TELESCOP", tel.isEmpty() ? tel : QLatin1Char('\'') + tel + QLatin1Char('\''),
        "Telescope (from XISF)");
    const QString obj = prop("Observation:Object:Name");
    add("OBJECT",   obj.isEmpty() ? obj : QLatin1Char('\'') + obj + QLatin1Char('\''),
        "Object (from XISF)");
    add("RA",  prop("Observation:Center:RA"),  "Image centre RA (deg, from XISF)");
    add("DEC", prop("Observation:Center:Dec"), "Image centre Dec (deg, from XISF)");
}

static void writeHeader(CCfits::PHDU& phdu, const ImageHeader& header) {
    std::vector<HeaderCard> cards = header.cards;
    appendCardsFromProperties(header, cards);
    for (const auto& c : cards) {
        if (isStructural(c.key)) continue;
        try {
            addTypedKey(phdu, c.key, c.value, c.comment);
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

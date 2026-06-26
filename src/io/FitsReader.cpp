#include "io/FitsReader.h"

#include <QFile>
#include <QFileInfo>
#include <vector>
#include <cstring>

#include <fitsio.h>   // CFITSIO C API (handles MEF + tile-compressed transparently)

namespace astro::io {

// CFITSIO "equivalent" BITPIX (accounts for BSCALE/BZERO, e.g. USHORT_IMG) -> our type.
static SampleFormat equivToFormat(int eqbitpix) {
    switch (eqbitpix) {
        case BYTE_IMG:    return SampleFormat::UInt8;    //   8
        case SHORT_IMG:   return SampleFormat::Int16;    //  16
        case USHORT_IMG:  return SampleFormat::UInt16;   //  16 + BZERO=32768
        case LONG_IMG:    return SampleFormat::Int32;    //  32
        case ULONG_IMG:   return SampleFormat::UInt32;   //  32 + BZERO
        case FLOAT_IMG:   return SampleFormat::Float32;  // -32
        case DOUBLE_IMG:  return SampleFormat::Float64;  // -64
        default:          return SampleFormat::Float32;  // LONGLONG etc. -> float
    }
}

static int cfitsioType(SampleFormat f) {
    switch (f) {
        case SampleFormat::UInt8:   return TBYTE;
        case SampleFormat::Int16:   return TSHORT;
        case SampleFormat::UInt16:  return TUSHORT;
        case SampleFormat::Int32:   return TINT;
        case SampleFormat::UInt32:  return TUINT;
        case SampleFormat::Float32: return TFLOAT;
        case SampleFormat::Float64: return TDOUBLE;
    }
    return TFLOAT;
}

// Move to the first HDU holding a >=2-D image. Returns false if none found.
// A tile-compressed image extension reports as IMAGE_HDU to CFITSIO, so this
// transparently catches both MEF and Rice/gzip-compressed FITS.
static bool moveToFirstImage(fitsfile* fptr, int& naxis, long naxes[3], int& status) {
    int nhdus = 0;
    fits_get_num_hdus(fptr, &nhdus, &status);
    if (status) return false;

    for (int i = 1; i <= nhdus; ++i) {
        int hdutype = 0;
        fits_movabs_hdu(fptr, i, &hdutype, &status);
        if (status) { status = 0; continue; }
        if (hdutype != IMAGE_HDU) continue;

        int nd = 0;
        fits_get_img_dim(fptr, &nd, &status);
        if (status || nd < 2) { status = 0; continue; }

        long dims[9] = {0};
        fits_get_img_size(fptr, (nd > 9 ? 9 : nd), dims, &status);
        if (status) { status = 0; continue; }

        naxis = nd;
        naxes[0] = dims[0];
        naxes[1] = dims[1];
        naxes[2] = (nd >= 3) ? dims[2] : 1;
        return true;
    }
    return false;
}

static void extractHeader(fitsfile* fptr, ImageHeader& h, int& status) {
    int nkeys = 0;
    fits_get_hdrspace(fptr, &nkeys, nullptr, &status);
    char name[FLEN_KEYWORD], value[FLEN_VALUE], comment[FLEN_COMMENT];
    for (int i = 1; i <= nkeys; ++i) {
        if (fits_read_keyn(fptr, i, name, value, comment, &status)) { status = 0; continue; }
        QString v = QString::fromLatin1(value).trimmed();
        if (v.startsWith('\'') && v.endsWith('\'') && v.size() >= 2)
            v = v.mid(1, v.size() - 2).trimmed();          // unquote string values
        h.cards.push_back({ QString::fromLatin1(name), v, QString::fromLatin1(comment) });
    }
}

bool FitsReader::canRead(const QString& path) const {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "fits" || ext == "fit" || ext == "fts" || ext == "fz") return true;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        return f.read(6) == QByteArray("SIMPLE", 6);       // FITS primary header magic
    return false;
}

LoadResult FitsReader::load(const QString& path, const LoadOptions& opts) const {
    LoadResult r;
    fitsfile* fptr = nullptr;
    int status = 0;

    fits_open_file(&fptr, path.toLocal8Bit().constData(), READONLY, &status);
    if (status) {
        char msg[FLEN_ERRMSG] = {0};
        fits_get_errstatus(status, msg);
        r.error = QStringLiteral("CFITSIO: %1").arg(QString::fromLatin1(msg));
        if (fptr) { int s2 = 0; fits_close_file(fptr, &s2); }
        return r;
    }

    int naxis = 0;
    long naxes[3] = {0, 0, 1};
    if (!moveToFirstImage(fptr, naxis, naxes, status)) {
        r.error = QStringLiteral("No 2-D image found in any HDU (primary or extensions)");
        int s2 = 0; fits_close_file(fptr, &s2);
        return r;
    }

    const int w  = int(naxes[0]);
    const int h  = int(naxes[1]);
    const int ch = (naxis >= 3) ? int(naxes[2]) : 1;       // 3rd axis -> RGB planes

    int eqtype = 0;
    fits_get_img_equivtype(fptr, &eqtype, &status);
    const SampleFormat nativeFmt = equivToFormat(eqtype);
    // promoteToFloat (default): read as TFLOAT; CFITSIO applies BSCALE/BZERO and
    // decompresses, so scaled-integer and compressed images come back correct.
    const SampleFormat storeFmt = opts.promoteToFloat ? SampleFormat::Float32 : nativeFmt;

    ImageData img(w, h, ch, storeFmt, ch == 3 ? ColorSpace::RGB : ColorSpace::Gray);

    std::vector<long> fpix(naxis, 1);                      // 1-based first pixel
    const long nelem = long(img.samplesPerChannel()) * ch; // band-sequential == planar
    fits_read_pix(fptr, cfitsioType(storeFmt), fpix.data(), nelem,
                  nullptr, img.bytes().data(), nullptr, &status);

    if (status) {
        char msg[FLEN_ERRMSG] = {0};
        fits_get_errstatus(status, msg);
        r.error = QStringLiteral("CFITSIO read: %1").arg(QString::fromLatin1(msg));
        int s2 = 0; fits_close_file(fptr, &s2);
        return r;
    }

    int hs = 0;
    extractHeader(fptr, r.header, hs);

    int s2 = 0;
    fits_close_file(fptr, &s2);

    r.image = std::move(img);
    r.ok = true;
    return r;
}

} // namespace astro::io

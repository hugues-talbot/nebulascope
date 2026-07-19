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

// Human-readable on-disk pixel type from a CFITSIO (equiv) BITPIX value.
static QString typeString(int eqbitpix) {
    switch (eqbitpix) {
        case BYTE_IMG:     return "8-bit unsigned int";
        case SHORT_IMG:    return "16-bit signed int";
        case USHORT_IMG:   return "16-bit unsigned int";
        case LONG_IMG:     return "32-bit signed int";
        case ULONG_IMG:    return "32-bit unsigned int";
        case LONGLONG_IMG: return "64-bit int";
        case FLOAT_IMG:    return "32-bit float";
        case DOUBLE_IMG:   return "64-bit float";
        default:           return QStringLiteral("BITPIX %1").arg(eqbitpix);
    }
}

// Walk every HDU and append a one-line summary; record the first image HDU.
static void enumerateHdus(fitsfile* fptr, QStringList& out, int& firstImageHdu, int& status) {
    int nhdus = 0;
    fits_get_num_hdus(fptr, &nhdus, &status);
    if (status) return;

    for (int i = 1; i <= nhdus; ++i) {
        int hdutype = 0;
        fits_movabs_hdu(fptr, i, &hdutype, &status);
        if (status) { status = 0; continue; }

        QString line = QStringLiteral("HDU %1: ").arg(i - 1);
        if (hdutype == IMAGE_HDU) {
            int nd = 0;
            fits_get_img_dim(fptr, &nd, &status);
            if (nd < 2) {
                line += "Primary/Image \u2014 no data";
            } else {
                long dims[9] = {0};
                fits_get_img_size(fptr, (nd > 9 ? 9 : nd), dims, &status);
                int eq = 0; fits_get_img_equivtype(fptr, &eq, &status);
                QString geom = QString::number(dims[0]);
                for (int d = 1; d < nd; ++d) geom += QStringLiteral("\u00d7%1").arg(dims[d]);
                const bool comp = fits_is_compressed_image(fptr, &status) != 0;
                line += QStringLiteral("Image %1, %2%3")
                            .arg(geom, typeString(eq), comp ? " (compressed)" : "");
                if (firstImageHdu < 0) firstImageHdu = i;
            }
        } else if (hdutype == ASCII_TBL || hdutype == BINARY_TBL) {
            long nrows = 0; int ncols = 0;
            fits_get_num_rows(fptr, &nrows, &status);
            fits_get_num_cols(fptr, &ncols, &status);
            line += QStringLiteral("%1 table, %2 cols \u00d7 %3 rows")
                        .arg(hdutype == BINARY_TBL ? "Binary" : "ASCII").arg(ncols).arg(nrows);
        } else {
            line += "Other";
        }
        if (status) status = 0;
        out << line;
    }
}

// Move to the first HDU holding a >=2-D image. Returns false if none found.
// A tile-compressed image extension reports as IMAGE_HDU to CFITSIO, so this
// transparently catches both MEF and Rice/gzip-compressed FITS.
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

QList<FitsHduEntry> listFitsImageHdus(const QString& path) {
    QList<FitsHduEntry> out;
    fitsfile* fptr = nullptr;
    int status = 0;
    fits_open_file(&fptr, path.toLocal8Bit().constData(), READONLY, &status);
    if (status) { if (fptr) { int s2 = 0; fits_close_file(fptr, &s2); } return out; }

    int nhdus = 0;
    fits_get_num_hdus(fptr, &nhdus, &status);
    for (int i = 1; i <= nhdus && !status; ++i) {
        int hdutype = 0;
        if (fits_movabs_hdu(fptr, i, &hdutype, &status)) { status = 0; continue; }
        if (hdutype != IMAGE_HDU) continue;
        int nd = 0;
        fits_get_img_dim(fptr, &nd, &status);
        if (status) { status = 0; continue; }
        if (nd < 2) continue;
        long dims[9] = {0};
        fits_get_img_size(fptr, (nd > 9 ? 9 : nd), dims, &status);
        int eq = 0; fits_get_img_equivtype(fptr, &eq, &status);
        const bool comp = fits_is_compressed_image(fptr, &status) != 0;
        if (status) { status = 0; continue; }
        QString geom = QString::number(dims[0]);
        for (int d = 1; d < nd; ++d) geom += QStringLiteral("\u00d7%1").arg(dims[d]);
        // EXTNAME, when present, is the astronomer's label for the extension.
        char extname[FLEN_VALUE] = {0};
        int s3 = 0;
        fits_read_key(fptr, TSTRING, "EXTNAME", extname, nullptr, &s3);
        FitsHduEntry e;
        e.hdu = i - 1;
        e.summary = QStringLiteral("%1%2 \u00b7 %3%4")
                        .arg(s3 == 0 && extname[0] ? QString::fromLatin1(extname) + QStringLiteral(" \u00b7 ") : QString(),
                             geom, typeString(eq),
                             comp ? QStringLiteral(" (compressed)") : QString());
        out.append(e);
    }
    int s2 = 0;
    fits_close_file(fptr, &s2);
    return out;
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

    // Enumerate all HDUs for the structure view, and locate the first image.
    int firstImageHdu = -1;
    QStringList structure;
    enumerateHdus(fptr, structure, firstImageHdu, status);
    if (firstImageHdu < 0) {
        r.error = QStringLiteral("No 2-D image found in any HDU (primary or extensions)");
        int s2 = 0; fits_close_file(fptr, &s2);
        return r;
    }

    // A specific HDU may be requested (multi-extension FITS); validate it.
    int targetHdu = firstImageHdu;                       // 1-based (CFITSIO)
    if (opts.fitsHdu >= 0) {
        targetHdu = opts.fitsHdu + 1;
        int hdutype2 = 0, nd2 = 0;
        if (fits_movabs_hdu(fptr, targetHdu, &hdutype2, &status) || hdutype2 != IMAGE_HDU ||
            fits_get_img_dim(fptr, &nd2, &status) || nd2 < 2) {
            r.error = QStringLiteral("HDU %1 does not contain a 2-D image").arg(opts.fitsHdu);
            int s2 = 0; fits_close_file(fptr, &s2);
            return r;
        }
    }

    int hdutype = 0;
    fits_movabs_hdu(fptr, targetHdu, &hdutype, &status);
    int naxis = 0;
    long naxes[9] = {0};
    fits_get_img_dim(fptr, &naxis, &status);
    fits_get_img_size(fptr, (naxis > 9 ? 9 : naxis), naxes, &status);

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
    r.header.container = "FITS";
    r.header.nativeType = typeString(eqtype);
    r.header.structure = structure;
    if (structure.size() > 1)
        r.header.structure.prepend(QStringLiteral("Showing HDU %1").arg(targetHdu - 1));

    int s2 = 0;
    fits_close_file(fptr, &s2);

    r.image = std::move(img);
    r.ok = true;
    return r;
}

} // namespace astro::io

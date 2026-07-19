#pragma once
#include "io/ImageReader.h"

namespace astro::io {

// One image HDU inside a (possibly multi-extension) FITS file.
struct FitsHduEntry {
    int     hdu = 0;        // absolute index, 0-based (HDU 0 = primary)
    QString summary;        // e.g. "4144×2822 · 16-bit unsigned int (compressed)"
};

// Header-only scan: every HDU holding a >=2-D image, in file order. Cheap (no
// pixel data is read); empty on error or when the file has no image HDU.
QList<FitsHduEntry> listFitsImageHdus(const QString& path);

// FITS backend, implemented on top of CCfits (the C++ wrapper over CFITSIO).
class FitsReader : public ImageReader {
public:
    bool        canRead(const QString& path) const override;
    LoadResult  load(const QString& path, const LoadOptions& opts = {}) const override;
    QString     name() const override { return QStringLiteral("FITS"); }
    QStringList extensions() const override { return { "fits", "fit", "fts" }; }
};

} // namespace astro::io

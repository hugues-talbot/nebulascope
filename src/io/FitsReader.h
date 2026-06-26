#pragma once
#include "io/ImageReader.h"

namespace astro::io {

// FITS backend, implemented on top of CCfits (the C++ wrapper over CFITSIO).
class FitsReader : public ImageReader {
public:
    bool        canRead(const QString& path) const override;
    LoadResult  load(const QString& path, const LoadOptions& opts = {}) const override;
    QString     name() const override { return QStringLiteral("FITS"); }
    QStringList extensions() const override { return { "fits", "fit", "fts" }; }
};

} // namespace astro::io

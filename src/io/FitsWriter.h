#pragma once
#include "io/ImageWriter.h"

namespace astro::io {

// FITS writer on top of CCfits / CFITSIO.
class FitsWriter : public ImageWriter {
public:
    bool        canWrite(const QString& path) const override;
    SaveResult  save(const QString& path, const ImageData& image,
                     const ImageHeader& header = {}, const SaveOptions& opts = {}) const override;
    QString     name() const override { return QStringLiteral("FITS"); }
    QStringList extensions() const override { return { "fits", "fit", "fts" }; }
};

} // namespace astro::io

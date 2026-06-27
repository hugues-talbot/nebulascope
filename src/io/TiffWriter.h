#pragma once
#include "io/ImageWriter.h"

namespace astro::io {

// TIFF writer via Qt's image plugins. Writes a 16-bit TIFF (grayscale or RGB).
// The float ImageData is linearly scaled by its finite min/max to fill the
// 16-bit range — a faithful linear export of the data (not the stretched view).
class TiffWriter : public ImageWriter {
public:
    bool        canWrite(const QString& path) const override;
    SaveResult  save(const QString& path, const ImageData& image,
                     const ImageHeader& header = {}, const SaveOptions& opts = {}) const override;
    QString     name() const override { return QStringLiteral("TIFF"); }
    QStringList extensions() const override { return { "tif", "tiff" }; }
};

} // namespace astro::io

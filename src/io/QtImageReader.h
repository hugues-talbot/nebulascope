#pragma once
#include "io/ImageReader.h"

namespace astro::io {

// Reader for ordinary picture formats (JPEG/PNG/TIFF) via Qt's image plugins.
// These are display-referred sRGB images; integer samples are normalized to
// [0,1] (like XISF) when promoted to Float32. 16-bit PNG/TIFF keep their depth.
class QtImageReader : public ImageReader {
public:
    bool        canRead(const QString& path) const override;
    LoadResult  load(const QString& path, const LoadOptions& opts = {}) const override;
    QString     name() const override { return QStringLiteral("Image"); }
    QStringList extensions() const override { return { "jpg", "jpeg", "png", "tif", "tiff" }; }
};

} // namespace astro::io

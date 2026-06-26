#pragma once
#include "io/ImageWriter.h"

namespace astro::io {

// XISF writer on top of libXISF (handles XML header, planar blocks, and
// zlib/LZ4/Zstd compression). https://gitea.nouspiro.space/nou/libXISF
class XisfWriter : public ImageWriter {
public:
    bool        canWrite(const QString& path) const override;
    SaveResult  save(const QString& path, const ImageData& image,
                     const ImageHeader& header = {}, const SaveOptions& opts = {}) const override;
    QString     name() const override { return QStringLiteral("XISF"); }
    QStringList extensions() const override { return { "xisf" }; }
};

} // namespace astro::io

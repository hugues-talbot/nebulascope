#pragma once
#include "io/ImageReader.h"

namespace astro::io {

// XISF backend (PixInsight's native open format), implemented on top of
// libXISF — a lightweight standalone C++/CMake library that handles the XML
// header, planar data blocks, compression (zlib/LZ4/Zstd) and checksums.
//   https://gitea.nouspiro.space/nou/libXISF
class XisfReader : public ImageReader {
public:
    bool        canRead(const QString& path) const override;
    LoadResult  load(const QString& path, const LoadOptions& opts = {}) const override;
    QString     name() const override { return QStringLiteral("XISF"); }
    QStringList extensions() const override { return { "xisf" }; }
};

} // namespace astro::io

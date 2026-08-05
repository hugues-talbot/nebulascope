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

// Parse the m/s/h attributes of an XISF <DisplayFunction> element (each a
// colon-separated list of 3-4 reals, RGB/K order). Returns a .valid result
// only when all three parse with at least 3 components. Exposed for tests.
DisplayFunction parseDisplayFunction(const QString& m, const QString& s, const QString& h);

} // namespace astro::io

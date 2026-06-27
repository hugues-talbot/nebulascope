#include "io/ImageReader.h"
#include "io/FitsReader.h"
#include "io/XisfReader.h"
#include "io/QtImageReader.h"

namespace astro::io {

std::vector<ImageReader*> registeredReaders() {
    // Readers are stateless and cheap; keep one static instance of each.
    // Order matters only for ambiguous probes — list most-specific first.
    static FitsReader fits;
    static XisfReader xisf;
    static QtImageReader picture;   // JPEG / PNG / TIFF
    static std::vector<ImageReader*> readers { &fits, &xisf, &picture };
    return readers;
}

ImageReader* readerForFile(const QString& path) {
    for (ImageReader* r : registeredReaders())
        if (r->canRead(path)) return r;
    return nullptr;
}

LoadResult loadImage(const QString& path, const LoadOptions& opts) {
    if (ImageReader* r = readerForFile(path))
        return r->load(path, opts);
    LoadResult res;
    res.error = QStringLiteral("Unsupported file format: %1").arg(path);
    return res;
}

} // namespace astro::io

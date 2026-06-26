#pragma once
//
// ImageWriter — backend-agnostic encoder interface + registry, the mirror of
// ImageReader. Each concrete writer serializes the shared ImageData/ImageHeader
// model to one container format. Whatever sample type the image carries
// (e.g. the promoted Float32 from the read path) is what gets written.
//
#include "core/ImageData.h"
#include "core/ImageHeader.h"
#include <QString>
#include <QStringList>
#include <vector>

namespace astro::io {

struct SaveResult {
    bool    ok = false;
    QString error;
};

struct SaveOptions {
    // XISF data-block compression (ignored by the FITS backend).
    enum class Compression { None, Zlib, LZ4, LZ4HC, Zstd };
    Compression xisfCompression = Compression::LZ4;
    int  compressionLevel = -1;   // -1 = codec default
    bool writeHeader      = true;  // emit FITS cards / XISF properties
};

class ImageWriter {
public:
    virtual ~ImageWriter() = default;

    // Cheap test: does this writer handle the path's container (by extension)?
    virtual bool canWrite(const QString& path) const = 0;

    // Encode `image` (+ optional `header`) to `path`. Never throws across this
    // boundary — failures come back via SaveResult::ok / ::error.
    virtual SaveResult save(const QString& path,
                            const ImageData& image,
                            const ImageHeader& header = {},
                            const SaveOptions& opts = {}) const = 0;

    virtual QString     name()       const = 0;
    virtual QStringList extensions() const = 0;
};

// --- registry / convenience ------------------------------------------------
std::vector<ImageWriter*> registeredWriters();
ImageWriter*              writerForFile(const QString& path);
SaveResult                saveImage(const QString& path,
                                    const ImageData& image,
                                    const ImageHeader& header = {},
                                    const SaveOptions& opts = {});

} // namespace astro::io

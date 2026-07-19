#pragma once
//
// ImageReader — backend-agnostic decoder interface + tiny registry.
//
// Each concrete reader understands ONE container format (FITS, XISF, ...) and
// decodes it into the shared ImageData/ImageHeader model. Everything downstream
// — stretch, histogram, GHS, display — is therefore format-blind.
//
// Adding a new format = subclass ImageReader, implement canRead()/load(), and
// add an instance to registeredReaders() in ImageReader.cpp. Nothing else
// changes.
//
#include "core/ImageData.h"
#include "core/ImageHeader.h"
#include <QString>
#include <QStringList>
#include <vector>

namespace astro::io {

struct LoadResult {
    bool        ok = false;
    QString     error;     // human-readable reason when !ok
    ImageData   image;
    ImageHeader header;
};

struct LoadOptions {
    // Promote integer images to Float32 on load so the whole pipeline works on
    // one uniform sample type. FITS: read via CCfits, which applies BSCALE/
    // BZERO -> correct physical values (also fixes unsigned-16 / BZERO=32768).
    // XISF: integer samples normalize to [0,1] by their full-scale. Float
    // sources pass through unchanged. Set false to keep the native type.
    bool promoteToFloat = true;

    // FITS only: absolute HDU index (0-based, as shown in the Info panel) of the
    // image to load. -1 = the first HDU containing a >=2-D image (default).
    int fitsHdu = -1;
};

class ImageReader {
public:
    virtual ~ImageReader() = default;

    // Cheap probe: extension match and/or magic-byte sniff. Must NOT fully decode.
    virtual bool canRead(const QString& path) const = 0;

    // Decode the primary image. Never throws across this boundary — failures
    // are reported via LoadResult::ok / ::error.
    virtual LoadResult load(const QString& path, const LoadOptions& opts = {}) const = 0;

    virtual QString     name()       const = 0;   // "FITS", "XISF"
    virtual QStringList extensions() const = 0;   // {"fits","fit","fts"}
};

// --- registry / convenience ------------------------------------------------
std::vector<ImageReader*> registeredReaders();
ImageReader*              readerForFile(const QString& path);  // nullptr if unsupported
LoadResult                loadImage(const QString& path, const LoadOptions& opts = {});

} // namespace astro::io

#pragma once
//
// DisplayRenderer — applies a StretchModel to an ImageData and produces an
// 8-bit RGB QImage for screen. Builds a per-channel lookup table once per
// render, then maps every pixel through it.
//
#include "core/ImageData.h"
#include "render/StretchModel.h"
#include <QImage>

class QColorTransform;

namespace astro {

class DisplayRenderer {
public:
    // xform: optional colour-space conversion (source profile → display
    // panel), applied per pixel at 16-bit precision INSIDE the pipeline so
    // the 8-bit dither runs LAST — converting after quantisation would band.
    static QImage render(const ImageData& img, const StretchModel& model,
                         const QColorTransform* xform = nullptr);
    // Bake the stretch into Float32 [0,1] pixel data at full precision (for
    // saving a non-linear edit as FITS/XISF). Mono stays 1-channel unless a
    // non-Gray colormap is active, which produces 3 channels.
    static ImageData renderFloat(const ImageData& img, const StretchModel& model);
};

} // namespace astro

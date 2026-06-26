#pragma once
//
// DisplayRenderer — applies a StretchModel to an ImageData and produces an
// 8-bit RGB QImage for screen. Builds a per-channel lookup table once per
// render, then maps every pixel through it.
//
#include "core/ImageData.h"
#include "render/StretchModel.h"
#include <QImage>

namespace astro {

class DisplayRenderer {
public:
    static QImage render(const ImageData& img, const StretchModel& model);
};

} // namespace astro

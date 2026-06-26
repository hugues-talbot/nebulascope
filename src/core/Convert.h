#pragma once
#include "core/ImageData.h"

namespace astro {

// Returns a Float32 copy of `src`. If `src` is already Float32 it is returned
// unchanged. When `normalizeIntegers` is true, UNSIGNED integer samples are
// scaled to [0,1] by their full-scale value (the XISF convention); signed
// integers and Float64 are cast directly. Used to give the rendering/stretch
// pipeline a single, uniform sample type.
ImageData toFloat32(const ImageData& src, bool normalizeIntegers);

} // namespace astro

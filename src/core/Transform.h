#pragma once
//
// Transform — lossless geometric operations on ImageData: 90° rotations and
// axis flips. These work on the raw planar bytes with the image's sample size,
// so every pixel type is handled generically and values are preserved exactly
// (so histogram statistics remain valid; only geometry changes).
//
#include "core/ImageData.h"

namespace astro {

ImageData rotate90(const ImageData& src, bool clockwise);  // ±90°, swaps W/H
ImageData flipHorizontal(const ImageData& src);            // left <-> right
ImageData flipVertical(const ImageData& src);              // top  <-> bottom

} // namespace astro

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

// Arbitrary rotation (positive = counter-clockwise on screen). Unlike the 90°
// ops this is NOT lossless: pixels are bilinearly resampled, the canvas grows
// to the rotated bounding box, and uncovered corners are filled with NaN
// (which the whole pipeline already treats as blank). Float32 only — the
// loader promotes everything to Float32, so this always holds in practice.
ImageData rotateArbitrary(const ImageData& src, double angleDeg);

} // namespace astro

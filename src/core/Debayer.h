#pragma once
//
// Debayer — demosaic a colour-filter-array (Bayer) mono frame into RGB.
//
// OSC cameras (ASI 533MC via ASIAIR/ASICAP, DSLRs, ...) deliver one mono
// plane where each pixel saw the sky through one filter of a repeating 2×2
// mosaic. The pattern name states the top-left 2×2 in row order: RGGB means
// row 0 = R G, row 1 = G B. FITS carries it as BAYERPAT (with optional
// XBAYROFF/YBAYROFF shifting the origin); PixInsight XISF embeds the same
// FITS keywords.
//
// Algorithms:
//   * Superpixel — each 2×2 cell becomes ONE RGB pixel (G = mean of the two
//     greens). Half resolution, zero interpolation artifacts, fastest.
//   * Bilinear  — full resolution, each missing sample averaged from its
//     nearest same-colour neighbours. Fast; slight star-edge artifacts.
//   * RCD       — Ratio Corrected Demosaicing (Luis Sanz Rodríguez; the
//     Siril/RawTherapee default). Directional green interpolation with a
//     low-pass discrimination, then ratio-corrected R/B. Best on stars.
//
// Input: 1-channel Float32 ImageData (any range). Output: 3-channel Float32
// RGB, same range; full size (Bilinear/RCD) or halved (Superpixel).
//
#include "core/ImageData.h"

namespace astro {

struct ImageHeader;

enum class BayerPattern { None, RGGB, BGGR, GRBG, GBRG };
enum class DebayerMethod { Superpixel, Bilinear, RCD };

const char* bayerPatternName(BayerPattern p);          // "RGGB", ... ("none")
BayerPattern bayerPatternFromString(const char* s);    // tolerant: case/padding

// Detect the pattern from header keywords: BAYERPAT, shifted by
// XBAYROFF/YBAYROFF when present (odd offsets swap pattern columns/rows).
// BayerPattern::None when the header carries no (usable) pattern.
BayerPattern bayerPatternFromHeader(const ImageHeader& h);

// Demosaic. Returns an invalid ImageData if `cfa` is not 1-channel Float32
// or the pattern is None.
ImageData debayer(const ImageData& cfa, BayerPattern p, DebayerMethod m);

} // namespace astro

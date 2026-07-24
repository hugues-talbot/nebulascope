#pragma once
//
// Adjustments — post-stretch display adjustments, applied to the stretched
// [0,1] values AFTER the transfer function (Linear/Log/Asinh/GHS), before
// colormap lookup / 8-bit quantisation. Mode-independent by construction.
//
//   * Tone ops (blackpoint/whitepoint, shadows/highlights, brightness,
//     contrast, gamma) are per-channel monotone curves — the renderer composes
//     them into its transfer LUTs, so they are free per pixel.
//   * Colour ops (temperature/tint, hue, saturation, vibrance) are
//     cross-channel and run per pixel, only when non-identity (RGB images).
//
#include <algorithm>
#include <cmath>

namespace astro {

struct AdjustParams {
    // Tone (per-channel, monotone)
    double blackpoint = 0.0;    // 0..0.5   clip-in from black (display space)
    double whitepoint = 1.0;    // 0.5..1   clip-in from white
    double shadows    = 0.0;    // -1..1    lift / crush dark tones
    double highlights = 0.0;    // -1..1    boost / recover bright tones
    double brightness = 0.0;    // -1..1
    double contrast   = 0.0;    // -1..1    pivot 0.5
    double gamma      = 1.0;    // ~0.33..3
    // Colour (cross-channel; RGB images)
    double temperature = 0.0;   // -1..1    blue <-> amber
    double tint        = 0.0;   // -1..1    green <-> magenta
    double hue         = 0.0;   // degrees, -180..180
    double saturation  = 0.0;   // -1..1
    double vibrance    = 0.0;   // -1..1    saturation weighted to muted pixels

    bool toneIdentity() const {
        return blackpoint == 0.0 && whitepoint == 1.0 && shadows == 0.0 &&
               highlights == 0.0 && brightness == 0.0 && contrast == 0.0 && gamma == 1.0;
    }
    bool colorIdentity() const {
        return temperature == 0.0 && tint == 0.0 && hue == 0.0 &&
               saturation == 0.0 && vibrance == 0.0;
    }
    bool identity() const { return toneIdentity() && colorIdentity(); }
};

// Tone curve on one stretched value. Order: BP/WP window -> shadows/highlights
// -> brightness -> contrast -> gamma. Maps [0,1] -> [0,1], monotone for the
// documented parameter ranges.
inline float applyTone(float y, const AdjustParams& a) {
    double v = y;
    if (a.blackpoint != 0.0 || a.whitepoint != 1.0) {
        const double d = std::max(1e-6, a.whitepoint - a.blackpoint);
        v = (v - a.blackpoint) / d;
        v = v < 0 ? 0 : (v > 1 ? 1 : v);
    }
    // Smooth weights that vanish at both ends: shadows acts on (1-v)^2·v,
    // highlights on v^2·(1-v) — black point and white point stay pinned.
    if (a.shadows != 0.0)    v += a.shadows    * v * (1.0 - v) * (1.0 - v) * 2.0;
    if (a.highlights != 0.0) v += a.highlights * v * v * (1.0 - v) * 2.0;
    if (a.brightness != 0.0) v += a.brightness * 0.5;
    if (a.contrast != 0.0) {
        // tan maps [-1,1] -> gain (0,inf) smoothly, 1 at 0 (kPi4 = pi/4).
        constexpr double kPi4 = 0.78539816339744830961;
        v = 0.5 + (v - 0.5) * std::tan((a.contrast + 1.0) * kPi4);
    }
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    if (a.gamma != 1.0) v = std::pow(v, 1.0 / a.gamma);
    return float(v);
}

// Cross-channel colour ops on a stretched RGB triple, in place.
// Order: white balance (temperature/tint gains) -> hue rotation -> sat/vibrance.
inline void applyColor(float& r, float& g, float& b, const AdjustParams& a) {
    double R = r, G = g, B = b;
    if (a.temperature != 0.0 || a.tint != 0.0) {
        R *= 1.0 + 0.30 * a.temperature + 0.15 * a.tint;
        G *= 1.0 - 0.30 * a.tint;
        B *= 1.0 - 0.30 * a.temperature + 0.15 * a.tint;
    }
    if (a.hue != 0.0) {
        // Standard luminance-preserving hue rotation (CSS/SVG hue-rotate matrix).
        const double th = a.hue * 0.01745329251994329577;   // deg -> rad
        const double c = std::cos(th), s = std::sin(th);
        const double nR = R*(0.299+0.701*c+0.168*s) + G*(0.587-0.587*c+0.330*s) + B*(0.114-0.114*c-0.497*s);
        const double nG = R*(0.299-0.299*c-0.328*s) + G*(0.587+0.413*c+0.035*s) + B*(0.114-0.114*c+0.292*s);
        const double nB = R*(0.299-0.300*c+1.250*s) + G*(0.587-0.588*c-1.050*s) + B*(0.114+0.886*c-0.203*s);
        R = nR; G = nG; B = nB;
    }
    if (a.saturation != 0.0 || a.vibrance != 0.0) {
        const double Y = 0.2126 * R + 0.7152 * G + 0.0722 * B;
        double f = 1.0 + a.saturation;
        if (a.vibrance != 0.0) {
            const double mx = std::max(R, std::max(G, B));
            const double mn = std::min(R, std::min(G, B));
            const double sat = mx > 1e-9 ? (mx - mn) / mx : 0.0;
            f *= 1.0 + a.vibrance * (1.0 - sat);       // muted pixels move most
        }
        R = Y + (R - Y) * f;
        G = Y + (G - Y) * f;
        B = Y + (B - Y) * f;
    }
    r = float(R < 0 ? 0 : (R > 1 ? 1 : R));
    g = float(G < 0 ? 0 : (G > 1 ? 1 : G));
    b = float(B < 0 ? 0 : (B > 1 ? 1 : B));
}

} // namespace astro

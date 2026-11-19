#pragma once
//
// Ds9Regions — import of SAOImage DS9 region files (.reg), resolved to the
// three vector primitives the annotation layer draws (ellipse, line, text).
//
// Supported shapes: circle, ellipse, annulus (and multi-ring ellipse/box) as
// concentric outlines; box, polygon, segment as line segments (exact
// geometry, including the rotation); line, vector, ruler, projection as a
// line; point as a small circle; text. Unsupported and counted: panda,
// epanda, bpanda, compass, composite.
//
// Coordinate systems: image and physical (1-based FITS pixels; physical is
// read as image — LTV/LTM offsets are not applied), fk5 / j2000 / icrs / wcs
// and galactic (through the image's plate solution). fk4/b1950 and ecliptic
// are counted as unsupported rather than approximated.
//
// Values: decimal degrees, sexagesimal (hh:mm:ss longitude in the equatorial
// systems, dd:mm:ss otherwise), 12h34m56s / +12d34m56s, and the size units
// " ' d r p i. A size without unit is degrees in a sky system and pixels in
// image coordinates, as in DS9.
//
// Angles: in image coordinates, counter-clockwise from +X in the FITS frame.
// In a sky system the shape is laid out ON THE SKY — the angle is measured
// from west toward north, DS9's convention for a north-up, east-left display
// — and every vertex is then carried through the plate solution, so rotated
// and mirrored images come out right without a parity special case.
//
#include "core/Wcs.h"
#include <QString>
#include <QStringList>
#include <vector>

namespace astro {

// One primitive in 0-based image pixels — same fields as an Annotation.
struct Ds9Shape {
    enum class Kind { Ellipse, Line, Text };
    Kind    kind = Kind::Ellipse;
    double  x = 0, y = 0;            // centre (Ellipse/Text) or first endpoint (Line)
    double  x2 = 0, y2 = 0;          // second endpoint (Line)
    double  a = 0, b = 0;            // semi-axes (Ellipse), px
    double  angleDeg = 0;            // direction of the a-axis, atan2(dy, dx) in pixel coords
    QString text;                    // label (Ellipse/Line) or content (Text)
    QString color;                   // "#rrggbb"; empty = the importer's default
    double  textSize = 0;            // points; 0 = default
    bool    sky = false;             // resolved through the plate solution (current
                                     // view frame) rather than given in disk pixels
};

struct Ds9ImportStats {
    int regions = 0;                 // region statements turned into shapes
    int shapes = 0;                  // primitives emitted
    int needWcs = 0;                 // skipped: sky coordinates/sizes, no plate solution
    int unsupported = 0;             // skipped: shape or coordinate system not handled
    int malformed = 0;               // skipped: could not be parsed
    int offImage = 0;                // skipped: sky position not projectable
    QStringList unsupportedKinds;    // distinct names, for the report
};

// Parse region-file text. `wcs` may be null or invalid (sky regions are then
// counted in needWcs). `pointRadiusPx` sizes `point` regions.
std::vector<Ds9Shape> parseDs9Regions(const QString& text, const Wcs* wcs,
                                      double pointRadiusPx,
                                      Ds9ImportStats* stats = nullptr);

// Read a file and parse it; an unreadable file sets *err and returns nothing.
std::vector<Ds9Shape> loadDs9Regions(const QString& path, const Wcs* wcs,
                                     double pointRadiusPx,
                                     Ds9ImportStats* stats = nullptr,
                                     QString* err = nullptr);

} // namespace astro

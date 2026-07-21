#pragma once
//
// Wcs — a linear World Coordinate System solution (gnomonic / TAN projection),
// parsed from the shared ImageHeader. Covers the solutions written by
// PixInsight's ImageSolver, ASTAP, astrometry.net, etc., which store the
// standard FITS keywords (CRVALn, CRPIXn, CDn_m — with PC/CDELT and
// CDELT+CROTA2 fallbacks). XISF carries the same keywords embedded, so both
// container formats share this parser.
//
// Spline ("distortion corrected") PixInsight solutions additionally store
// PCL:AstrometricSolution:* properties; the linear keywords they also write are
// used here as the (very close) approximation.
//
#include "core/ImageHeader.h"
#include <QString>

namespace astro {

class Wcs {
public:
    static Wcs fromHeader(const ImageHeader& h);

    bool valid() const { return m_valid; }

    // 0-based pixel coordinates (as used by the hover readout; FITS CRPIX is
    // 1-based and this converts internally). Returns false when !valid().
    bool pixelToSky(double x, double y, double& raDeg, double& decDeg) const;
    // Inverse: RA/Dec (degrees) → 0-based pixel. Returns false when !valid()
    // or the point is on the far hemisphere. Used by the grid overlay.
    bool skyToPixel(double raDeg, double decDeg, double& x, double& y) const;

    // Rebase the solution onto a rotated/flipped image. w/h are the image
    // dimensions BEFORE the transform (0-based pixel-centre convention, same as
    // the annotation transform).
    enum class PixelXform { RotCW, RotCCW, FlipH, FlipV };
    Wcs transformed(PixelXform op, int w, int h) const;

    double pixelScaleArcsec() const;   // mean scale, arcsec/pixel
    double rotationDeg() const;        // position angle of +Y axis, approx.

    static QString formatRa(double raDeg);    // "HH:MM:SS.ss"
    static QString formatDec(double decDeg);  // "±DD°MM′SS.s″"

    // Parse telescope-pointing keywords (RA/DEC as degrees, or OBJCTRA/OBJCTDEC
    // as sexagesimal "HH MM SS.s" / "±DD MM SS"). These are NOT a plate
    // solution — just one coordinate for the frame — but worth displaying.
    static bool parsePointing(const ImageHeader& h, double& raDeg, double& decDeg);

    QString summary() const;           // "1.21″/px · rotation −12.3° · TAN"

private:
    static Wcs fromFitsKeywords(const ImageHeader& h);
    static Wcs fromPclProperties(const ImageHeader& h);

    bool   m_valid = false;
    double m_crval1 = 0, m_crval2 = 0;   // reference RA/Dec, degrees
    double m_crpix1 = 0, m_crpix2 = 0;   // reference pixel, 1-based
    double m_cd11 = 0, m_cd12 = 0, m_cd21 = 0, m_cd22 = 0;   // deg/pixel
};

} // namespace astro

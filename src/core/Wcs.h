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

    double pixelScaleArcsec() const;   // mean scale, arcsec/pixel
    double rotationDeg() const;        // position angle of +Y axis, approx.

    static QString formatRa(double raDeg);    // "HH:MM:SS.ss"
    static QString formatDec(double decDeg);  // "±DD°MM′SS.s″"

    QString summary() const;           // "1.21″/px · rotation −12.3° · TAN"

private:
    bool   m_valid = false;
    double m_crval1 = 0, m_crval2 = 0;   // reference RA/Dec, degrees
    double m_crpix1 = 0, m_crpix2 = 0;   // reference pixel, 1-based
    double m_cd11 = 0, m_cd12 = 0, m_cd21 = 0, m_cd22 = 0;   // deg/pixel
};

} // namespace astro

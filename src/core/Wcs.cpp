#include "core/Wcs.h"
#include <QtMath>
#include <QRegularExpression>
#include <cmath>

namespace astro {

static bool numOf(const ImageHeader& h, const char* key, double& out) {
    QString v = h.valueOf(QLatin1String(key)).trimmed();
    if (v.startsWith('\'') && v.endsWith('\'') && v.size() >= 2)
        v = v.mid(1, v.size() - 2).trimmed();      // defensively unquote
    bool ok = false;
    const double d = v.toDouble(&ok);
    if (ok) out = d;
    return ok;
}

Wcs Wcs::fromFitsKeywords(const ImageHeader& h) {
    Wcs w;

    // Projection: accept RA---TAN (and TAN-SIP / TPV, whose linear part is what
    // we parse). Anything else (SIN, ARC, ...) is rejected rather than misread.
    const QString ctype1 = h.valueOf(QStringLiteral("CTYPE1")).toUpper();
    if (!ctype1.contains(QLatin1String("RA")) || !ctype1.contains(QLatin1String("TAN")))
        return w;

    if (!numOf(h, "CRVAL1", w.m_crval1) || !numOf(h, "CRVAL2", w.m_crval2) ||
        !numOf(h, "CRPIX1", w.m_crpix1) || !numOf(h, "CRPIX2", w.m_crpix2))
        return w;

    // Linear transform, in order of preference:
    //   1. CD matrix        (PixInsight, astrometry.net)
    //   2. PC matrix × CDELT
    //   3. CDELT + CROTA2   (legacy)
    if (numOf(h, "CD1_1", w.m_cd11)) {
        numOf(h, "CD1_2", w.m_cd12);
        numOf(h, "CD2_1", w.m_cd21);
        if (!numOf(h, "CD2_2", w.m_cd22)) return w;
    } else {
        double cdelt1 = 0, cdelt2 = 0;
        if (!numOf(h, "CDELT1", cdelt1) || !numOf(h, "CDELT2", cdelt2)) return w;
        double pc11 = 1, pc12 = 0, pc21 = 0, pc22 = 1;
        if (numOf(h, "PC1_1", pc11)) {
            numOf(h, "PC1_2", pc12);
            numOf(h, "PC2_1", pc21);
            numOf(h, "PC2_2", pc22);
        } else {
            double crota2 = 0;
            numOf(h, "CROTA2", crota2);
            const double cr = std::cos(qDegreesToRadians(crota2));
            const double sr = std::sin(qDegreesToRadians(crota2));
            pc11 = cr; pc12 = -sr; pc21 = sr; pc22 = cr;
        }
        w.m_cd11 = cdelt1 * pc11; w.m_cd12 = cdelt1 * pc12;
        w.m_cd21 = cdelt2 * pc21; w.m_cd22 = cdelt2 * pc22;
    }

    if (std::fabs(w.m_cd11 * w.m_cd22 - w.m_cd12 * w.m_cd21) < 1e-20) return w;
    w.m_valid = true;
    return w;
}

// PixInsight XISF: PCL:AstrometricSolution:* typed properties, surfaced by the
// XISF reader's XML-header scan. Vectors arrive serialized as "a, b" and
// matrices as "[2×2] a, b, c, d" — parse those strings.
static bool propVec(const ImageHeader& h, const char* id, int n, double* out) {
    QString v = h.properties.value(QLatin1String(id)).toString();
    if (v.isEmpty()) return false;
    if (v.startsWith(QLatin1Char('['))) {          // strip "[R×C] " dimension prefix
        const int br = v.indexOf(QLatin1Char(']'));
        if (br < 0) return false;
        v = v.mid(br + 1);
    }
    // libXISF serializes vectors as {a,b} and matrices as {{a,b},{c,d}} — strip
    // the braces so both spellings (and our XML-scan "a, b" form) parse alike.
    v.remove(QLatin1Char('{'));
    v.remove(QLatin1Char('}'));
    const QStringList parts = v.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() < n) return false;
    for (int i = 0; i < n; ++i) {
        bool ok = false;
        out[i] = parts.at(i).trimmed().toDouble(&ok);
        if (!ok) return false;
    }
    return true;
}

Wcs Wcs::fromPclProperties(const ImageHeader& h) {
    Wcs w;
    // Only the gnomonic (TAN) projection is handled; PI names it explicitly.
    const QString proj = h.properties.value(
        QStringLiteral("PCL:AstrometricSolution:ProjectionSystem")).toString();
    if (!proj.isEmpty() && !proj.contains(QLatin1String("Gnomonic"), Qt::CaseInsensitive))
        return w;

    double rc[2], ic[2], m[4];
    if (!propVec(h, "PCL:AstrometricSolution:ReferenceCelestialCoordinates", 2, rc)) return w;
    if (!propVec(h, "PCL:AstrometricSolution:ReferenceImageCoordinates",     2, ic)) return w;
    if (!propVec(h, "PCL:AstrometricSolution:LinearTransformationMatrix",    4, m))  return w;

    w.m_crval1 = rc[0]; w.m_crval2 = rc[1];        // degrees
    // PI image coordinates share our top-left origin; pixelToSky() subtracts a
    // 1-based FITS-style reference, so offset by one to reuse the same math.
    w.m_crpix1 = ic[0] + 1.0;
    w.m_crpix2 = ic[1] + 1.0;
    w.m_cd11 = m[0]; w.m_cd12 = m[1];              // row-major 2×2, deg/pixel
    w.m_cd21 = m[2]; w.m_cd22 = m[3];
    if (std::fabs(w.m_cd11 * w.m_cd22 - w.m_cd12 * w.m_cd21) < 1e-20) return w;
    w.m_valid = true;
    return w;
}

Wcs Wcs::fromHeader(const ImageHeader& h) {
    // FITS WCS keywords first (both containers can carry them); fall back to
    // PixInsight's structured astrometric-solution properties (XISF).
    Wcs w = fromFitsKeywords(h);
    if (!w.valid()) w = fromPclProperties(h);
    return w;
}

bool Wcs::pixelToSky(double x, double y, double& raDeg, double& decDeg) const {
    if (!m_valid) return false;
    // FITS pixels are 1-based with the reference at the pixel CENTRE.
    const double dx = (x + 1.0) - m_crpix1;
    const double dy = (y + 1.0) - m_crpix2;

    // Intermediate world coordinates (gnomonic tangent-plane offsets), radians.
    const double xi  = qDegreesToRadians(m_cd11 * dx + m_cd12 * dy);
    const double eta = qDegreesToRadians(m_cd21 * dx + m_cd22 * dy);

    const double ra0  = qDegreesToRadians(m_crval1);
    const double dec0 = qDegreesToRadians(m_crval2);
    const double cosd = std::cos(dec0), sind = std::sin(dec0);

    // Inverse gnomonic projection.
    const double denom = cosd - eta * sind;
    const double ra  = ra0 + std::atan2(xi, denom);
    const double dec = std::atan2(sind + eta * cosd,
                                  std::sqrt(xi * xi + denom * denom));

    raDeg = qRadiansToDegrees(ra);
    while (raDeg < 0)     raDeg += 360.0;
    while (raDeg >= 360)  raDeg -= 360.0;
    decDeg = qRadiansToDegrees(dec);
    return true;
}

bool Wcs::skyToPixel(double raDeg, double decDeg, double& x, double& y) const {
    if (!m_valid) return false;
    const double ra   = qDegreesToRadians(raDeg);
    const double dec  = qDegreesToRadians(decDeg);
    const double ra0  = qDegreesToRadians(m_crval1);
    const double dec0 = qDegreesToRadians(m_crval2);
    const double cosd = std::cos(dec0), sind = std::sin(dec0);
    const double dra  = ra - ra0;

    // Forward gnomonic projection to tangent-plane offsets (radians).
    const double cosc = sind * std::sin(dec) + cosd * std::cos(dec) * std::cos(dra);
    if (cosc <= 1e-9) return false;                       // far hemisphere
    const double xi  = std::cos(dec) * std::sin(dra) / cosc;
    const double eta = (cosd * std::sin(dec) - sind * std::cos(dec) * std::cos(dra)) / cosc;

    // Invert the 2×2 CD matrix (degrees/pixel).
    const double det = m_cd11 * m_cd22 - m_cd12 * m_cd21;
    if (std::fabs(det) < 1e-20) return false;
    const double xiD = qRadiansToDegrees(xi), etaD = qRadiansToDegrees(eta);
    const double dx = ( m_cd22 * xiD - m_cd12 * etaD) / det;
    const double dy = (-m_cd21 * xiD + m_cd11 * etaD) / det;
    x = m_crpix1 + dx - 1.0;                              // back to 0-based
    y = m_crpix2 + dy - 1.0;
    return true;
}

Wcs Wcs::transformed(PixelXform op, int w, int h) const {
    Wcs t = *this;
    if (!m_valid) return t;
    // 0-based reference pixel.
    const double cx = m_crpix1 - 1.0, cy = m_crpix2 - 1.0;
    double nx = cx, ny = cy;
    // CD' = CD · J, where J = d(old pixel)/d(new pixel).
    switch (op) {
        case PixelXform::RotCW:                       // x' = (h-1)-y, y' = x
            nx = (h - 1) - cy; ny = cx;
            t.m_cd11 = -m_cd12; t.m_cd12 = m_cd11;    // J = [[0,1],[-1,0]]
            t.m_cd21 = -m_cd22; t.m_cd22 = m_cd21;
            break;
        case PixelXform::RotCCW:                      // x' = y, y' = (w-1)-x
            nx = cy; ny = (w - 1) - cx;
            t.m_cd11 = m_cd12;  t.m_cd12 = -m_cd11;   // J = [[0,-1],[1,0]]
            t.m_cd21 = m_cd22;  t.m_cd22 = -m_cd21;
            break;
        case PixelXform::FlipH:                       // x' = (w-1)-x
            nx = (w - 1) - cx;
            t.m_cd11 = -m_cd11; t.m_cd21 = -m_cd21;   // J = [[-1,0],[0,1]]
            break;
        case PixelXform::FlipV:                       // y' = (h-1)-y
            ny = (h - 1) - cy;
            t.m_cd12 = -m_cd12; t.m_cd22 = -m_cd22;   // J = [[1,0],[0,-1]]
            break;
    }
    t.m_crpix1 = nx + 1.0;
    t.m_crpix2 = ny + 1.0;
    return t;
}

double Wcs::pixelScaleArcsec() const {
    if (!m_valid) return 0.0;
    return std::sqrt(std::fabs(m_cd11 * m_cd22 - m_cd12 * m_cd21)) * 3600.0;
}

double Wcs::rotationDeg() const {
    if (!m_valid) return 0.0;
    return qRadiansToDegrees(std::atan2(m_cd21, m_cd11));
}

QString Wcs::formatRa(double raDeg) {
    double hours = raDeg / 15.0;
    int hh = int(hours);
    double mf = (hours - hh) * 60.0;
    int mm = int(mf);
    double ss = (mf - mm) * 60.0;
    if (ss >= 59.995) { ss = 0; if (++mm == 60) { mm = 0; hh = (hh + 1) % 24; } }
    return QStringLiteral("%1:%2:%3")
        .arg(hh, 2, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 5, 'f', 2, QLatin1Char('0'));
}

QString Wcs::formatDec(double decDeg) {
    const QChar sign = decDeg < 0 ? QLatin1Char('-') : QLatin1Char('+');
    double a = std::fabs(decDeg);
    int dd = int(a);
    double mf = (a - dd) * 60.0;
    int mm = int(mf);
    double ss = (mf - mm) * 60.0;
    if (ss >= 59.95) { ss = 0; if (++mm == 60) { mm = 0; ++dd; } }
    return QStringLiteral("%1%2\u00b0%3\u2032%4\u2033")
        .arg(sign)
        .arg(dd, 2, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 4, 'f', 1, QLatin1Char('0'));
}

// "13 29 52.70" / "13:29:52.7" → degrees (factor 15 for RA hours). Sign-aware.
static bool parseSexagesimal(QString s, double factor, double& outDeg) {
    s = s.trimmed();
    if (s.startsWith('\'') && s.endsWith('\'') && s.size() >= 2) s = s.mid(1, s.size() - 2).trimmed();
    if (s.isEmpty()) return false;
    double sign = 1.0;
    if (s.startsWith('-')) { sign = -1.0; s.remove(0, 1); }
    else if (s.startsWith('+')) s.remove(0, 1);
    const QStringList parts = s.split(QRegularExpression(QStringLiteral("[\\s:]+")), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return false;
    bool ok = false;
    double val = 0, div = 1;
    for (const QString& p : parts) {
        const double x = p.toDouble(&ok);
        if (!ok) return false;
        val += x / div;
        div *= 60.0;
    }
    outDeg = sign * val * factor;
    return true;
}

bool Wcs::parsePointing(const ImageHeader& h, double& raDeg, double& decDeg) {
    // Preferred: RA/DEC as decimal degrees (NINA, SGP, many drivers).
    double ra = 0, dec = 0;
    if (numOf(h, "RA", ra) && numOf(h, "DEC", dec)) { raDeg = ra; decDeg = dec; return true; }
    // Sexagesimal variants: RA/DEC or OBJCTRA/OBJCTDEC as "HH MM SS" / "±DD MM SS".
    const QString raS  = !h.valueOf(QStringLiteral("OBJCTRA")).isEmpty()
                             ? h.valueOf(QStringLiteral("OBJCTRA"))  : h.valueOf(QStringLiteral("RA"));
    const QString decS = !h.valueOf(QStringLiteral("OBJCTDEC")).isEmpty()
                             ? h.valueOf(QStringLiteral("OBJCTDEC")) : h.valueOf(QStringLiteral("DEC"));
    if (raS.isEmpty() || decS.isEmpty()) return false;
    return parseSexagesimal(raS, 15.0, raDeg) && parseSexagesimal(decS, 1.0, decDeg);
}

QString Wcs::summary() const {
    if (!m_valid) return {};
    double ra = 0, dec = 0;
    pixelToSky(m_crpix1 - 1.0, m_crpix2 - 1.0, ra, dec);   // reference pixel centre
    return QStringLiteral("%1\u2033/px \u00b7 rotation %2\u00b0 \u00b7 TAN \u00b7 ref %3 %4")
        .arg(pixelScaleArcsec(), 0, 'f', 2)
        .arg(rotationDeg(), 0, 'f', 1)
        .arg(formatRa(ra), formatDec(dec));
}

} // namespace astro

// WCS tests: TAN projection round-trips, keyword fallbacks, the PCL property
// path, transform rebasing, formatting and pointing parse — the astrometry
// behind the hover readout and grid overlay.
#include "nstest.h"
#include "core/Wcs.h"
#include "core/ImageHeader.h"
#include <cmath>

using namespace astro;

static void addNum(ImageHeader& h, const char* k, double v) {
    h.cards.push_back({ QString::fromLatin1(k), QString::number(v, 'g', 15), QString() });
}

// North-up TAN solution: scale arcsec/px, reference pixel (1-based) at (px,py).
static ImageHeader tanHeader(double ra0, double dec0, double px, double py,
                             double scaleArcsec) {
    const double s = scaleArcsec / 3600.0;
    ImageHeader h;
    h.cards.push_back({ "CTYPE1", "'RA---TAN'", QString() });
    h.cards.push_back({ "CTYPE2", "'DEC--TAN'", QString() });
    addNum(h, "CRVAL1", ra0); addNum(h, "CRVAL2", dec0);
    addNum(h, "CRPIX1", px);  addNum(h, "CRPIX2", py);
    addNum(h, "CD1_1", -s); addNum(h, "CD1_2", 0.0);
    addNum(h, "CD2_1", 0.0); addNum(h, "CD2_2", s);
    return h;
}

NS_TEST(wcs_reference_pixel_is_crval) {
    Wcs w = Wcs::fromHeader(tanHeader(180.0, 45.0, 512.0, 384.0, 1.5));
    NS_CHECK(w.valid());
    double ra = 0, dec = 0;
    // CRPIX is 1-based; the API takes 0-based pixels.
    NS_CHECK(w.pixelToSky(511.0, 383.0, ra, dec));
    NS_CHECK_NEAR(ra, 180.0, 1e-9);
    NS_CHECK_NEAR(dec, 45.0, 1e-9);
    NS_CHECK_NEAR(w.pixelScaleArcsec(), 1.5, 1e-6);
}

NS_TEST(wcs_sky_pixel_roundtrip) {
    Wcs w = Wcs::fromHeader(tanHeader(10.0, -30.0, 100.0, 200.0, 2.0));
    NS_CHECK(w.valid());
    for (double x : { 0.0, 57.0, 511.5, 1023.0 })
        for (double y : { 0.0, 33.0, 767.9 }) {
            double ra, dec, bx, by;
            NS_CHECK(w.pixelToSky(x, y, ra, dec));
            NS_CHECK(w.skyToPixel(ra, dec, bx, by));
            NS_CHECK_NEAR(bx, x, 1e-6);
            NS_CHECK_NEAR(by, y, 1e-6);
        }
}

NS_TEST(wcs_rejects_non_tan_and_garbage) {
    ImageHeader h = tanHeader(180, 45, 1, 1, 1.0);
    h.cards[0].value = "'RA---SIN'";                  // orthographic: refuse
    NS_CHECK(!Wcs::fromHeader(h).valid());
    NS_CHECK(!Wcs::fromHeader(ImageHeader{}).valid());
    ImageHeader d = tanHeader(180, 45, 1, 1, 1.0);    // degenerate CD matrix
    d.cards[6].value = "0"; d.cards[9].value = "0";   // CD1_1 = CD2_2 = 0
    NS_CHECK(!Wcs::fromHeader(d).valid());
}

NS_TEST(wcs_cdelt_crota_fallback) {
    // Legacy CDELT+CROTA2 spelling must give a valid, round-tripping solution.
    ImageHeader h;
    h.cards.push_back({ "CTYPE1", "RA---TAN", QString() });
    addNum(h, "CRVAL1", 83.8); addNum(h, "CRVAL2", -5.4);   // M42
    addNum(h, "CRPIX1", 320.0); addNum(h, "CRPIX2", 240.0);
    addNum(h, "CDELT1", -2.5 / 3600.0); addNum(h, "CDELT2", 2.5 / 3600.0);
    addNum(h, "CROTA2", 33.0);
    Wcs w = Wcs::fromHeader(h);
    NS_CHECK(w.valid());
    NS_CHECK_NEAR(w.pixelScaleArcsec(), 2.5, 1e-6);
    double ra, dec, bx, by;
    NS_CHECK(w.pixelToSky(10.0, 20.0, ra, dec));
    NS_CHECK(w.skyToPixel(ra, dec, bx, by));
    NS_CHECK_NEAR(bx, 10.0, 1e-6);
    NS_CHECK_NEAR(by, 20.0, 1e-6);
}

NS_TEST(wcs_pcl_properties_match_fits_keywords) {
    // The same solution through both parsers must agree (PI's image
    // coordinates are 0-based top-left; FITS CRPIX is 1-based).
    const double s = 1.2 / 3600.0;
    ImageHeader fits = tanHeader(210.5, 54.3, 400.5, 300.5, 1.2);
    ImageHeader pcl;
    pcl.properties["PCL:AstrometricSolution:ProjectionSystem"] = "Gnomonic";
    pcl.properties["PCL:AstrometricSolution:ReferenceCelestialCoordinates"] = "210.5, 54.3";
    pcl.properties["PCL:AstrometricSolution:ReferenceImageCoordinates"] = "399.5, 299.5";
    pcl.properties["PCL:AstrometricSolution:LinearTransformationMatrix"] =
        QStringLiteral("[2×2] %1, 0, 0, %2").arg(-s, 0, 'g', 15).arg(s, 0, 'g', 15);
    Wcs a = Wcs::fromHeader(fits), b = Wcs::fromHeader(pcl);
    NS_CHECK(a.valid() && b.valid());
    double ra1, dec1, ra2, dec2;
    NS_CHECK(a.pixelToSky(123.0, 456.0, ra1, dec1));
    NS_CHECK(b.pixelToSky(123.0, 456.0, ra2, dec2));
    NS_CHECK_NEAR(ra1, ra2, 1e-9);
    NS_CHECK_NEAR(dec1, dec2, 1e-9);
}

NS_TEST(wcs_transform_inverses) {
    // RotCW∘RotCCW and FlipH∘FlipH must restore the original sky mapping.
    const int W = 640, H = 480;
    Wcs w = Wcs::fromHeader(tanHeader(11.1, 22.2, 100.0, 120.0, 1.0));
    Wcs back1 = w.transformed(Wcs::PixelXform::RotCW, W, H)
                 .transformed(Wcs::PixelXform::RotCCW, H, W);
    Wcs back2 = w.transformed(Wcs::PixelXform::FlipH, W, H)
                 .transformed(Wcs::PixelXform::FlipH, W, H);
    for (const Wcs* b : { &back1, &back2 }) {
        NS_CHECK(b->valid());
        double ra, dec, r2, d2;
        NS_CHECK(w.pixelToSky(50.0, 60.0, ra, dec));
        NS_CHECK(b->pixelToSky(50.0, 60.0, r2, d2));
        NS_CHECK_NEAR(ra, r2, 1e-9);
        NS_CHECK_NEAR(dec, d2, 1e-9);
    }
    // A transform must not change the pixel scale.
    NS_CHECK_NEAR(w.transformed(Wcs::PixelXform::RotCW, W, H).pixelScaleArcsec(),
                  w.pixelScaleArcsec(), 1e-9);
}

NS_TEST(wcs_arbitrary_rotation_keeps_centre) {
    // rotated() spins about the image centre onto an expanded canvas: the sky
    // coordinate at the centre must be unchanged.
    const int W = 200, H = 100, NW = 220, NH = 180;
    Wcs w = Wcs::fromHeader(tanHeader(150.0, 2.5, 90.0, 40.0, 1.0));
    Wcs r = w.rotated(37.0, W, H, NW, NH);
    NS_CHECK(r.valid());
    double ra1, dec1, ra2, dec2;
    NS_CHECK(w.pixelToSky((W - 1) / 2.0, (H - 1) / 2.0, ra1, dec1));
    NS_CHECK(r.pixelToSky((NW - 1) / 2.0, (NH - 1) / 2.0, ra2, dec2));
    NS_CHECK_NEAR(ra1, ra2, 1e-6);
    NS_CHECK_NEAR(dec1, dec2, 1e-6);
}

NS_TEST(wcs_formatting) {
    NS_CHECK(Wcs::formatRa(0.0).startsWith("00:00:00"));
    NS_CHECK(Wcs::formatRa(187.5).startsWith("12:30:00"));
    const QString d = Wcs::formatDec(-45.25);
    NS_CHECK(d.contains("45") && d.contains("15"));
    NS_CHECK(d.startsWith(QChar(0x2212)) || d.startsWith('-'));  // − or -
}

NS_TEST(wcs_pointing_parse) {
    ImageHeader sexa;
    sexa.cards.push_back({ "OBJCTRA",  "'12 30 00'",  QString() });
    sexa.cards.push_back({ "OBJCTDEC", "'-45 15 00'", QString() });
    double ra = 0, dec = 0;
    NS_CHECK(Wcs::parsePointing(sexa, ra, dec));
    NS_CHECK_NEAR(ra, 187.5, 1e-6);
    NS_CHECK_NEAR(dec, -45.25, 1e-6);

    ImageHeader deg;
    deg.cards.push_back({ "RA",  "10.5",   QString() });
    deg.cards.push_back({ "DEC", "-20.25", QString() });
    NS_CHECK(Wcs::parsePointing(deg, ra, dec));
    NS_CHECK_NEAR(ra, 10.5, 1e-6);
    NS_CHECK_NEAR(dec, -20.25, 1e-6);
}

int main() { return nstest::runAll(); }

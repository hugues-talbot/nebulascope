// DS9 region import: the parser and its geometry, against hand-computed
// truth — pixel conventions, units, the sky angle convention under both
// image parities, galactic coordinates, and the honest skip counts.
#include "nstest.h"
#include "core/Ds9Regions.h"
#include "core/ImageHeader.h"
#include "core/Wcs.h"
#include <cmath>

using namespace astro;

static void addNum(ImageHeader& h, const char* k, double v) {
    h.cards.push_back({ QString::fromLatin1(k), QString::number(v, 'g', 15), QString() });
}

// TAN solution with reference pixel (1-based) at (px,py). mirrored = east
// toward +x (the parity of an image flipped left-right).
static Wcs tanWcs(double ra0, double dec0, double px, double py, double scaleArcsec,
                  bool mirrored = false) {
    const double s = scaleArcsec / 3600.0;
    ImageHeader h;
    h.cards.push_back({ "CTYPE1", "'RA---TAN'", QString() });
    h.cards.push_back({ "CTYPE2", "'DEC--TAN'", QString() });
    addNum(h, "CRVAL1", ra0); addNum(h, "CRVAL2", dec0);
    addNum(h, "CRPIX1", px);  addNum(h, "CRPIX2", py);
    addNum(h, "CD1_1", mirrored ? s : -s); addNum(h, "CD1_2", 0.0);
    addNum(h, "CD2_1", 0.0); addNum(h, "CD2_2", s);
    return Wcs::fromHeader(h);
}

// Angle difference modulo 180 degrees (an ellipse axis has no direction).
static double axisDiff(double a, double b) {
    double d = std::fmod(a - b, 180.0);
    if (d < -90) d += 180;
    if (d > 90) d -= 180;
    return std::fabs(d);
}

NS_TEST(ds9_image_coordinates) {
    const QString reg = QStringLiteral(
        "# Region file format: DS9 version 4.1\n"
        "global color=green dashlist=8 3 width=1 font=\"helvetica 10 normal roman\" select=1\n"
        "image\n"
        "circle(101,201,20) # color=red text={M 31; core}\n"
        "ellipse(101,201,30,10,30)\n"
        "box(101,201,40,20,0)\n"
        "polygon(11,11,21,11,21,21)\n"
        "line(1,1,11,21) # line=0 0\n"
        "# vector(101,201,50,90) vector=1\n"
        "# text(51,61) text={hello world} color=#ff8800 font=\"times 14 bold\"\n"
        "point(5,6) # point=cross\n"
        "-circle(301,301,5)\n"
        "# circle of confusion, a plain comment\n");
    Ds9ImportStats st;
    const std::vector<Ds9Shape> s = parseDs9Regions(reg, nullptr, 3.0, &st);
    NS_CHECK(st.regions == 9);
    NS_CHECK(st.malformed == 0 && st.needWcs == 0 && st.unsupported == 0);
    // circle 1, ellipse 1, box 4, polygon 3, line 1, vector 1, text 1, point 1, circle 1
    NS_CHECK(st.shapes == 14 && s.size() == 14);

    // circle: FITS 1-based -> 0-based, per-region colour and label (with ';').
    NS_CHECK(s[0].kind == Ds9Shape::Kind::Ellipse);
    NS_CHECK_NEAR(s[0].x, 100.0, 1e-9); NS_CHECK_NEAR(s[0].y, 200.0, 1e-9);
    NS_CHECK_NEAR(s[0].a, 20.0, 1e-9);  NS_CHECK_NEAR(s[0].b, 20.0, 1e-9);
    NS_CHECK(s[0].color == QLatin1String("#ff0000"));
    NS_CHECK(s[0].text == QLatin1String("M 31; core"));
    NS_CHECK(!s[0].sky);
    // ellipse: angle CCW from +X in FITS (X,Y) = the direction in pixel axes.
    NS_CHECK_NEAR(s[1].a, 30.0, 1e-9); NS_CHECK_NEAR(s[1].b, 10.0, 1e-9);
    NS_CHECK_NEAR(s[1].angleDeg, 30.0, 1e-9);
    NS_CHECK(s[1].color == QLatin1String("#00ff00"));      // the global default
    NS_CHECK_NEAR(s[1].textSize, 10.0, 1e-9);
    // box: four segments around (100,200), half-sizes 20 x 10.
    double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (int i = 2; i < 6; ++i) {
        NS_CHECK(s[i].kind == Ds9Shape::Kind::Line);
        for (double x : { s[i].x, s[i].x2 }) { minx = std::min(minx, x); maxx = std::max(maxx, x); }
        for (double y : { s[i].y, s[i].y2 }) { miny = std::min(miny, y); maxy = std::max(maxy, y); }
    }
    NS_CHECK_NEAR(minx, 80.0, 1e-9); NS_CHECK_NEAR(maxx, 120.0, 1e-9);
    NS_CHECK_NEAR(miny, 190.0, 1e-9); NS_CHECK_NEAR(maxy, 210.0, 1e-9);
    // polygon closes: last segment returns to the first vertex.
    NS_CHECK_NEAR(s[8].x, 20.0, 1e-9); NS_CHECK_NEAR(s[8].y, 20.0, 1e-9);
    NS_CHECK_NEAR(s[8].x2, 10.0, 1e-9); NS_CHECK_NEAR(s[8].y2, 10.0, 1e-9);
    // line
    NS_CHECK_NEAR(s[9].x2, 10.0, 1e-9); NS_CHECK_NEAR(s[9].y2, 20.0, 1e-9);
    // vector (comment form): 50 px at 90 degrees = +Y.
    NS_CHECK_NEAR(s[10].x2, 100.0, 1e-9); NS_CHECK_NEAR(s[10].y2, 250.0, 1e-9);
    // text (comment form), hex colour, font size.
    NS_CHECK(s[11].kind == Ds9Shape::Kind::Text);
    NS_CHECK(s[11].text == QLatin1String("hello world"));
    NS_CHECK(s[11].color == QLatin1String("#ff8800"));
    NS_CHECK_NEAR(s[11].textSize, 14.0, 1e-9);
    NS_CHECK_NEAR(s[11].x, 50.0, 1e-9); NS_CHECK_NEAR(s[11].y, 60.0, 1e-9);
    // point -> small circle of the requested radius; excluded region kept.
    NS_CHECK_NEAR(s[12].a, 3.0, 1e-9);
    NS_CHECK_NEAR(s[13].x, 300.0, 1e-9); NS_CHECK_NEAR(s[13].a, 5.0, 1e-9);
}

NS_TEST(ds9_sky_coordinates_and_units) {
    // 1.5"/px, north up, east left (in a y-up display): +x is west, +y north.
    const Wcs w = tanWcs(180.0, 45.0, 512.0, 384.0, 1.5);
    NS_CHECK(w.valid());
    const QString reg = QStringLiteral(
        "fk5\n"
        "circle(12:00:00.000,+45:00:00.00,15\")\n"
        "circle(12h00m00s,+45d00m00s,0.5')\n"
        "icrs;circle(180.0,45.0,0.0125)\n"                 // degrees by default
        "fk5;ellipse(180.0,45.0,30\",15\",30)\n"
        "ellipse(180.0,45.0,20p,10p,0)\n"
        "line(180.0,45.0,180.0,45.0125)\n"
        "vector(180.0,45.0,30\",90)\n");
    Ds9ImportStats st;
    const std::vector<Ds9Shape> s = parseDs9Regions(reg, &w, 3.0, &st);
    NS_CHECK(st.regions == 7 && s.size() == 7);
    for (const Ds9Shape& q : s) NS_CHECK(q.sky);
    // The reference pixel (1-based 512,384) is 0-based (511,383).
    NS_CHECK_NEAR(s[0].x, 511.0, 1e-6); NS_CHECK_NEAR(s[0].y, 383.0, 1e-6);
    NS_CHECK_NEAR(s[0].a, 10.0, 1e-3);                     // 15" / 1.5"
    NS_CHECK_NEAR(s[1].a, 20.0, 1e-3);                     // 0.5' = 30"
    NS_CHECK_NEAR(s[2].a, 30.0, 1e-3);                     // 0.0125 deg = 45"
    // Sky angle 30: from west (+x here) toward north (+y) -> +30 in pixel axes.
    NS_CHECK_NEAR(s[3].a, 20.0, 1e-3); NS_CHECK_NEAR(s[3].b, 10.0, 1e-3);
    NS_CHECK(axisDiff(s[3].angleDeg, 30.0) < 1e-3);
    // Pixel-unit radii in a sky system.
    NS_CHECK_NEAR(s[4].a, 20.0, 1e-6); NS_CHECK_NEAR(s[4].b, 10.0, 1e-6);
    NS_CHECK(axisDiff(s[4].angleDeg, 0.0) < 1e-3);
    // Line to 45" due north = +30 px in y.
    NS_CHECK_NEAR(s[5].x2, 511.0, 1e-3); NS_CHECK_NEAR(s[5].y2, 413.0, 1e-2);
    // Vector at sky angle 90 = north.
    NS_CHECK_NEAR(s[6].x2, 511.0, 1e-3); NS_CHECK_NEAR(s[6].y2, 403.0, 1e-2);

    // Mirrored image (east toward +x): the SAME sky ellipse must come out
    // mirrored in pixels — west is now -x, so the axis sits at 150 = -30.
    const Wcs m = tanWcs(180.0, 45.0, 512.0, 384.0, 1.5, true);
    const std::vector<Ds9Shape> sm = parseDs9Regions(
        QStringLiteral("fk5;ellipse(180.0,45.0,30\",15\",30)"), &m, 3.0, &st);
    NS_CHECK(sm.size() == 1);
    NS_CHECK(axisDiff(sm[0].angleDeg, -30.0) < 1e-3);
    NS_CHECK_NEAR(sm[0].a, 20.0, 1e-3);

    // An angular size in image coordinates borrows the plate scale.
    const std::vector<Ds9Shape> si = parseDs9Regions(
        QStringLiteral("image;circle(10,10,30\")"), &w, 3.0, &st);
    NS_CHECK(si.size() == 1);
    NS_CHECK_NEAR(si[0].a, 20.0, 1e-6);
    NS_CHECK(!si[0].sky);
}

NS_TEST(ds9_galactic_and_skips) {
    // The galactic centre, l = b = 0, sits at ICRS 266.40499, -28.93617.
    const Wcs w = tanWcs(266.40499, -28.93617, 512.0, 384.0, 1.5);
    Ds9ImportStats st;
    const std::vector<Ds9Shape> g = parseDs9Regions(
        QStringLiteral("galactic\npoint(0,0)\n"), &w, 4.0, &st);
    NS_CHECK(g.size() == 1 && st.regions == 1);
    NS_CHECK(std::hypot(g[0].x - 511.0, g[0].y - 383.0) < 1.0);   // within 1.5"
    NS_CHECK_NEAR(g[0].a, 4.0, 1e-9);

    // No plate solution: sky regions are counted, image ones still import.
    const std::vector<Ds9Shape> n = parseDs9Regions(
        QStringLiteral("fk5\ncircle(180,45,10\")\nimage\ncircle(5,5,2)\ncircle(5,5,2\")\n"),
        nullptr, 3.0, &st);
    NS_CHECK(n.size() == 1);
    NS_CHECK(st.regions == 1 && st.needWcs == 2);

    // Unsupported shapes and systems are named, malformed lines counted.
    const std::vector<Ds9Shape> u = parseDs9Regions(
        QStringLiteral("image\npanda(1,1,0,360,4,10,20,1)\nfk4\ncircle(10,10,1)\n"
                       "image\ncircle(1,2)\nellipse(a,b,c,d,e)\ncircle(7,8,3)\n"),
        &w, 3.0, &st);
    NS_CHECK(u.size() == 1);
    NS_CHECK(st.unsupported == 2);
    NS_CHECK(st.unsupportedKinds.contains(QStringLiteral("panda")));
    NS_CHECK(st.unsupportedKinds.contains(QStringLiteral("fk4")));
    NS_CHECK(st.malformed == 2);

    // Old parenthesis-free syntax and an annulus as concentric circles.
    const std::vector<Ds9Shape> o = parseDs9Regions(
        QStringLiteral("image; circle 11 21 5 # color=cyan\nannulus(11,21,5,10,15)\n"),
        nullptr, 3.0, &st);
    NS_CHECK(o.size() == 4 && st.regions == 2);
    NS_CHECK(o[0].color == QLatin1String("#00ffff"));
    NS_CHECK_NEAR(o[3].a, 15.0, 1e-9);
}

int main() { return nstest::runAll(); }

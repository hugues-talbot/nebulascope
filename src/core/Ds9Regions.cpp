#include "core/Ds9Regions.h"
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace astro {

namespace {

constexpr double kDeg = M_PI / 180.0;

enum class Sys { Image, Equatorial, Galactic, Unsupported };

struct Val {
    double v = 0;
    QChar unit;                 // null, or one of d r p i " '
    bool ok = false;
    bool angular() const { return unit == 'd' || unit == 'r' || unit == '"' || unit == '\''; }
    bool pixel() const { return unit == 'p' || unit == 'i'; }
    double degrees() const {
        if (unit == 'r') return v / kDeg;
        if (unit == '"') return v / 3600.0;
        if (unit == '\'') return v / 60.0;
        return v;                                   // 'd' or none
    }
};

// A coordinate or size token. `lonHours`: a sexagesimal value is hours
// (equatorial longitude) rather than degrees.
Val parseVal(QString t, bool lonHours) {
    Val out;
    t = t.trimmed();
    if (t.isEmpty()) return out;
    bool ok = false;
    // 12h34m56.7s / +12d34m56.7s
    const int ih = t.indexOf('h'), im = t.indexOf('m');
    const int id = t.indexOf('d');
    if ((ih > 0 || id > 0) && im > 0) {
        const int i0 = ih > 0 ? ih : id;
        if (im > i0) {
            const bool neg = t.startsWith('-');
            const double a = std::fabs(t.left(i0).toDouble(&ok));
            if (!ok) return out;
            const double b = t.mid(i0 + 1, im - i0 - 1).toDouble(&ok);
            if (!ok) return out;
            QString rest = t.mid(im + 1);
            if (rest.endsWith('s')) rest.chop(1);
            const double c = rest.isEmpty() ? 0.0 : rest.toDouble(&ok);
            if (!ok) return out;
            out.v = (neg ? -1 : 1) * (a + b / 60.0 + c / 3600.0) * (ih > 0 ? 15.0 : 1.0);
            out.unit = 'd';
            out.ok = true;
            return out;
        }
    }
    if (t.contains(':')) {
        const QStringList p = t.split(':');
        if (p.size() < 2 || p.size() > 3) return out;
        const bool neg = p[0].trimmed().startsWith('-');
        double acc = 0, div = 1;
        for (const QString& s : p) {
            const double x = std::fabs(s.toDouble(&ok));
            if (!ok) return out;
            acc += x / div;
            div *= 60.0;
        }
        out.v = (neg ? -1 : 1) * acc * (lonHours ? 15.0 : 1.0);
        out.unit = 'd';
        out.ok = true;
        return out;
    }
    const QChar last = t.back();
    if (last == 'd' || last == 'r' || last == 'p' || last == 'i' || last == '"' || last == '\'') {
        out.unit = last;
        t.chop(1);
    }
    out.v = t.toDouble(&ok);
    out.ok = ok;
    return out;
}

// Galactic (l, b) -> ICRS (ra, dec), degrees (Hipparcos rotation, transposed).
void galToEq(double l, double b, double& ra, double& dec) {
    static const double R[3][3] = {
        { -0.0548755604162154, -0.8734370902348850, -0.4838350155487132 },
        { +0.4941094278755837, -0.4448296299600112, +0.7469822444972189 },
        { -0.8676661490190047, -0.1980763734312015, +0.4559837761750669 } };
    const double cl = std::cos(l * kDeg), sl = std::sin(l * kDeg);
    const double cb = std::cos(b * kDeg), sb = std::sin(b * kDeg);
    const double g[3] = { cb * cl, cb * sl, sb };
    double e[3];
    for (int i = 0; i < 3; ++i) e[i] = R[0][i] * g[0] + R[1][i] * g[1] + R[2][i] * g[2];
    ra = std::atan2(e[1], e[0]) / kDeg;
    if (ra < 0) ra += 360.0;
    dec = std::asin(std::max(-1.0, std::min(1.0, e[2]))) / kDeg;
}

// The point at angular distance d from (lon, lat) along position angle pa
// (east of north), all in degrees — exact on the sphere.
void destination(double lon, double lat, double pa, double d, double& lon2, double& lat2) {
    const double s1 = std::sin(lat * kDeg), c1 = std::cos(lat * kDeg);
    const double sd = std::sin(d * kDeg), cd = std::cos(d * kDeg);
    const double s2 = std::max(-1.0, std::min(1.0, s1 * cd + c1 * sd * std::cos(pa * kDeg)));
    lat2 = std::asin(s2) / kDeg;
    lon2 = lon + std::atan2(std::sin(pa * kDeg) * sd * c1, cd - s1 * s2) / kDeg;
}

struct Pt { double x = 0, y = 0; };

QString ds9Color(const QString& name) {
    static const QHash<QString, QString> named = {
        { "white", "#ffffff" }, { "black", "#000000" }, { "red", "#ff0000" },
        { "green", "#00ff00" }, { "blue", "#0000ff" }, { "cyan", "#00ffff" },
        { "magenta", "#ff00ff" }, { "yellow", "#ffff00" } };
    const QString k = name.trimmed().toLower();
    const auto it = named.constFind(k);
    if (it != named.constEnd()) return it.value();
    if (k.startsWith('#') && (k.size() == 7 || k.size() == 4)) return k;
    return QString();
}

// key=value properties; a value is {...}, "...", '...' or a bare token.
QHash<QString, QString> parseProps(const QString& s) {
    QHash<QString, QString> out;
    int i = 0;
    const int n = s.size();
    while (i < n) {
        while (i < n && (s[i].isSpace() || s[i] == '#')) ++i;
        const int k0 = i;
        while (i < n && !s[i].isSpace() && s[i] != '=') ++i;
        const QString key = s.mid(k0, i - k0).toLower();
        if (i >= n || s[i] != '=') { if (i == k0) ++i; continue; }
        ++i;
        QString val;
        if (i < n && s[i] == '{') {
            int depth = 0;
            const int v0 = i + 1;
            for (; i < n; ++i) {
                if (s[i] == '{') ++depth;
                else if (s[i] == '}' && --depth == 0) break;
            }
            val = s.mid(v0, i - v0);
            ++i;
        } else if (i < n && (s[i] == '"' || s[i] == '\'')) {
            const QChar q = s[i];
            const int v0 = ++i;
            while (i < n && s[i] != q) ++i;
            val = s.mid(v0, i - v0);
            ++i;
        } else {
            const int v0 = i;
            while (i < n && !s[i].isSpace()) ++i;
            val = s.mid(v0, i - v0);
        }
        if (!key.isEmpty() && !out.contains(key)) out.insert(key, val);
    }
    return out;
}

// "helvetica 10 normal roman" -> 10
double fontSize(const QString& font) {
    for (const QString& t : font.split(' ', Qt::SkipEmptyParts)) {
        bool ok = false;
        const double v = t.toDouble(&ok);
        if (ok && v >= 4 && v <= 96) return v;
    }
    return 0.0;
}

// Split on `sep` outside (), {}, "" and ''.
QStringList splitOutside(const QString& s, QChar sep) {
    QStringList out;
    int par = 0, brace = 0;
    QChar quote;
    int start = 0;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (!quote.isNull()) { if (c == quote) quote = QChar(); continue; }
        if (c == '"') quote = c;
        else if (c == '\'' && brace > 0) continue;       // apostrophe inside text={...}
        else if (c == '{') ++brace;
        else if (c == '}') brace = std::max(0, brace - 1);
        else if (c == '(') ++par;
        else if (c == ')') par = std::max(0, par - 1);
        else if (c == sep && par == 0 && brace == 0) {
            out << s.mid(start, i - start);
            start = i + 1;
        }
    }
    out << s.mid(start);
    return out;
}

bool sysFromWord(const QString& w, Sys& sys) {
    static const QHash<QString, Sys> map = {
        { "image", Sys::Image }, { "physical", Sys::Image },
        { "fk5", Sys::Equatorial }, { "j2000", Sys::Equatorial },
        { "icrs", Sys::Equatorial }, { "wcs", Sys::Equatorial },
        { "galactic", Sys::Galactic },
        { "fk4", Sys::Unsupported }, { "b1950", Sys::Unsupported },
        { "ecliptic", Sys::Unsupported }, { "linear", Sys::Unsupported },
        { "amplifier", Sys::Unsupported }, { "detector", Sys::Unsupported } };
    const auto it = map.constFind(w);
    if (it == map.constEnd()) {
        if (w.size() == 4 && w.startsWith(QLatin1String("wcs"))) { sys = Sys::Unsupported; return true; }
        return false;
    }
    sys = it.value();
    return true;
}

const QStringList kShapes = {
    "circle", "ellipse", "box", "polygon", "line", "vector", "text", "point",
    "annulus", "ruler", "projection", "segment",
    "panda", "epanda", "bpanda", "compass", "composite" };

enum class Fail { None, NeedWcs, Malformed, OffImage };

// Everything needed to turn one region's numbers into pixels.
struct Resolver {
    Sys sys = Sys::Image;
    const Wcs* wcs = nullptr;
    Fail fail = Fail::None;
    double lon0 = 0, lat0 = 0;        // region anchor, native sky coordinates
    Pt c;                             // anchor in pixels

    bool haveWcs() const { return wcs && wcs->valid(); }
    void failWith(Fail f) { if (fail == Fail::None) fail = f; }

    bool skyToPx(double lon, double lat, Pt& p) {
        if (!haveWcs()) { failWith(Fail::NeedWcs); return false; }
        double ra = lon, dec = lat;
        if (sys == Sys::Galactic) galToEq(lon, lat, ra, dec);
        if (!wcs->skyToPixel(ra, dec, p.x, p.y)) { failWith(Fail::OffImage); return false; }
        return true;
    }
    // A position given by two tokens.
    bool position(const QString& tx, const QString& ty, Pt& p, double* lon = nullptr,
                  double* lat = nullptr) {
        const Val a = parseVal(tx, sys == Sys::Equatorial), b = parseVal(ty, false);
        if (!a.ok || !b.ok) { failWith(Fail::Malformed); return false; }
        if (sys == Sys::Image || (a.pixel() && b.pixel())) {
            p.x = a.v - 1.0;                         // FITS 1-based -> 0-based
            p.y = b.v - 1.0;
            if (sys != Sys::Image && haveWcs()) {    // keep a sky anchor for sized shapes
                double ra = 0, dec = 0;
                wcs->pixelToSky(p.x, p.y, ra, dec);
                if (lon) *lon = ra;
                if (lat) *lat = dec;
            }
            return true;
        }
        const double lo = a.degrees(), la = b.degrees();
        if (lon) *lon = lo;
        if (lat) *lat = la;
        return skyToPx(lo, la, p);
    }
    bool anchor(const QString& tx, const QString& ty) {
        return position(tx, ty, c, &lon0, &lat0);
    }
    // Pixel vector of a displacement of `size` along region angle thetaDeg.
    bool vec(double thetaDeg, const Val& size, double scale, Pt& v) {
        if (!size.ok) { failWith(Fail::Malformed); return false; }
        if (sys == Sys::Image) {
            double len = size.v;
            if (size.angular()) {
                if (!haveWcs()) { failWith(Fail::NeedWcs); return false; }
                len = size.degrees() * 3600.0 / wcs->pixelScaleArcsec();
            }
            len *= scale;
            v.x = len * std::cos(thetaDeg * kDeg);   // CCW from +X in FITS (X, Y),
            v.y = len * std::sin(thetaDeg * kDeg);   // which are our pixel axes
            return true;
        }
        if (!haveWcs()) { failWith(Fail::NeedWcs); return false; }
        // On the sky: theta runs from west toward north, i.e. position angle
        // (east of north) = theta - 90.
        const double pa = thetaDeg - 90.0;
        const bool px = size.pixel();
        const double d = px ? 5.0 * wcs->pixelScaleArcsec() / 3600.0   // direction probe
                            : size.degrees() * scale;
        if (d == 0.0) { v = Pt(); return true; }
        double lon = 0, lat = 0;
        destination(lon0, lat0, pa, d, lon, lat);
        Pt e;
        if (!skyToPx(lon, lat, e)) return false;
        v.x = e.x - c.x;
        v.y = e.y - c.y;
        if (px) {
            const double n = std::hypot(v.x, v.y);
            if (n <= 0) { failWith(Fail::Malformed); return false; }
            v.x *= size.v * scale / n;
            v.y *= size.v * scale / n;
        }
        return true;
    }
};

} // namespace

std::vector<Ds9Shape> parseDs9Regions(const QString& text, const Wcs* wcs,
                                      double pointRadiusPx, Ds9ImportStats* statsOut) {
    std::vector<Ds9Shape> out;
    Ds9ImportStats st;
    Sys sys = Sys::Image;                // DS9's default when a file names none
    QString sysWord = QStringLiteral("image");
    QString globalColor;
    double globalFont = 0.0;

    auto noteUnsupported = [&](const QString& what) {
        ++st.unsupported;
        if (!st.unsupportedKinds.contains(what)) st.unsupportedKinds << what;
    };

    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                         Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith('#')) {
            // DS9 writes some shapes in comment form: "# text(x,y) text={..}",
            // "# vector(...)", "# ruler(...)", "# segment(...)", ...
            const QString rest = line.mid(1).trimmed();
            const int par = rest.indexOf('(');
            if (par <= 0 || !kShapes.contains(rest.left(par).trimmed().toLower())) continue;
            line = rest;
        }
        for (QString stmt : splitOutside(line, ';')) {
            stmt = stmt.trimmed();
            if (stmt.isEmpty()) continue;
            const QString low = stmt.toLower();
            if (low.startsWith(QLatin1String("global"))) {
                const auto props = parseProps(stmt.mid(6));
                if (props.contains("color")) globalColor = ds9Color(props.value("color"));
                if (props.contains("font")) globalFont = fontSize(props.value("font"));
                continue;
            }
            Sys s2;
            if (sysFromWord(low, s2)) { sys = s2; sysWord = low; continue; }

            // [+|-|!] name ( args ) [#] props      or      name args # props
            int i = 0;
            while (i < stmt.size() && (stmt[i] == '+' || stmt[i] == '-' || stmt[i] == '!')) ++i;
            int j = i;
            while (j < stmt.size() && stmt[j].isLetter()) ++j;
            const QString name = stmt.mid(i, j - i).toLower();
            if (!kShapes.contains(name)) { ++st.malformed; continue; }
            QString argStr, propStr;
            int k = j;
            while (k < stmt.size() && stmt[k].isSpace()) ++k;
            if (k < stmt.size() && stmt[k] == '(') {
                const int close = stmt.indexOf(')', k);
                if (close < 0) { ++st.malformed; continue; }
                argStr = stmt.mid(k + 1, close - k - 1);
                propStr = stmt.mid(close + 1);
            } else {
                const int hash = stmt.indexOf('#', k);
                argStr = hash < 0 ? stmt.mid(k) : stmt.mid(k, hash - k);
                propStr = hash < 0 ? QString() : stmt.mid(hash + 1);
            }
            QStringList args;
            for (const QString& a : argStr.split(QRegularExpression(QStringLiteral("[,\\s]+")),
                                                 Qt::SkipEmptyParts))
                if (!a.contains('=')) args << a;            // drop "n=4"-style extras
            const auto props = parseProps(propStr);

            if (name == QLatin1String("panda") || name == QLatin1String("epanda") ||
                name == QLatin1String("bpanda") || name == QLatin1String("compass") ||
                name == QLatin1String("composite")) { noteUnsupported(name); continue; }
            if (sys == Sys::Unsupported) { noteUnsupported(sysWord); continue; }

            Resolver R;
            R.sys = sys;
            R.wcs = wcs;
            const QString color = props.contains("color") ? ds9Color(props.value("color"))
                                                          : globalColor;
            const double fsize = props.contains("font") ? fontSize(props.value("font"))
                                                        : globalFont;
            const QString label = props.value("text");
            std::vector<Ds9Shape> made;
            auto emitEllipse = [&](const Pt& u, const Pt& v) {
                Ds9Shape s;
                s.kind = Ds9Shape::Kind::Ellipse;
                s.x = R.c.x; s.y = R.c.y;
                s.a = std::hypot(u.x, u.y);
                s.b = std::hypot(v.x, v.y);
                s.angleDeg = std::atan2(u.y, u.x) / kDeg;
                made.push_back(s);
            };
            auto emitLine = [&](const Pt& p, const Pt& q) {
                Ds9Shape s;
                s.kind = Ds9Shape::Kind::Line;
                s.x = p.x; s.y = p.y; s.x2 = q.x; s.y2 = q.y;
                made.push_back(s);
            };
            auto angleArg = [&](const QString& t, double& deg) {
                const Val a = parseVal(t, false);
                if (!a.ok) { R.failWith(Fail::Malformed); return false; }
                deg = a.unit == 'r' ? a.v / kDeg : a.v;
                return true;
            };
            const int n = int(args.size());
            bool ok = true;

            if (name == QLatin1String("circle") || name == QLatin1String("annulus")) {
                ok = n >= 3 && R.anchor(args[0], args[1]);
                for (int r = 2; ok && r < n; ++r) {
                    Pt u, v;
                    const Val rad = parseVal(args[r], false);
                    ok = R.vec(0.0, rad, 1.0, u) && R.vec(90.0, rad, 1.0, v);
                    if (ok) {
                        const double m = 0.5 * (std::hypot(u.x, u.y) + std::hypot(v.x, v.y));
                        Ds9Shape s;
                        s.kind = Ds9Shape::Kind::Ellipse;
                        s.x = R.c.x; s.y = R.c.y; s.a = s.b = m;
                        made.push_back(s);
                    }
                }
            } else if (name == QLatin1String("ellipse") || name == QLatin1String("box")) {
                ok = n >= 4 && R.anchor(args[0], args[1]);
                double theta = 0.0;
                int pairs = (n - 2) / 2;
                if (ok && (n - 2) % 2 == 1) ok = angleArg(args[n - 1], theta);
                const bool box = name == QLatin1String("box");
                for (int p = 0; ok && p < pairs; ++p) {
                    Pt u, v;
                    const double half = box ? 0.5 : 1.0;
                    ok = R.vec(theta, parseVal(args[2 + 2 * p], false), half, u) &&
                         R.vec(theta + 90.0, parseVal(args[3 + 2 * p], false), half, v);
                    if (!ok) break;
                    // A box is the ellipse's frame with corners: centre, the
                    // two half-size vectors, the direction of the first.
                    emitEllipse(u, v);
                    if (box) made.back().kind = Ds9Shape::Kind::Box;
                }
            } else if (name == QLatin1String("polygon") || name == QLatin1String("segment")) {
                ok = n >= 4 && n % 2 == 0;
                std::vector<Pt> pts;
                for (int p = 0; ok && p + 1 < n; p += 2) {
                    Pt q;
                    ok = R.position(args[p], args[p + 1], q);
                    if (ok) pts.push_back(q);
                }
                if (!ok && R.fail == Fail::None) R.failWith(Fail::Malformed);
                for (std::size_t p = 0; ok && p + 1 < pts.size(); ++p) emitLine(pts[p], pts[p + 1]);
                if (ok && name == QLatin1String("polygon") && pts.size() > 2)
                    emitLine(pts.back(), pts.front());
            } else if (name == QLatin1String("line") || name == QLatin1String("ruler") ||
                       name == QLatin1String("projection")) {
                Pt p, q;
                ok = n >= 4 && R.position(args[0], args[1], p) && R.position(args[2], args[3], q);
                if (ok) emitLine(p, q);
            } else if (name == QLatin1String("vector")) {
                double theta = 0.0;
                Pt v;
                ok = n >= 4 && R.anchor(args[0], args[1]) && angleArg(args[3], theta) &&
                     R.vec(theta, parseVal(args[2], false), 1.0, v);
                if (ok) emitLine(R.c, Pt { R.c.x + v.x, R.c.y + v.y });
            } else if (name == QLatin1String("point")) {
                ok = n >= 2 && R.anchor(args[0], args[1]);
                if (ok) {
                    Ds9Shape s;
                    s.kind = Ds9Shape::Kind::Ellipse;
                    s.x = R.c.x; s.y = R.c.y; s.a = s.b = std::max(1.0, pointRadiusPx);
                    made.push_back(s);
                }
            } else if (name == QLatin1String("text")) {
                ok = n >= 2 && R.anchor(args[0], args[1]);
                if (ok && !label.isEmpty()) {
                    Ds9Shape s;
                    s.kind = Ds9Shape::Kind::Text;
                    s.x = R.c.x; s.y = R.c.y;
                    made.push_back(s);
                }
            }
            if (!ok || R.fail != Fail::None) {
                const Fail f = R.fail == Fail::None ? Fail::Malformed : R.fail;
                if (f == Fail::NeedWcs) ++st.needWcs;
                else if (f == Fail::OffImage) ++st.offImage;
                else ++st.malformed;
                continue;
            }
            if (made.empty()) continue;                 // e.g. a text region without text
            made.front().text = label;                  // one label per region
            for (Ds9Shape& s : made) {
                s.color = color;
                s.textSize = fsize;
                s.sky = sys != Sys::Image;
                out.push_back(s);
            }
            ++st.regions;
            st.shapes += int(made.size());
        }
    }
    if (statsOut) *statsOut = st;
    return out;
}

std::vector<Ds9Shape> loadDs9Regions(const QString& path, const Wcs* wcs,
                                     double pointRadiusPx, Ds9ImportStats* stats,
                                     QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err) *err = f.errorString();
        if (stats) *stats = Ds9ImportStats();
        return {};
    }
    return parseDs9Regions(QString::fromUtf8(f.readAll()), wcs, pointRadiusPx, stats);
}

} // namespace astro

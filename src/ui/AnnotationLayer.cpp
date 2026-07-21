#include "ui/AnnotationLayer.h"
#include "core/Preferences.h"
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsItemGroup>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsSimpleTextItem>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <QPainterPath>
#include <QPen>
#include <QFont>
#include <QTransform>
#include <QtMath>
#include <cmath>

namespace astro {

static const QColor kGridColor(143, 192, 245, 90);
static const QColor kGridLabel(143, 192, 245, 200);
static const QColor kGridColorInv(20, 34, 52, 150);
static const QColor kGridLabelInv(10, 20, 32, 230);

// ---- Annotation JSON ---------------------------------------------------------

QJsonObject Annotation::toJson() const {
    QJsonObject o;
    o["type"] = type == Type::Line ? "line" : (type == Type::Text ? "text" : "ellipse");
    o["x"] = x; o["y"] = y;
    if (type == Type::Line) { o["x2"] = x2; o["y2"] = y2; }
    if (type == Type::Ellipse) { o["a"] = a; o["b"] = b; o["angle"] = angleDeg; }
    if (!label.isEmpty()) o["label"] = label;
    o["size"] = textSize;
    o["color"] = color.name(QColor::HexRgb);
    return o;
}

Annotation Annotation::fromJson(const QJsonObject& o) {
    Annotation a;
    const QString t = o["type"].toString(QStringLiteral("ellipse"));
    a.type = t == QLatin1String("line") ? Type::Line
           : t == QLatin1String("text") ? Type::Text : Type::Ellipse;
    a.x = o["x"].toDouble(); a.y = o["y"].toDouble();
    a.x2 = o["x2"].toDouble(); a.y2 = o["y2"].toDouble();
    a.a = o["a"].toDouble(40); a.b = o["b"].toDouble(40);
    a.angleDeg = o["angle"].toDouble();
    a.label = o["label"].toString();
    a.textSize = o["size"].toDouble(10);
    const QColor c(o["color"].toString(QStringLiteral("#8fc0f5")));
    if (c.isValid()) a.color = c;
    return a;
}

QJsonDocument AnnotationLayer::toJson(const std::vector<Annotation>& annotations) {
    QJsonArray arr;
    for (const Annotation& a : annotations) arr.append(a.toJson());
    QJsonObject root;
    root["format"] = "nebulascope-annotations";
    root["version"] = 1;
    root["annotations"] = arr;
    return QJsonDocument(root);
}

std::vector<Annotation> AnnotationLayer::fromJson(const QJsonDocument& doc, QString* err) {
    std::vector<Annotation> out;
    if (!doc.isObject()) { if (err) *err = QStringLiteral("Not a JSON object"); return out; }
    const QJsonObject root = doc.object();
    if (root["format"].toString() != QLatin1String("nebulascope-annotations")) {
        if (err) *err = QStringLiteral("Not a NebulaScope annotation file");
        return out;
    }
    for (const auto& v : root["annotations"].toArray())
        if (v.isObject()) out.push_back(Annotation::fromJson(v.toObject()));
    return out;
}

AnnotationLayer::AnnotationLayer(QGraphicsScene* scene, QObject* parent)
    : QObject(parent), m_scene(scene) {}

static QPointF handleHome(const Annotation& a, const QString& role);   // defined below

void AnnotationLayer::setActive(int idx) {
    m_active = idx;
    rebuildHandles();
}

void AnnotationLayer::syncHandles() {
    if (!m_group || m_handles.empty() || m_active < 0 || m_active >= int(m_lastAnns.size())) return;
    // Drag offset of the active annotation's sub-group (0,0 when untouched).
    QPointF d;
    for (QGraphicsItem* it : m_group->childItems())
        if (it->data(0).isValid() && it->data(0).toInt() == m_active) { d = it->pos(); break; }
    const Annotation& a = m_lastAnns[std::size_t(m_active)];
    for (QGraphicsItem* h : m_handles)
        h->setPos(handleHome(a, h->data(2).toString()) + d);
}

double AnnotationLayer::niceStepDeg(double spanDeg, int target) {
    const double raw = spanDeg / std::max(1, target);
    // 1-2-5 ladder plus arcmin/arcsec-friendly substeps.
    static const double steps[] = { 1.0/3600, 2.0/3600, 5.0/3600, 10.0/3600, 30.0/3600,
                                    1.0/60, 2.0/60, 5.0/60, 10.0/60, 30.0/60,
                                    1, 2, 5, 10, 15, 30, 45 };
    for (double s : steps) if (s >= raw) return s;
    return 45;
}

void AnnotationLayer::rebuild(int w, int h, const Wcs& wcs,
                              const std::vector<Annotation>& annotations) {
    m_rebuilding = true;
    m_handles.clear();                            // children of m_group, deleted with it
    if (m_group) { m_scene->removeItem(m_group); delete m_group; m_group = nullptr; }
    m_group = new QGraphicsItemGroup();
    m_group->setZValue(10);                       // above the pixmap (z 0)
    m_group->setHandlesChildEvents(false);
    m_scene->addItem(m_group);

    if (m_gridVisible && w > 0 && h > 0) buildGrid(w, h, wcs);
    buildAnnotations(annotations);
    m_lastAnns = annotations;
    m_rebuilding = false;
    rebuildHandles();                             // handles survive rebuilds
}

// Handle geometry shared by rebuildHandles() and commitMoves(): where each
// handle sits in scene coordinates for a given annotation.
static QPointF handleHome(const Annotation& a, const QString& role) {
    if (role == QLatin1String("p1")) return { a.x, a.y };
    if (role == QLatin1String("p2")) return { a.x2, a.y2 };
    const double th = qDegreesToRadians(a.angleDeg);
    if (role == QLatin1String("a")) return { a.x + a.a * std::cos(th), a.y + a.a * std::sin(th) };
    return { a.x - a.b * std::sin(th), a.y + a.b * std::cos(th) };   // "b": perpendicular
}

void AnnotationLayer::rebuildHandles() {
    if (!m_group) return;
    for (QGraphicsItem* h : m_handles) { m_group->removeFromGroup(h); m_scene->removeItem(h); delete h; }
    m_handles.clear();

    if (m_active < 0 || m_active >= int(m_lastAnns.size())) { m_active = -1; return; }
    const int idx = m_active;
    const Annotation& a = m_lastAnns[std::size_t(idx)];

    QStringList roles;
    if (a.type == Annotation::Type::Ellipse) roles = { "a", "b" };
    else if (a.type == Annotation::Type::Line) roles = { "p1", "p2" };
    else return;                                   // text: move only

    for (const QString& role : roles) {
        auto* h = new QGraphicsRectItem(-4, -4, 8, 8);
        h->setBrush(QColor(255, 255, 255, 230));
        h->setPen(QPen(QColor(10, 16, 24), 1));
        h->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        h->setFlags(h->flags() | QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        h->setZValue(30);
        h->setData(1, idx);
        h->setData(2, role);
        h->setPos(handleHome(a, role));
        m_group->addToGroup(h);
        m_handles.push_back(h);
    }
}

void AnnotationLayer::buildGrid(int w, int h, const Wcs& wcs) {
    const QColor gridCol  = m_inverted ? kGridColorInv : kGridColor;
    const QColor gridLab  = m_inverted ? kGridLabelInv : kGridLabel;
    const QColor chipCol  = m_inverted ? QColor(235, 240, 246, 170) : QColor(5, 7, 10, 150);
    QPen pen(gridCol, 0);                         // width 0 = cosmetic (1px at any zoom)

    if (!wcs.valid()) {
        // Pixel-grid fallback: lines every ~1/8 of the larger dimension.
        const int step = std::max(64, ((std::max(w, h) / 8) / 50) * 50);
        QPainterPath p;
        for (int x = step; x < w; x += step) { p.moveTo(x, 0); p.lineTo(x, h); }
        for (int y = step; y < h; y += step) { p.moveTo(0, y); p.lineTo(w, y); }
        auto* item = new QGraphicsPathItem(p);
        item->setPen(pen);
        m_group->addToGroup(item);
        return;
    }

    // Sky bounds: sample the frame's corners and edge midpoints.
    double raMin = 1e9, raMax = -1e9, decMin = 1e9, decMax = -1e9;
    const double sx[] = { 0.0, 0.5, 1.0 };
    double ra0 = 0, dec0 = 0;
    wcs.pixelToSky(w / 2.0, h / 2.0, ra0, dec0);
    for (double fx : sx) for (double fy : sx) {
        double ra, dec;
        if (!wcs.pixelToSky(fx * (w - 1), fy * (h - 1), ra, dec)) continue;
        // unwrap RA around the field centre so min/max work across 0h
        while (ra - ra0 > 180)  ra -= 360;
        while (ra - ra0 < -180) ra += 360;
        raMin = std::min(raMin, ra);  raMax = std::max(raMax, ra);
        decMin = std::min(decMin, dec); decMax = std::max(decMax, dec);
    }
    if (raMin > raMax || decMin > decMax) return;

    const double raStep  = niceStepDeg((raMax - raMin), Preferences::get().gridTargetLines);
    const double decStep = niceStepDeg(decMax - decMin, Preferences::get().gridTargetLines);
    QFont labelFont;
    labelFont.setPointSizeF(7.0);

    auto addCurve = [&](bool isoRa, double fixed, double from, double to, double step) {
        QPainterPath path;
        bool pen0 = false;
        const int SAMPLES = 120;
        double firstX = -1, firstY = -1, labelAngle = 0;
        double prevX = 0, prevY = 0; bool havePrev = false, needAngle = false;
        for (int i = 0; i <= SAMPLES; ++i) {
            const double v = from + (to - from) * i / SAMPLES;
            double px, py;
            const bool ok = isoRa ? wcs.skyToPixel(fixed, v, px, py)
                                  : wcs.skyToPixel(v, fixed, px, py);
            if (!ok || px < -w * 0.2 || px > w * 1.2 || py < -h * 0.2 || py > h * 1.2) {
                pen0 = false;
                havePrev = false;
                continue;
            }
            if (!pen0) { path.moveTo(px, py); pen0 = true; }
            else path.lineTo(px, py);
            // First sample actually inside the frame anchors the label — on ANY
            // sample, not just pen-down (most curves enter from outside). The
            // label is set along the curve: take its direction from the
            // neighbouring sample (previous if available, else the next one).
            if (firstX < 0 && px >= 0 && px < w && py >= 0 && py < h) {
                firstX = px; firstY = py;
                if (havePrev) labelAngle = qRadiansToDegrees(std::atan2(py - prevY, px - prevX));
                else needAngle = true;
            } else if (needAngle) {
                labelAngle = qRadiansToDegrees(std::atan2(py - firstY, px - firstX));
                needAngle = false;
            }
            prevX = px; prevY = py; havePrev = true;
        }
        if (path.isEmpty()) return;
        auto* item = new QGraphicsPathItem(path);
        item->setPen(QPen(gridCol, 0));
        m_group->addToGroup(item);

        if (firstX >= 0) {
            const QString text = isoRa ? Wcs::formatRa(fixed) : Wcs::formatDec(fixed);
            // Keep text upright: flip directions pointing left.
            double ang = labelAngle;
            if (ang > 90)  ang -= 180;
            if (ang < -90) ang += 180;
            // Text + dark chip behind it, fixed screen size, rotated to run
            // along the grid line (rotation applies in device space because of
            // ItemIgnoresTransformations, which is exactly what we want).
            auto* label = new QGraphicsSimpleTextItem(text);
            label->setBrush(gridLab);
            label->setFont(labelFont);
            const QRectF tb = label->boundingRect().adjusted(-3, -1, 3, 1);
            auto* chip = new QGraphicsRectItem(tb);
            chip->setBrush(chipCol);
            chip->setPen(Qt::NoPen);
            for (QGraphicsItem* it : { (QGraphicsItem*)chip, (QGraphicsItem*)label }) {
                it->setFlag(QGraphicsItem::ItemIgnoresTransformations);
                it->setPos(firstX, firstY);
                it->setRotation(ang);
            }
            chip->setZValue(1);
            label->setZValue(2);
            m_group->addToGroup(chip);
            m_group->addToGroup(label);
        }
    };

    // Iso-RA curves (constant RA, Dec varies) and iso-Dec curves.
    const double decPad = decStep, raPad = raStep;
    for (double ra = std::ceil(raMin / raStep) * raStep; ra <= raMax; ra += raStep) {
        double raN = ra; while (raN < 0) raN += 360; while (raN >= 360) raN -= 360;
        addCurve(true, raN, decMin - decPad, decMax + decPad, decStep);
    }
    for (double dec = std::ceil(decMin / decStep) * decStep; dec <= decMax; dec += decStep)
        addCurve(false, dec, raMin - raPad, raMax + raPad, raStep);
}

void AnnotationLayer::buildAnnotations(const std::vector<Annotation>& annotations) {
    for (std::size_t idx = 0; idx < annotations.size(); ++idx) {
        const Annotation& a = annotations[idx];
        // Inverted contrast: complement the stroke colour so dark strokes sit on
        // bright fields; the user's chosen hue stays recognisable as its inverse.
        const QColor col = m_inverted
            ? QColor(255 - a.color.red(), 255 - a.color.green(), 255 - a.color.blue())
            : a.color;
        QFont f;
        f.setPointSizeF(a.textSize);
        f.setBold(true);
        // Stroke width in screen pixels at any zoom (cosmetic pen; 0 = hairline).
        QPen stroke(col, Preferences::get().annLineWidth);
        stroke.setCosmetic(true);

        // One sub-group per annotation: shape + label move/select as a unit.
        auto* g = new QGraphicsItemGroup();
        g->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        g->setData(0, int(idx));                  // index into the data model
        m_group->addToGroup(g);

        double lx = a.x, ly = a.y;                // label anchor
        if (a.type == Annotation::Type::Line) {
            auto* line = new QGraphicsLineItem(a.x, a.y, a.x2, a.y2);
            line->setPen(stroke);
            g->addToGroup(line);
            lx = a.x; ly = a.y;   // label at the START endpoint — the segment points at the target
        } else if (a.type == Annotation::Type::Ellipse) {
            auto* ell = new QGraphicsEllipseItem(a.x - a.a, a.y - a.b, 2 * a.a, 2 * a.b);
            ell->setPen(stroke);
            ell->setBrush(Qt::NoBrush);
            if (std::fabs(a.angleDeg) > 1e-6) {
                ell->setTransformOriginPoint(a.x, a.y);
                ell->setRotation(a.angleDeg);
            }
            g->addToGroup(ell);
            lx = a.x + a.a * 0.7071; ly = a.y - a.b * 0.7071;   // NE of the ellipse
        }
        // Text: for Type::Text the label IS the annotation, at (x, y).
        if (!a.label.isEmpty()) {
            auto* label = new QGraphicsSimpleTextItem(a.label);
            label->setBrush(col);
            label->setFont(f);
            label->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            label->setPos(lx, ly);
            if (a.type == Annotation::Type::Line) {
                // Keep the text clear of the segment: centre the text box just
                // beyond the start endpoint, opposite the segment's direction.
                // Offsets are in DEVICE pixels (the label ignores view zoom), so
                // the clearance holds at any zoom level.
                const QRectF tb = label->boundingRect();
                double dx = a.x2 - a.x, dy = a.y2 - a.y;
                const double len = std::hypot(dx, dy);
                if (len > 1e-9) { dx /= len; dy /= len; } else { dx = 1; dy = 0; }
                const double r = 6.0 + 0.5 * std::hypot(tb.width(), tb.height());
                label->setTransform(QTransform::fromTranslate(-dx * r - tb.width() / 2.0,
                                                              -dy * r - tb.height() / 2.0));
            }
            g->addToGroup(label);
        }
    }
}

int AnnotationLayer::hitTest(const QPointF& scenePos) const {
    if (!m_scene || !m_group) return -1;
    // Items with ItemIgnoresTransformations (text labels) can only be hit-tested
    // with the view's device transform — the plain items(scenePos) overload
    // misses them at any zoom other than 1:1.
    QTransform vt;
    if (!m_scene->views().isEmpty()) vt = m_scene->views().first()->viewportTransform();
    for (QGraphicsItem* it : m_scene->items(scenePos, Qt::IntersectsItemShape, Qt::DescendingOrder, vt))
        for (QGraphicsItem* p = it; p; p = p->parentItem()) {
            const QVariant v = p->data(0);
            if (v.isValid()) return v.toInt();
        }
    return -1;
}

bool AnnotationLayer::commitMoves(std::vector<Annotation>& annotations) {
    if (!m_group) return false;
    bool changed = false;
    QSet<int> movedByGroup;                       // whole-annotation drags this pass
    for (QGraphicsItem* it : m_group->childItems()) {
        // Whole-annotation drags (sub-groups tagged data(0)).
        const QVariant v = it->data(0);
        if (v.isValid()) {
            const QPointF d = it->pos();              // drag offset (0,0 if untouched)
            if (d.isNull()) continue;
            const int idx = v.toInt();
            if (idx < 0 || idx >= int(annotations.size())) continue;
            Annotation& a = annotations[std::size_t(idx)];
            a.x += d.x(); a.y += d.y();
            if (a.type == Annotation::Type::Line) { a.x2 += d.x(); a.y2 += d.y(); }
            movedByGroup.insert(idx);
            changed = true;
            continue;
        }
        // Resize-handle drags (tagged data(1) = index, data(2) = role).
        const QVariant hv = it->data(1);
        if (!hv.isValid()) continue;
        const int idx = hv.toInt();
        if (idx < 0 || idx >= int(annotations.size())) continue;
        // The annotation itself was dragged: its handles carry a stale (or
        // merely translated) position — interpreting that as a resize is what
        // used to distort the shape. Skip; rebuild repositions them.
        if (movedByGroup.contains(idx)) continue;
        Annotation& a = annotations[std::size_t(idx)];
        const QString role = it->data(2).toString();
        const QPointF now = it->pos();                // handleHome + drag offset
        if (now == handleHome(a, role)) continue;
        if (role == QLatin1String("p1")) { a.x = now.x(); a.y = now.y(); }
        else if (role == QLatin1String("p2")) { a.x2 = now.x(); a.y2 = now.y(); }
        else if (role == QLatin1String("a")) {
            const double dx = now.x() - a.x, dy = now.y() - a.y;
            a.a = std::max(2.0, std::hypot(dx, dy));
            a.angleDeg = qRadiansToDegrees(std::atan2(dy, dx));   // a-handle also rotates
        } else {                                       // "b": perpendicular semi-axis, length only
            a.b = std::max(2.0, std::hypot(now.x() - a.x, now.y() - a.y));
        }
        changed = true;
    }
    return changed;
}

} // namespace astro

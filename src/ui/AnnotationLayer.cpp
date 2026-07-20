#include "ui/AnnotationLayer.h"
#include <QGraphicsScene>
#include <QGraphicsItemGroup>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsSimpleTextItem>
#include <QPainterPath>
#include <QPen>
#include <QFont>
#include <cmath>

namespace astro {

static const QColor kGridColor(143, 192, 245, 90);
static const QColor kGridLabel(143, 192, 245, 200);

AnnotationLayer::AnnotationLayer(QGraphicsScene* scene, QObject* parent)
    : QObject(parent), m_scene(scene) {}

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
    if (m_group) { m_scene->removeItem(m_group); delete m_group; m_group = nullptr; }
    m_group = new QGraphicsItemGroup();
    m_group->setZValue(10);                       // above the pixmap (z 0)
    m_group->setHandlesChildEvents(false);
    m_scene->addItem(m_group);

    if (m_gridVisible && w > 0 && h > 0) buildGrid(w, h, wcs);
    buildAnnotations(annotations);
}

void AnnotationLayer::buildGrid(int w, int h, const Wcs& wcs) {
    QPen pen(kGridColor, 0);                      // width 0 = cosmetic (1px at any zoom)

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

    const double raStep  = niceStepDeg((raMax - raMin), 6);
    const double decStep = niceStepDeg(decMax - decMin, 6);
    QFont labelFont;
    labelFont.setPointSizeF(9.0);

    auto addCurve = [&](bool isoRa, double fixed, double from, double to, double step) {
        QPainterPath path;
        bool pen0 = false;
        const int SAMPLES = 120;
        double firstX = -1, firstY = -1;
        for (int i = 0; i <= SAMPLES; ++i) {
            const double v = from + (to - from) * i / SAMPLES;
            double px, py;
            const bool ok = isoRa ? wcs.skyToPixel(fixed, v, px, py)
                                  : wcs.skyToPixel(v, fixed, px, py);
            if (!ok || px < -w * 0.2 || px > w * 1.2 || py < -h * 0.2 || py > h * 1.2) {
                pen0 = false;
                continue;
            }
            if (!pen0) { path.moveTo(px, py); pen0 = true; }
            else path.lineTo(px, py);
            // First sample actually inside the frame anchors the label — on ANY
            // sample, not just pen-down (most curves enter from outside).
            if (firstX < 0 && px >= 0 && px < w && py >= 0 && py < h) { firstX = px; firstY = py; }
        }
        if (path.isEmpty()) return;
        auto* item = new QGraphicsPathItem(path);
        item->setPen(QPen(kGridColor, 0));
        m_group->addToGroup(item);

        if (firstX >= 0) {
            const QString text = isoRa ? Wcs::formatRa(fixed) : Wcs::formatDec(fixed);
            // Text + dark chip behind it, both fixed screen size, nudged in from
            // the frame edge so labels don't sit half outside.
            auto* label = new QGraphicsSimpleTextItem(text);
            label->setBrush(kGridLabel);
            label->setFont(labelFont);
            const QRectF tb = label->boundingRect().adjusted(-4, -2, 4, 2);
            auto* chip = new QGraphicsRectItem(tb);
            chip->setBrush(QColor(5, 7, 10, 170));
            chip->setPen(Qt::NoPen);
            chip->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            chip->setPos(firstX, firstY);
            label->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            label->setPos(firstX, firstY);
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
    QFont f;
    f.setPointSizeF(10.0);
    f.setBold(true);
    for (const Annotation& a : annotations) {
        auto* ell = new QGraphicsEllipseItem(a.x - a.rx, a.y - a.ry, 2 * a.rx, 2 * a.ry);
        ell->setPen(QPen(a.color, 0, Qt::SolidLine));
        ell->setBrush(Qt::NoBrush);
        if (std::fabs(a.angleDeg) > 1e-6) {
            ell->setTransformOriginPoint(a.x, a.y);
            ell->setRotation(a.angleDeg);
        }
        m_group->addToGroup(ell);

        if (!a.label.isEmpty()) {
            auto* label = new QGraphicsSimpleTextItem(a.label);
            label->setBrush(a.color);
            label->setFont(f);
            label->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            label->setPos(a.x + a.rx * 0.7071, a.y - a.ry * 0.7071);   // NE of the ellipse
            m_group->addToGroup(label);
        }
    }
}

} // namespace astro

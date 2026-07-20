#pragma once
//
// AnnotationLayer — a vector overlay above the image pixmap. Everything here is
// QGraphicsItems in scene (image-pixel) coordinates: never rasterized into the
// image data, always crisp at any zoom, excluded from data export.
//
//   * RA/Dec grid — iso-RA / iso-Dec curves from the Wcs (pixel grid fallback),
//     with coordinate labels that keep constant screen size.
//   * Source annotations — an ellipse + text label marking a star/nebula/galaxy.
//
// Annotations are plain data (Annotation struct); MainWindow stores them per
// image and calls rebuild() on switch, so the layer itself stays stateless
// about which image is showing.
//
#include <QObject>
#include <QString>
#include <QColor>
#include <vector>
#include "core/Wcs.h"

class QGraphicsScene;
class QGraphicsItemGroup;

namespace astro {

struct Annotation {
    QString label;                 // e.g. "M81", "Holmberg IX"
    double  x = 0, y = 0;          // centre, image pixels
    double  rx = 40, ry = 40;      // ellipse radii, image pixels
    double  angleDeg = 0;          // ellipse rotation
    QColor  color = QColor("#8fc0f5");
};

class AnnotationLayer : public QObject {
    Q_OBJECT
public:
    explicit AnnotationLayer(QGraphicsScene* scene, QObject* parent = nullptr);

    // Rebuild everything for the image now showing (called on image switch,
    // grid toggle, or annotation edits). w/h in image pixels.
    void rebuild(int w, int h, const Wcs& wcs, const std::vector<Annotation>& annotations);

    void setGridVisible(bool on) { m_gridVisible = on; }
    bool gridVisible() const { return m_gridVisible; }

private:
    void buildGrid(int w, int h, const Wcs& wcs);
    void buildAnnotations(const std::vector<Annotation>& annotations);
    // Pick a "nice" grid step in degrees for roughly `target` lines across span.
    static double niceStepDeg(double spanDeg, int target);

    QGraphicsScene* m_scene = nullptr;
    QGraphicsItemGroup* m_group = nullptr;   // owns all overlay items
    bool m_gridVisible = false;
};

} // namespace astro

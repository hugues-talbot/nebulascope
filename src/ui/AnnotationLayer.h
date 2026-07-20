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
#include <QJsonObject>
#include <vector>
#include "core/Wcs.h"

class QGraphicsScene;
class QGraphicsItem;
class QGraphicsItemGroup;
class QJsonDocument;

namespace astro {

struct Annotation {
    enum class Type { Ellipse, Line, Text };
    Type    type = Type::Ellipse;
    QString label;                 // text content (Text) or marker name (Ellipse/Line)
    double  x = 0, y = 0;          // centre (Ellipse/Text) or first endpoint (Line), image px
    double  x2 = 0, y2 = 0;        // second endpoint (Line)
    double  a = 40, b = 40;        // semi-major / semi-minor axes (Ellipse), image px
    double  angleDeg = 0;          // ellipse rotation, degrees
    double  textSize = 10;         // label point size (screen points)
    QColor  color = QColor("#8fc0f5");

    QJsonObject toJson() const;
    static Annotation fromJson(const QJsonObject& o);
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

    // Inverted contrast: light backing chips with complement-coloured strokes,
    // for annotations sitting on bright fields.
    void setInvertedContrast(bool on) { m_inverted = on; }
    bool invertedContrast() const { return m_inverted; }
    int activeIndex() const { return m_active; }   // annotation showing handles, -1 = none

    // JSON (de)serialization of a whole annotation list.
    static QJsonDocument toJson(const std::vector<Annotation>& annotations);
    static std::vector<Annotation> fromJson(const QJsonDocument& doc, QString* err = nullptr);

    // Index of the annotation whose items contain scenePos, or -1.
    int hitTest(const QPointF& scenePos) const;
    // Show resize handles for this annotation index (-1 = none). Driven
    // explicitly from clicks in the view — not from Qt selection state.
    void setActive(int idx);
    // Reposition the handles to follow a live drag of the active annotation.
    void syncHandles();
    // Fold user drags (Qt movable items) back into the data model; true if
    // anything moved. Caller rebuilds afterwards.
    bool commitMoves(std::vector<Annotation>& annotations);

private:
    void buildGrid(int w, int h, const Wcs& wcs);
    void buildAnnotations(const std::vector<Annotation>& annotations);
    // Resize grab-handles for the currently selected annotation (ellipse axes,
    // line endpoints). Rebuilt on every scene selection change.
    void rebuildHandles();
    // Pick a "nice" grid step in degrees for roughly `target` lines across span.
    static double niceStepDeg(double spanDeg, int target);

    QGraphicsScene* m_scene = nullptr;
    QGraphicsItemGroup* m_group = nullptr;   // owns all overlay items
    std::vector<Annotation> m_lastAnns;      // model copy for handle geometry
    std::vector<QGraphicsItem*> m_handles;
    int m_active = -1;                       // annotation index showing handles
    bool m_rebuilding = false;
    bool m_gridVisible = false;
    bool m_inverted = false;
};

} // namespace astro

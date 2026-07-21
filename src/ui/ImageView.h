#pragma once
//
// ImageView — the central canvas. Shows the stretched image and provides the
// inspection interactions from the mockup:
//   * left-drag a rectangle  -> zoom to that region
//   * wheel                  -> zoom in / out at cursor
//   * right-drag or middle-drag -> pan (right-click without drag -> context menu)
//   * Shift + left-drag      -> pan
//   * hover                  -> emit the pixel value under the cursor
//
#include <QGraphicsView>
#include "core/ImageData.h"

class QGraphicsScene;
class QGraphicsPixmapItem;
class QRubberBand;

namespace astro {

class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);

    void setDisplayImage(const QImage& img);
    void setSource(const ImageData* img) { m_src = img; }   // for pixel readout
    void zoomToFit();
    void zoomActualSize();          // 1:1 — one image pixel per screen pixel, centred on the current view

    // The image-pixel rectangle currently visible in the viewport, clamped to
    // the image bounds (empty if nothing is shown). Used for region export.
    QRect visibleImageRect() const;

    // Annotation drawing tools: while a tool is armed, left-drag draws the
    // shape (dashed preview) instead of the zoom rectangle; Text is a single
    // click. The tool disarms itself after each shape.
    enum class DrawTool { None, Ellipse, Line, Text };
    void setDrawTool(DrawTool t);
    DrawTool drawTool() const { return m_tool; }

    bool hasImage() const { return m_item != nullptr; }
    QSizeF imageSize() const;             // scene-rect size (image pixels)
    // Copy another view's zoom + centre (linked navigation); re-emission guarded.
    void adoptNavigation(const ImageView* src);

signals:
    void pixelHovered(int x, int y, double r, double g, double b, bool valid);
    void viewNavigated();                 // user zoomed/panned/fit this view
    // Right-click without drag. x/y are image pixels; onImage says whether the
    // click landed inside the image bounds.
    void contextMenuRequested(const QPoint& globalPos, int x, int y, bool onImage);
    // Drawing-tool results, in image-pixel coordinates.
    void ellipseDrawn(double cx, double cy, double a, double b);
    void lineDrawn(double x1, double y1, double x2, double y2);
    void textPointPicked(double x, double y);
    void drawToolFinished();       // tool disarmed — uncheck the toolbar button
    void annotationsEdited();      // user finished dragging annotation items
    // Left-press resolved: on an annotation item (isHandle for resize squares)
    // or empty sky. MainWindow uses it to drive which annotation shows handles.
    void annotationPressed(const QPointF& scenePos, bool isHandle);
    void annotationDragged();      // live item drag in progress (each move tick)
    void annotationDoubleClicked(const QPointF& scenePos);   // open the edit dialog

protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_item = nullptr;
    QRubberBand* m_band = nullptr;
    QPoint m_press;
    bool m_banding = false;
    bool m_panning = false;
    QPoint m_panLast;
    QPoint m_panStart;
    DrawTool m_tool = DrawTool::None;
    bool m_drawing = false;
    bool m_itemDrag = false;               // interacting with a selectable annotation
    bool m_handleDrag = false;             // that item is a resize handle
    QPointF m_drawStart;
    QGraphicsItem* m_preview = nullptr;    // dashed rubber shape while dragging
    const ImageData* m_src = nullptr;
    bool m_adopting = false;               // suppress viewNavigated during adoptNavigation
};

} // namespace astro

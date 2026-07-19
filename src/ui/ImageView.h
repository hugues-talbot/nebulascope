#pragma once
//
// ImageView — the central canvas. Shows the stretched image and provides the
// inspection interactions from the mockup:
//   * left-drag a rectangle  -> zoom to that region
//   * wheel                  -> zoom in / out at cursor
//   * right-drag or middle-drag -> pan
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

signals:
    void pixelHovered(int x, int y, double r, double g, double b, bool valid);

protected:
    void mousePressEvent(QMouseEvent*) override;
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
    const ImageData* m_src = nullptr;
};

} // namespace astro

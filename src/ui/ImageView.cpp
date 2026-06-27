#include "ui/ImageView.h"
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QRubberBand>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <cmath>

namespace astro {

ImageView::ImageView(QWidget* parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setBackgroundBrush(QColor("#05070a"));
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::NoDrag);
}

void ImageView::setDisplayImage(const QImage& img) {
    const QPixmap pm = QPixmap::fromImage(img);
    if (!m_item) {
        m_item = m_scene->addPixmap(pm);
        m_scene->setSceneRect(m_item->boundingRect());
        zoomToFit();
    } else {
        m_item->setPixmap(pm);
        m_scene->setSceneRect(m_item->boundingRect());
    }
}

void ImageView::zoomToFit() {
    if (m_item) fitInView(m_item->boundingRect(), Qt::KeepAspectRatio);
}

QRect ImageView::visibleImageRect() const {
    if (!m_item) return QRect();
    // Scene coordinates map 1:1 to image pixels (pixmap at origin, unscaled item).
    const QRectF vis = mapToScene(viewport()->rect()).boundingRect();
    const QRectF inter = vis.intersected(m_item->boundingRect());
    if (inter.width() < 1 || inter.height() < 1) return QRect();
    return inter.toRect();
}

void ImageView::mousePressEvent(QMouseEvent* e) {
    // Shift + left-drag pans the canvas (instead of rubber-band zoom).
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier)) {
        setDragMode(QGraphicsView::ScrollHandDrag);
        QGraphicsView::mousePressEvent(e);              // base class drives the pan
        return;
    }
    if (e->button() == Qt::LeftButton) {
        m_press = e->pos();
        m_banding = true;
        if (!m_band) m_band = new QRubberBand(QRubberBand::Rectangle, this);
        m_band->setGeometry(QRect(m_press, QSize()));
        m_band->show();
        return;
    }
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {  // pan
        m_panning = true;
        m_panLast = e->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    QGraphicsView::mousePressEvent(e);
}

void ImageView::mouseMoveEvent(QMouseEvent* e) {
    if (m_banding && m_band) m_band->setGeometry(QRect(m_press, e->pos()).normalized());

    if (m_panning) {                                 // right/middle-button pan
        const QPoint d = e->pos() - m_panLast;
        m_panLast = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
    }

    if (m_src) {
        const QPointF sp = mapToScene(e->pos());
        const int x = int(std::floor(sp.x()));
        const int y = int(std::floor(sp.y()));
        if (x >= 0 && y >= 0 && x < m_src->width() && y < m_src->height()) {
            const std::size_t i = std::size_t(y) * m_src->width() + x;
            double r, g, b;
            if (m_src->channels() >= 3) {
                r = m_src->plane<float>(0)[i];
                g = m_src->plane<float>(1)[i];
                b = m_src->plane<float>(2)[i];
            } else {
                r = g = b = m_src->plane<float>(0)[i];
            }
            emit pixelHovered(x, y, r, g, b, true);
        } else {
            emit pixelHovered(0, 0, 0, 0, 0, false);
        }
    }
    QGraphicsView::mouseMoveEvent(e);
}

void ImageView::mouseReleaseEvent(QMouseEvent* e) {
    if (m_panning && (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton)) {
        m_panning = false;
        unsetCursor();
        return;
    }
    // End a Shift-pan: hand the release to the base class while still in
    // ScrollHandDrag, then return to the default no-drag mode.
    if (dragMode() == QGraphicsView::ScrollHandDrag && e->button() == Qt::LeftButton) {
        QGraphicsView::mouseReleaseEvent(e);
        setDragMode(QGraphicsView::NoDrag);
        return;
    }
    if (e->button() == Qt::LeftButton && m_banding && m_band) {
        m_banding = false;
        const QRect r = m_band->geometry();
        m_band->hide();
        if (r.width() > 8 && r.height() > 8) {                       // drag = zoom to box
            const QRectF sr = mapToScene(r).boundingRect();
            fitInView(sr, Qt::KeepAspectRatio);
        }
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void ImageView::wheelEvent(QWheelEvent* e) {
    const double f = e->angleDelta().y() > 0 ? 1.2 : (1.0 / 1.2);
    scale(f, f);
}

} // namespace astro

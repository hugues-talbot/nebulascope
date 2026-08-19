#include "ui/ImageView.h"
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QPen>
#include <QPainter>
#include <QRubberBand>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QScrollBar>
#include <cmath>

namespace astro {

ImageView::ImageView(QWidget* parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setBackgroundBrush(QColor("#05070a"));
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    setAcceptDrops(true);          // image-list rows can be dropped onto a cell
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::NoDrag);
    // Any scrollbar motion (wheel, pans, zoom side-effects) counts as navigation.
    auto nav = [this] { if (!m_adopting) emit viewNavigated(); };
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, nav);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, nav);
}

QSizeF ImageView::imageSize() const {
    return m_item ? m_item->boundingRect().size() : QSizeF();
}

void ImageView::adoptNavigation(const ImageView* src) {
    adoptNavigationCalibrated(src, QTransform(), QTransform());
}

void ImageView::adoptNavigationCalibrated(const ImageView* src, const QTransform& srcWorld,
                                          const QTransform& dstWorld) {
    if (m_adopting || !m_item) return;
    m_adopting = true;
    // Qt composition applies the LEFT factor first. The shared-frame condition
    // "a world point lands at the same viewport position in both views" gives
    //   dstWorld^-1 * V_dst == srcWorld^-1 * V_src
    // so the desired scene->viewport map for this view is:
    const QTransform M = dstWorld * srcWorld.inverted() * src->viewportTransform();
    setTransform(QTransform(M.m11(), M.m12(), M.m21(), M.m22(), 0, 0));
    centerOn(M.inverted().map(QPointF(viewport()->rect().center())));
    m_adopting = false;
}

void ImageView::clearDisplay() {
    if (m_item) { m_scene->removeItem(m_item); delete m_item; m_item = nullptr; }
    m_scene->setSceneRect(QRectF());
    m_src = nullptr;
    resetTransform();
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

// Sampling mode follows the zoom DIRECTION, chosen at paint time so every
// zoom path is covered. Minified: bilinear — nearest-neighbour would take
// every Nth pixel, which phase-locks with periodic fixed-pattern structure
// (CMOS banding) and beats it into large moiré blocks at specific zoom-out
// levels. At 1:1 and magnified: nearest-neighbour — pixels stay crisp
// squares instead of bilinear blur.
void ImageView::paintEvent(QPaintEvent* e) {
    if (m_item) {
        const qreal s = std::sqrt(std::abs(transform().determinant()));
        const Qt::TransformationMode want =
            s < 0.999 ? Qt::SmoothTransformation : Qt::FastTransformation;
        if (m_item->transformationMode() != want)
            m_item->setTransformationMode(want);   // schedules one clean repaint
    }
    QGraphicsView::paintEvent(e);
}

void ImageView::setMarker(int x, int y) {
    if (x == m_markX && y == m_markY) return;
    m_markX = x; m_markY = y;
    viewport()->update();
}

// Crosshair with a small open centre (the measured pixel stays visible),
// drawn in scene = image coordinates so it tracks the pixel through
// zoom/pan; the pen is cosmetic (1 px on screen at any zoom). Two-tone —
// a dark halo under a light line — so it reads on both sky and stars.
void ImageView::drawForeground(QPainter* p, const QRectF& rect) {
    QGraphicsView::drawForeground(p, rect);
    if (m_markX < 0 || m_markY < 0 || !m_item) return;
    const QPointF c(m_markX + 0.5, m_markY + 0.5);              // pixel centre
    const qreal s = std::sqrt(std::abs(transform().determinant()));  // px per image px
    const qreal gap = 4.0 / s, arm = 14.0 / s;                    // screen-constant sizes
    auto lines = [&](QPainter& q) {
        q.drawLine(QPointF(c.x() - arm, c.y()), QPointF(c.x() - gap, c.y()));
        q.drawLine(QPointF(c.x() + gap, c.y()), QPointF(c.x() + arm, c.y()));
        q.drawLine(QPointF(c.x(), c.y() - arm), QPointF(c.x(), c.y() - gap));
        q.drawLine(QPointF(c.x(), c.y() + gap), QPointF(c.x(), c.y() + arm));
    };
    p->save();
    p->setRenderHint(QPainter::Antialiasing, false);
    QPen halo(QColor(0, 0, 0, 170), 3.0);
    halo.setCosmetic(true);
    p->setPen(halo);
    lines(*p);
    QPen line(QColor(255, 235, 120), 1.0);                        // warm, unlike any palette
    line.setCosmetic(true);
    p->setPen(line);
    lines(*p);
    p->restore();
}

void ImageView::zoomToFit() {
    if (m_item) fitInView(m_item->boundingRect(), Qt::KeepAspectRatio);
    if (!m_adopting) emit viewNavigated();     // scale change may not move scrollbars
}

void ImageView::zoomActualSize() {
    if (!m_item) return;
    const QPointF centre = mapToScene(viewport()->rect().center());  // keep the current view centre
    resetTransform();                                                // scale = 1 → 1 image px per screen px
    centerOn(centre);
    if (!m_adopting) emit viewNavigated();
}

QRect ImageView::visibleImageRect() const {
    if (!m_item) return QRect();
    // Scene coordinates map 1:1 to image pixels (pixmap at origin, unscaled item).
    const QRectF vis = mapToScene(viewport()->rect()).boundingRect();
    const QRectF inter = vis.intersected(m_item->boundingRect());
    if (inter.width() < 1 || inter.height() < 1) return QRect();
    return inter.toRect();
}

void ImageView::setDrawTool(DrawTool t) {
    m_tool = t;
    if (m_preview) { scene()->removeItem(m_preview); delete m_preview; m_preview = nullptr; }
    m_drawing = false;
    if (t == DrawTool::None) unsetCursor();
    else setCursor(Qt::CrossCursor);
}

// ---- image-list drag & drop -------------------------------------------------
// The list's drags carry the row's key in this custom format. Accept as a
// COPY: if the drop resolved as a move, the list (in drag-drop mode) would
// delete the dragged row. Anything else (e.g. Finder file drags) is ignored
// so it propagates to MainWindow's window-level file-drop handling.
static const char* kListKeyMime = "application/x-nebulascope-listkey";

void ImageView::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat(kListKeyMime)) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    } else {
        e->ignore();
    }
}

void ImageView::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasFormat(kListKeyMime)) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    } else {
        e->ignore();
    }
}

void ImageView::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasFormat(kListKeyMime)) { e->ignore(); return; }
    e->setDropAction(Qt::CopyAction);
    e->accept();
    emit listKeyDropped(QString::fromUtf8(e->mimeData()->data(kListKeyMime)));
}

void ImageView::mousePressEvent(QMouseEvent* e) {
    // Shift + left-drag pans the canvas (instead of rubber-band zoom).
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier)) {
        setDragMode(QGraphicsView::ScrollHandDrag);
        QGraphicsView::mousePressEvent(e);              // base class drives the pan
        return;
    }
    if (e->button() == Qt::LeftButton && m_tool != DrawTool::None) {
        const QPointF sp = mapToScene(e->pos());
        if (m_tool == DrawTool::Text) {
            const double tx = sp.x(), ty = sp.y();
            setDrawTool(DrawTool::None);
            emit textPointPicked(tx, ty);
            emit drawToolFinished();
            return;
        }
        if (m_tool == DrawTool::Register) {
            // One pick per arming: MainWindow re-arms the views for the
            // next pick (the tool stays cross-cursor on the OTHER cells
            // until the pair completes).
            const double rx = sp.x(), ry = sp.y();
            setDrawTool(DrawTool::None);
            emit registerPointPicked(rx, ry);
            return;
        }
        m_drawing = true;
        m_drawStart = sp;
        QPen pen(QColor("#8fc0f5"), 0, Qt::DashLine);
        m_preview = (m_tool == DrawTool::Line)
            ? static_cast<QGraphicsItem*>(scene()->addLine(QLineF(sp, sp), pen))
            : static_cast<QGraphicsItem*>(scene()->addEllipse(QRectF(sp, sp), pen));
        m_preview->setZValue(20);
        return;
    }
    if (e->button() == Qt::LeftButton) {
        // Click on a selectable annotation → Qt select/move, not the zoom band.
        if (QGraphicsItem* hit = itemAt(e->pos())) {
            QGraphicsItem* p = hit;
            while (p && !(p->flags() & QGraphicsItem::ItemIsSelectable)) p = p->parentItem();
            if (p) {
                m_itemDrag = true;
                m_handleDrag = p->data(1).isValid();
                QGraphicsView::mousePressEvent(e);
                if (!p->isSelected()) {
                    scene()->clearSelection();
                    p->setSelected(true);
                }
                emit annotationPressed(mapToScene(e->pos()), p->data(1).isValid());
                return;
            }
        }
        emit annotationPressed(mapToScene(e->pos()), false);   // empty sky → clears handles
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
        m_panStart = e->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    QGraphicsView::mousePressEvent(e);
}

void ImageView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && m_tool == DrawTool::None) {
        if (QGraphicsItem* hit = itemAt(e->pos())) {
            QGraphicsItem* p = hit;
            while (p && !(p->flags() & QGraphicsItem::ItemIsSelectable)) p = p->parentItem();
            if (p && !p->data(1).isValid()) {              // annotation, not a handle
                emit annotationDoubleClicked(mapToScene(e->pos()));
                return;
            }
        }
    }
    QGraphicsView::mouseDoubleClickEvent(e);
}

void ImageView::mouseMoveEvent(QMouseEvent* e) {
    if (m_itemDrag) {
        QGraphicsView::mouseMoveEvent(e);         // base moves the item
        // Handles follow a body drag — but a HANDLE drag must not be synced,
        // or syncHandles() would snap it straight back to its home position.
        if (!m_handleDrag) emit annotationDragged();
        return;
    }
    if (m_drawing && m_preview) {
        const QPointF sp = mapToScene(e->pos());
        if (m_tool == DrawTool::Line) {
            static_cast<QGraphicsLineItem*>(m_preview)->setLine(QLineF(m_drawStart, sp));
        } else {
            const double a = std::fabs(sp.x() - m_drawStart.x());
            const double b = std::fabs(sp.y() - m_drawStart.y());
            static_cast<QGraphicsEllipseItem*>(m_preview)->setRect(
                m_drawStart.x() - a, m_drawStart.y() - b, 2 * a, 2 * b);
        }
        return;
    }
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
    if (m_itemDrag && e->button() == Qt::LeftButton) {
        m_itemDrag = false;
        m_handleDrag = false;
        QGraphicsView::mouseReleaseEvent(e);
        emit annotationsEdited();                 // commit any drag into the model
        return;
    }
    if (m_drawing && e->button() == Qt::LeftButton) {
        const QPointF sp = mapToScene(e->pos());
        const DrawTool tool = m_tool;
        setDrawTool(DrawTool::None);                 // clears preview + m_drawing
        if (tool == DrawTool::Line) {
            if (QLineF(m_drawStart, sp).length() > 3)
                emit lineDrawn(m_drawStart.x(), m_drawStart.y(), sp.x(), sp.y());
        } else {
            const double a = std::fabs(sp.x() - m_drawStart.x());
            const double b = std::fabs(sp.y() - m_drawStart.y());
            if (a > 2 || b > 2)
                emit ellipseDrawn(m_drawStart.x(), m_drawStart.y(), std::max(a, 2.0), std::max(b, 2.0));
        }
        emit drawToolFinished();
        return;
    }
    if (m_panning && (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton)) {
        m_panning = false;
        unsetCursor();
        // A right-click that never really moved is a context-menu request, not a pan.
        if (e->button() == Qt::RightButton &&
            (e->pos() - m_panStart).manhattanLength() < 4) {
            const QPointF sp = mapToScene(e->pos());
            const int x = int(std::floor(sp.x()));
            const int y = int(std::floor(sp.y()));
            const bool onImage = m_src && x >= 0 && y >= 0 &&
                                 x < m_src->width() && y < m_src->height();
            emit contextMenuRequested(e->globalPosition().toPoint(), x, y, onImage);
        }
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
            if (!m_adopting) emit viewNavigated();
        }
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void ImageView::zoomBy(double factor) {
    if (factor <= 0.0 || !scene()) return;
    // Keyboard zoom: anchor on the viewport centre (the wheel anchors on the
    // cursor), then restore the wheel behaviour.
    const ViewportAnchor prev = transformationAnchor();
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scale(factor, factor);
    setTransformationAnchor(prev);
    if (!m_adopting) emit viewNavigated();
}

void ImageView::wheelEvent(QWheelEvent* e) {
    // Shift = fine zoom for precise framing (e.g. before calibration-linking).
    // NOTE: with Shift held, Qt reports the wheel on the X axis (horizontal-
    // scroll emulation) — read whichever axis actually carries the motion.
    const int delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    if (delta == 0) return;
    const double step = (e->modifiers() & Qt::ShiftModifier) ? 1.04 : 1.2;
    const double f = delta > 0 ? step : (1.0 / step);
    scale(f, f);
    if (!m_adopting) emit viewNavigated();
}

} // namespace astro

#pragma once
//
// ViewGrid — the split main view: an R×C (max 5×5) grid of ViewCells. Exactly
// one cell is ACTIVE (accent border); the histogram/info docks, annotation
// tools, rotation, and the image list all operate on it — MainWindow swaps its
// current-image state in and out of cells on activation, so every existing
// per-path mechanism (stretch memory, sidecars, orientation history, undo
// guards) keeps working unchanged.
//
// Navigation linking: cells holding images of the SAME pixel dimensions share
// zoom/pan by default (each drives the others' view transform); different
// dimensions never link. The ⇄ button on each cell opts it out.
//
#include <QFrame>
#include <QWidget>
#include <QTransform>
#include <vector>
#include "core/ImageData.h"
#include "core/ImageHeader.h"
#include "core/ImageStats.h"
#include "core/Wcs.h"
#include "render/StretchModel.h"

class QGridLayout;
class QLabel;
class QToolButton;

namespace astro {

class ImageView;
class AnnotationLayer;

class ViewCell : public QFrame {
    Q_OBJECT
public:
    explicit ViewCell(int index, QWidget* parent = nullptr);

    ImageView* view() const { return m_view; }
    AnnotationLayer* layer() const { return m_layer; }
    int index() const { return m_index; }
    bool occupied() const;
    void clearContent();                  // drop image/path/stretch; show placeholder
    bool linkEnabled() const;
    void setActive(bool on);
    void refreshChrome();                 // placeholder / link-button visibility

    // Stash of MainWindow's current-image state while this cell is inactive.
    // (Moved in/out on activation swaps; see MainWindow::onCellSwap.)
    ImageData image;
    ImageHeader header;
    QString path;
    Wcs wcs;
    StretchModel::State stretch{};
    bool hasStretch = false;
    std::vector<ChannelStats> stats;

    // Calibrated linking: maps this cell's scene onto the link group's shared
    // frame. Identity + !calibrated = the automatic same-size link.
    QTransform world;
    bool calibrated = false;

signals:
    void pressed(ViewCell* self);         // any mouse press inside → activate
    void linkToggled(ViewCell* self, bool on);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    int m_index;
    ImageView* m_view = nullptr;
    AnnotationLayer* m_layer = nullptr;
    QLabel* m_placeholder = nullptr;
    QToolButton* m_linkBtn = nullptr;
    bool m_active = false;
};

class ViewGrid : public QWidget {
    Q_OBJECT
public:
    explicit ViewGrid(QWidget* parent = nullptr);

    void setGrid(int rows, int cols);     // clamped to 1..5 each
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }
    ViewCell* activeCell() const { return m_cells.empty() ? nullptr : m_cells[m_active]; }
    ViewCell* cellAt(int i) const { return (i >= 0 && i < int(m_cells.size())) ? m_cells[std::size_t(i)] : nullptr; }
    void activate(ViewCell* c);
    void clearAll();                      // empty every cell (last image closed)
    ViewCell* firstEmptyVisible();        // first shown cell with no image, or null
    // Show/hide the views' scrollbars ("elevators") — panning still works via
    // right-drag / Shift-drag; applies to every cell, current and future.
    void setScrollBarsVisible(bool on);
    bool scrollBarsVisible() const { return m_scrollbars; }

signals:
    // Emitted BEFORE the active highlight moves, so MainWindow can stash the
    // old cell's state and adopt the new cell's. Not emitted for the first cell.
    void aboutToActivate(ViewCell* current, ViewCell* next);
    void viewCreated(ImageView* v);       // hook up app-level signals once per view
    void linkMessage(const QString& text);  // calibration feedback for the status bar

public:
    // The active image's pixels were remapped by `forward` (old scene → new
    // scene: rotate/flip). Carry any link calibration through it instead of
    // breaking the link; same-size auto-links are promoted to calibrated ones
    // first (identity worlds) so they survive the dimension change.
    void remapActiveScene(const QTransform& forward);

private:
    static bool linkablePair(const ViewCell* a, const ViewCell* b);
    void onLinkToggled(ViewCell* c, bool on);
    ViewCell* makeCell();
    void relayout();
    void onNavigated(ViewCell* c);        // propagate zoom/pan to linked cells

    QGridLayout* m_lay = nullptr;
    std::vector<ViewCell*> m_cells;       // persists across regrids (content kept)
    int m_rows = 1, m_cols = 1;
    std::size_t m_active = 0;
    bool m_scrollbars = true;
};

} // namespace astro

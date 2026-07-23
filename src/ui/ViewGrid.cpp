#include "ui/ViewGrid.h"
#include "ui/ImageView.h"
#include "ui/AnnotationLayer.h"
#include <QGridLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QEvent>
#include <QMouseEvent>

namespace astro {

// ---- ViewCell ---------------------------------------------------------------

ViewCell::ViewCell(int index, QWidget* parent) : QFrame(parent), m_index(index) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 2, 2, 2);
    m_view = new ImageView(this);
    m_layer = new AnnotationLayer(m_view->scene(), this);
    lay->addWidget(m_view);

    m_placeholder = new QLabel(QStringLiteral("Click here, then pick an image\nfrom the Open Images list"), this);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setStyleSheet(QStringLiteral("color:#3f4c5a;font-size:12px;background:transparent;border:none;"));
    m_placeholder->setAttribute(Qt::WA_TransparentForMouseEvents);   // clicks reach the view

    m_linkBtn = new QToolButton(this);
    m_linkBtn->setText(QStringLiteral("\u21c4"));
    m_linkBtn->setCheckable(true);
    m_linkBtn->setChecked(true);
    m_linkBtn->setToolTip(QStringLiteral(
        "Linked navigation.\nSame-size images link automatically.\nDifferent sizes: align both views as desired,\n"
        "then tick \u21c4 here to calibrate-link them at this alignment."));
    connect(m_linkBtn, &QToolButton::toggled, this,
            [this](bool on) { emit linkToggled(this, on); });
    m_linkBtn->setStyleSheet(QStringLiteral(
        "QToolButton{color:#5b6876;background:rgba(10,15,21,0.7);border:1px solid #1f2b37;border-radius:4px;padding:1px 5px;}"
        "QToolButton:checked{color:#8fc0f5;border-color:#2a557e;}"));

    m_view->viewport()->installEventFilter(this);
    setActive(false);
    refreshChrome();
}

bool ViewCell::occupied() const { return m_view->hasImage(); }
bool ViewCell::linkEnabled() const { return m_linkBtn->isChecked(); }

void ViewCell::clearContent() {
    image = ImageData();
    header = ImageHeader();
    path.clear();
    wcs = Wcs();
    hasStretch = false;
    stats.clear();
    world = QTransform();
    calibrated = false;
    if (m_linkBtn) m_linkBtn->setChecked(false);
    m_view->clearDisplay();
    refreshChrome();
}

void ViewCell::setActive(bool on) {
    m_active = on;
    setStyleSheet(QStringLiteral("astro--ViewCell{border:%1;}")
                      .arg(on ? QStringLiteral("2px solid #2a557e")
                              : QStringLiteral("1px solid #18222d")));
}

void ViewCell::refreshChrome() {
    m_placeholder->setVisible(!occupied());
    m_linkBtn->setVisible(occupied());
}

bool ViewCell::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_view->viewport() && ev->type() == QEvent::MouseButtonPress)
        emit pressed(this);               // activate BEFORE the view handles it
    return false;
}

void ViewCell::resizeEvent(QResizeEvent* e) {
    QFrame::resizeEvent(e);
    m_placeholder->setGeometry(rect());
    m_linkBtn->move(width() - m_linkBtn->sizeHint().width() - 8, 8);
    m_linkBtn->resize(m_linkBtn->sizeHint());
    m_linkBtn->raise();
}

void ViewCell::mousePressEvent(QMouseEvent* e) {
    emit pressed(this);                  // clicks on the frame/border also activate
    QFrame::mousePressEvent(e);
}

// ---- ViewGrid ---------------------------------------------------------------

ViewGrid::ViewGrid(QWidget* parent) : QWidget(parent) {
    m_lay = new QGridLayout(this);
    m_lay->setContentsMargins(0, 0, 0, 0);
    m_lay->setSpacing(3);
}

ViewCell* ViewGrid::makeCell() {
    auto* c = new ViewCell(int(m_cells.size()), this);
    const Qt::ScrollBarPolicy pol = m_scrollbars ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff;
    c->view()->setHorizontalScrollBarPolicy(pol);
    c->view()->setVerticalScrollBarPolicy(pol);
    connect(c, &ViewCell::pressed, this, &ViewGrid::activate);
    connect(c, &ViewCell::linkToggled, this, &ViewGrid::onLinkToggled);
    connect(c->view(), &ImageView::viewNavigated, this, [this, c] { onNavigated(c); });
    m_cells.push_back(c);
    emit viewCreated(c->view());
    return c;
}

void ViewGrid::setGrid(int rows, int cols) {
    m_rows = std::max(1, std::min(5, rows));
    m_cols = std::max(1, std::min(5, cols));
    const std::size_t need = std::size_t(m_rows) * m_cols;
    const bool first = m_cells.empty();
    while (m_cells.size() < need) makeCell();
    if (first) { m_active = 0; m_cells[0]->setActive(true); }
    // A shrink can hide the active cell — move activity to the top-left first.
    if (m_active >= need) activate(m_cells[0]);
    relayout();
}

void ViewGrid::relayout() {
    // Remove everything, then re-place the first rows*cols cells; hide extras
    // (they keep their images, so growing the grid back restores them).
    while (QLayoutItem* it = m_lay->takeAt(0)) delete it;
    const std::size_t shown = std::size_t(m_rows) * m_cols;
    for (std::size_t i = 0; i < m_cells.size(); ++i) {
        if (i < shown) {
            m_lay->addWidget(m_cells[i], int(i) / m_cols, int(i) % m_cols);
            m_cells[i]->show();
            m_cells[i]->refreshChrome();
        } else {
            m_cells[i]->hide();
        }
    }
    for (int r = 0; r < m_rows; ++r) m_lay->setRowStretch(r, 1);
    for (int c = 0; c < m_cols; ++c) m_lay->setColumnStretch(c, 1);
}

void ViewGrid::setScrollBarsVisible(bool on) {
    m_scrollbars = on;
    const Qt::ScrollBarPolicy pol = on ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff;
    for (ViewCell* c : m_cells) {
        c->view()->setHorizontalScrollBarPolicy(pol);
        c->view()->setVerticalScrollBarPolicy(pol);
    }
}

void ViewGrid::clearAll() {
    for (ViewCell* c : m_cells) c->clearContent();
}

void ViewGrid::activate(ViewCell* c) {
    if (!c || c == activeCell()) return;
    ViewCell* old = activeCell();
    emit aboutToActivate(old, c);         // MainWindow swaps its state now
    if (old) old->setActive(false);
    for (std::size_t i = 0; i < m_cells.size(); ++i)
        if (m_cells[i] == c) m_active = i;
    c->setActive(true);
    c->refreshChrome();
}

bool ViewGrid::linkablePair(const ViewCell* a, const ViewCell* b) {
    if (!a->linkEnabled() || !b->linkEnabled() || !a->occupied() || !b->occupied()) return false;
    if (a->view()->imageSize() == b->view()->imageSize()) return true;   // automatic
    return a->calibrated && b->calibrated;                               // manual, calibrated
}

// Ticking ⇄ on a cell whose image differs in size from another linked cell
// captures the CURRENT relative view state as the calibration: "what both
// views show right now corresponds". Unticking forgets it.
void ViewGrid::onLinkToggled(ViewCell* c, bool on) {
    if (!on) {
        if (c->calibrated) emit linkMessage(QStringLiteral("View unlinked — calibration forgotten"));
        c->calibrated = false;
        c->world = QTransform();
        return;
    }
    if (!c->occupied()) return;
    // Anchor: prefer the active cell, else the first other linked occupied cell.
    ViewCell* anchor = nullptr;
    if (activeCell() != c && activeCell() && activeCell()->occupied() && activeCell()->linkEnabled())
        anchor = activeCell();
    const std::size_t shown = std::size_t(m_rows) * m_cols;
    for (std::size_t i = 0; !anchor && i < m_cells.size() && i < shown; ++i) {
        ViewCell* o = m_cells[i];
        if (o != c && o->occupied() && o->linkEnabled()) anchor = o;
    }
    if (!anchor || anchor->view()->imageSize() == c->view()->imageSize()) return;   // same-size: automatic
    // Shared frame X = anchorWorld^-1 * V_anchor; calibrate so V_c == world_c * X.
    const QTransform X = anchor->world.inverted() * anchor->view()->viewportTransform();
    c->world = c->view()->viewportTransform() * X.inverted();
    c->calibrated = true;
    anchor->calibrated = true;            // keeps its current world (identity or prior)
    emit linkMessage(QStringLiteral("Views calibration-linked at the current alignment"));
}

void ViewGrid::remapActiveScene(const QTransform& forward) {
    ViewCell* c = activeCell();
    if (!c) return;
    // Promote live same-size auto-links to calibrated links (identity worlds
    // ARE their correspondence) — after the remap the sizes will differ.
    const std::size_t shown = std::size_t(m_rows) * m_cols;
    for (std::size_t i = 0; i < m_cells.size() && i < shown; ++i) {
        ViewCell* o = m_cells[i];
        if (o == c || !linkablePair(c, o)) continue;
        if (!(c->calibrated && o->calibrated)) { c->calibrated = true; o->calibrated = true; }
    }
    if (c->calibrated)
        c->world = forward.inverted() * c->world;   // w = W_old(F⁻¹(p_new))
}

void ViewGrid::onNavigated(ViewCell* c) {
    c->refreshChrome();
    if (!c->linkEnabled() || !c->occupied()) return;
    const std::size_t shown = std::size_t(m_rows) * m_cols;
    for (std::size_t i = 0; i < m_cells.size() && i < shown; ++i) {
        ViewCell* o = m_cells[i];
        if (o == c || !linkablePair(c, o)) continue;
        o->view()->adoptNavigationCalibrated(c->view(), c->world, o->world);
    }
}

} // namespace astro

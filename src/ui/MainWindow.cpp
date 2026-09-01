#include "ui/MainWindow.h"
#include <QSet>
#include <QColorSpace>
#include <QFileSystemModel>
#include "ui/ImageView.h"
#include "ui/HistogramPanel.h"
#include "ui/RotateDialog.h"
#include "ui/ViewGrid.h"
#include "ui/InfoPanel.h"
#include "ui/CombineDialog.h"
#include "ui/StarCombineDialog.h"
#include "io/ImageReader.h"
#include "io/FitsReader.h"
#include "app/AppInfo.h"
#include "app/BuildInfo.h"
#ifdef Q_OS_MACOS
#include "app/MacSavePanel.h"
#endif
#include "io/ImageWriter.h"
#include "core/Debayer.h"
#include "core/ImageStats.h"
#include "core/ColorTransport.h"
#include "core/SexCatalog.h"
#include "core/Preferences.h"
#include "ui/PreferencesDialog.h"
#include "render/DisplayRenderer.h"
#include "core/Colormap.h"
#include "core/Transform.h"
#include "core/Deconvolve.h"

#include <QDockWidget>
#include <QListWidget>
#include <QApplication>
#include <QCoreApplication>
#include <QClipboard>
#include <QtConcurrent/QtConcurrent>
#include <QInputDialog>
#include <QActionGroup>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QProgressBar>
#include <QTextEdit>
#include <QFontDatabase>
#include <QFormLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonArray>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QUndoStack>
#include <QUndoCommand>
#include <QFile>
#include <QMenu>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>
#include <QtMath>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QUrl>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextStream>
#include <QDir>
#include <QLabel>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QShortcut>
#include <QKeySequence>
#include <QSettings>
#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QWidget>
#include <QHBoxLayout>

namespace astro {

// Sidecar (de)serialization of the display adjustments — defined near
// writeAnnotationsFile, used earlier by displayPath's sidecar auto-load.
static QJsonObject adjustToJson(const AdjustParams& a);
static AdjustParams adjustFromJson(const QJsonObject& o);

MainWindow::MainWindow() {
    setWindowTitle(tr("NebulaScope — Inspector"));
    m_undo = new QUndoStack(this);
    m_annColor = Preferences::get().annColor;   // user default for new annotations
    buildUi();
    buildMenusAndToolbar();
    setAcceptDrops(true);          // drop FITS/XISF/images onto the window to open
    resize(1480, 940);
}

namespace {
// QListWidget whose drags also carry the dragged row's key, so a row can be
// dropped onto a specific view cell (ImageView accepts this format as a copy).
class ImageListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;
protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        QMimeData* md = QListWidget::mimeData(items);
        if (md && !items.isEmpty())
            md->setData(QStringLiteral("application/x-nebulascope-listkey"),
                        items.first()->data(Qt::UserRole).toString().toUtf8());
        return md;
    }
};
} // namespace

static bool looksLikeImage(const QString& path) {
    static const QStringList exts = {
        "fits","fit","fts","fz","xisf","jpg","jpeg","png","tif","tiff","txt" };
    return exts.contains(QFileInfo(path).suffix().toLower());
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (!e->mimeData()->hasUrls()) return;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && looksLikeImage(u.toLocalFile())) { e->acceptProposedAction(); return; }
}

void MainWindow::dropEvent(QDropEvent* e) {
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls()) {
        if (!u.isLocalFile()) continue;
        const QString p = u.toLocalFile();
        if (p.endsWith(".txt", Qt::CaseInsensitive)) importListFile(p);   // a saved list
        else if (looksLikeImage(p)) paths << p;
    }
    if (!paths.isEmpty()) { openPaths(paths); e->acceptProposedAction(); }
}

void MainWindow::buildUi() {
    // Central widget: the split-view grid. MainWindow's m_view/m_annotations
    // always point at the ACTIVE cell's view/layer; onCellSwap moves the
    // current-image state between cells on activation.
    m_grid = new ViewGrid(this);
    setCentralWidget(m_grid);
    connect(m_grid, &ViewGrid::viewCreated, this, &MainWindow::connectViewSignals);
    connect(m_grid, &ViewGrid::aboutToActivate, this, &MainWindow::onCellSwap);
    m_grid->setGrid(1, 1);
    connect(m_grid, &ViewGrid::linkMessage, this,
            [this](const QString& t) { statusBar()->showMessage(t, 5000); });
    // New/re-placed cells are created after the overlay boxes and would stack
    // above them — re-raise the panels whenever the grid changes.
    connect(m_grid, &ViewGrid::gridChanged, this,
            [this] { if (m_overlay) layoutOverlayPanels(); });
    m_view = m_grid->activeCell()->view();
    m_annotations = m_grid->activeCell()->layer();
    m_view->setSource(&m_image);

    // left dock: open images (with an append / remove / export button bar)
    m_leftDock = new QDockWidget(tr("Open Images"), this);
    m_leftDock->setObjectName("leftDock");
    auto* listHost = new QWidget(m_leftDock);
    auto* lv = new QVBoxLayout(listHost);
    lv->setContentsMargins(4, 4, 4, 4);
    lv->setSpacing(4);
    auto* bar = new QHBoxLayout();
    bar->setSpacing(4);
    auto* addBtn = new QToolButton(); addBtn->setText("+");  addBtn->setToolTip(tr("Append files\u2026"));
    auto* remBtn = new QToolButton(); remBtn->setText("\u2212"); remBtn->setToolTip(tr("Close & remove selected (Del)"));
    auto* expBtn = new QToolButton(); expBtn->setText("\u2913"); expBtn->setToolTip(tr("Export list\u2026"));
    bar->addWidget(addBtn);
    bar->addWidget(remBtn);
    bar->addStretch();
    bar->addWidget(expBtn);
    lv->addLayout(bar);

    m_fileList = new ImageListWidget(listHost);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // DragDrop (not InternalMove) so rows can ALSO be dragged onto a view
    // cell: InternalMove deletes the source row when an external target
    // accepts the drop. Internal reorders still default to a move, and the
    // model rejects foreign mime types, so no external junk can drop in.
    m_fileList->setDragDropMode(QAbstractItemView::DragDrop);
    m_fileList->setDefaultDropAction(Qt::MoveAction);               // drag to reorder
    lv->addWidget(m_fileList, 1);
    m_leftDock->setWidget(listHost);
    addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);
    connect(m_fileList, &QListWidget::currentRowChanged, this, &MainWindow::showRow);
    // A checkbox toggled on a row that belongs to a multi-row selection
    // applies the new state to every highlighted row. A click outside the
    // selection stays single-row, so a stray click can't retag a whole batch.
    // The model signal (not itemChanged) carries the role, which keeps text
    // edits (dedupe renames, save rebranding) from being mistaken for tags.
    connect(m_fileList->model(), &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& tl, const QModelIndex& br,
                   const QList<int>& roles) {
        if (m_tagPropagating || tl != br) return;
        if (!roles.contains(Qt::CheckStateRole)) return;
        QListWidgetItem* it = m_fileList->item(tl.row());
        if (!it || !it->isSelected()) return;
        const auto sel = m_fileList->selectedItems();
        if (sel.size() < 2) return;
        m_tagPropagating = true;
        const Qt::CheckState st = it->checkState();
        for (QListWidgetItem* o : sel)
            if (o != it && (o->flags() & Qt::ItemIsUserCheckable))
                o->setCheckState(st);
        m_tagPropagating = false;
        statusBar()->showMessage(
            st == Qt::Checked ? tr("%n image(s) checked (keep)", nullptr, int(sel.size()))
                              : tr("%n image(s) unchecked", nullptr, int(sel.size())), 2000);
    });
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileList, &QListWidget::customContextMenuRequested, this, &MainWindow::onListContextMenu);
    connect(addBtn, &QToolButton::clicked, this, &MainWindow::appendToList);
    connect(remBtn, &QToolButton::clicked, this, &MainWindow::removeSelected);
    connect(expBtn, &QToolButton::clicked, this, &MainWindow::exportList);

    auto* del = new QShortcut(QKeySequence::Delete, m_fileList);
    del->setContext(Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, &MainWindow::removeSelected);

    // left dock (tabbed): image info / FITS structure / header
    m_infoDock = new QDockWidget(tr("Info"), this);
    m_infoDock->setObjectName("infoDock");
    m_info = new InfoPanel(&m_model, m_infoDock);
    m_infoDock->setWidget(m_info);
    addDockWidget(Qt::LeftDockWidgetArea, m_infoDock);
    tabifyDockWidget(m_leftDock, m_infoDock);
    m_leftDock->raise();

    // right dock: histogram
    m_rightDock = new QDockWidget(tr("Histogram"), this);
    m_rightDock->setObjectName("rightDock");
    m_hist = new HistogramPanel(&m_model, m_rightDock);
    connect(m_hist, &HistogramPanel::interactiveDrag, this, &MainWindow::holdRenders);
    connect(m_hist, &HistogramPanel::applyToAllRequested,
            this, &MainWindow::applyStretchToAllList);
    // Common axis (RGB): switch the model's range policy; re-express the
    // current handles on the new ranges so the SCREEN does not change — only
    // the plot's axis does. Persisted as a preference.
    m_model.setCommonAxis(Preferences::get().commonAxis);
    m_hist->setCommonAxisChecked(Preferences::get().commonAxis);
    connect(m_hist, &HistogramPanel::commonAxisToggled, this, [this](bool on) {
        m_model.setCommonAxis(on);
        Preferences::get().commonAxis = on;
        Preferences::get().save();
        if (!m_image.isValid() || m_curStats.empty()) return;
        const int n = std::min<int>(int(m_curStats.size()), 3);
        double lo[3], hi[3];
        if (on && n > 1) {
            double pmn, pmx;
            StretchModel::pooledRange(m_curStats, n, pmn, pmx);
            for (int c = 0; c < 3; ++c) { lo[c] = pmn; hi[c] = std::max(pmn + 1e-6, pmx); }
        } else {
            for (int c = 0; c < 3; ++c) {
                const int si = std::min(c, n - 1);
                lo[c] = m_curStats[si].min;
                hi[c] = std::max(double(m_curStats[si].min) + 1e-6, double(m_curStats[si].max));
            }
        }
        // Not a stretch edit (display unchanged): squelch the undo coalescer.
        // (StretchSquelch is defined further down; inline its two steps.)
        flushStretchUndo();
        ++m_squelchStretch;
        m_model.rebaseRanges(lo, hi);
        if (--m_squelchStretch == 0) resyncStretchUndoBase();
        statusBar()->showMessage(on ? tr("Histogram: common axis — channels on one pooled range")
                                    : tr("Histogram: per-channel axis — each channel over its own range"), 3000);
    });
    m_hist->setSource(&m_image);
    m_rightDock->setWidget(m_hist);
    m_rightDock->setMinimumWidth(400);
    addDockWidget(Qt::RightDockWidgetArea, m_rightDock);

    m_pixelLabel = new QLabel("—");
    statusBar()->addPermanentWidget(m_pixelLabel);

    m_imgCache.setBudgetBytes(qint64(Preferences::get().imageCacheMB) * 1024 * 1024);
    connect(&m_model, &StretchModel::changed, this, &MainWindow::updateDisplay);
    m_renderWatcher = new QFutureWatcher<QImage>(this);
    connect(m_renderWatcher, &QFutureWatcher<QImage>::finished, this, &MainWindow::onRenderDone);

    // Keep the active image's stretch memory current: every edit (drag, tab,
    // Auto/Reset) is snapshotted under its path, so revisiting restores it.
    connect(&m_model, &StretchModel::changed, this, [this] {
        if (!m_currentPath.isEmpty()) m_stfByPath.insert(m_currentPath, m_model.state());
    });

    // Stretch undo history: coalesce a gesture's stream of changed() signals
    // into ONE undo command once the controls go quiet.
    m_stretchUndoTimer = new QTimer(this);
    m_stretchUndoTimer->setSingleShot(true);
    m_stretchUndoTimer->setInterval(700);
    connect(m_stretchUndoTimer, &QTimer::timeout, this, &MainWindow::pushStretchUndo);
    connect(&m_model, &StretchModel::changed, this, [this] {
        if (m_squelchStretch || m_currentPath.isEmpty()) return;
        if (m_undoBasePath != m_currentPath) { resyncStretchUndoBase(); return; }
        m_undoPendingNext = m_model.state();
        m_stretchUndoTimer->start();
    });

    // Interop: reload images that external tools overwrite on disk. Debounced —
    // suites write in bursts (or write-then-rename, which drops the watch).
    m_fileWatcher = new QFileSystemWatcher(this);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::onWatchedFileChanged);
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(600);
    connect(m_reloadTimer, &QTimer::timeout, this, &MainWindow::reloadChangedFiles);
}

// ---- undo commands -----------------------------------------------------------
// Snapshot-based: each annotation edit stores the before/after lists (cheap —
// tens of small structs), each transform stores its own inverse. Both classes
// skip their first redo() because the edit is applied where it happens.

namespace {

class AnnotationCmd : public QUndoCommand {
public:
    AnnotationCmd(MainWindow* w, QString path,
                  std::vector<Annotation> before, std::vector<Annotation> after,
                  const QString& text)
        : m_w(w), m_path(std::move(path)),
          m_before(std::move(before)), m_after(std::move(after)) { setText(text); }
    void undo() override { m_w->setAnnotations(m_path, m_before); }
    void redo() override {
        if (m_first) { m_first = false; return; }
        m_w->setAnnotations(m_path, m_after);
    }
private:
    MainWindow* m_w;
    QString m_path;
    std::vector<Annotation> m_before, m_after;
    bool m_first = true;
};

// Arbitrary rotation is absolute (a total angle) and always re-applied from the
// stashed pre-rotation base, so undo/redo only needs the two angles — no image
// snapshots, and both directions are a single exact resample from the base.
class RotateAngleCmd : public QUndoCommand {
public:
    RotateAngleCmd(MainWindow* w, QString path, double prevDeg, double nextDeg)
        : m_w(w), m_path(std::move(path)), m_prev(prevDeg), m_next(nextDeg) {
        setText(QCoreApplication::translate("astro::MainWindowHelpers", "rotate to %1\u00b0").arg(nextDeg));
    }
    void undo() override {
        if (m_w->currentPath() != m_path) { setObsolete(true); return; }
        m_w->rotateToAngle(m_prev);
    }
    void redo() override {
        if (m_first) { m_first = false; return; }
        if (m_w->currentPath() != m_path) { setObsolete(true); return; }
        m_w->rotateToAngle(m_next);
    }
private:
    MainWindow* m_w;
    QString m_path;
    double m_prev, m_next;
    bool m_first = true;
};

// Creating a synthetic image (combine / colour transport) is undoable: undo
// removes the list entry (full cleanup), redo re-registers the same pixels.
// If the entry was meanwhile saved to disk (rebranded to a file path), the
// mem:// key no longer exists and both directions safely no-op.
class SyntheticImageCmd : public QUndoCommand {
public:
    SyntheticImageCmd(MainWindow* w, QString key, QString name, std::shared_ptr<ImageData> img)
        : m_w(w), m_key(std::move(key)), m_name(std::move(name)), m_img(std::move(img)) {
        setText(QCoreApplication::translate("astro::MainWindowHelpers", "create %1").arg(m_name));
    }
    void undo() override { m_w->removeSyntheticEntry(m_key); }
    void redo() override {
        if (m_first) { m_first = false; return; }
        m_w->restoreSyntheticEntry(m_key, m_name, m_img);
    }
private:
    MainWindow* m_w;
    QString m_key, m_name;
    std::shared_ptr<ImageData> m_img;
    bool m_first = true;
};

MainWindow::Xform inverseXform(MainWindow::Xform x) {
    using X = MainWindow::Xform;
    if (x == X::RotCW)  return X::RotCCW;
    if (x == X::RotCCW) return X::RotCW;
    return x;                                    // flips are self-inverse
}

// Transforms only exist on the displayed image (a reload resets them), so a
// command whose image is no longer showing marks itself obsolete instead of
// corrupting whatever is on screen now.
class TransformCmd : public QUndoCommand {
public:
    TransformCmd(MainWindow* w, QString path, MainWindow::Xform x)
        : m_w(w), m_path(std::move(path)), m_x(x) { setText(QCoreApplication::translate("astro::MainWindowHelpers", "transform image")); }
    void undo() override {
        if (m_w->currentPath() != m_path) { setObsolete(true); return; }
        m_w->doTransform(inverseXform(m_x));
    }
    void redo() override {
        if (m_first) { m_first = false; return; }
        if (m_w->currentPath() != m_path) { setObsolete(true); return; }
        m_w->doTransform(m_x);
    }
private:
    MainWindow* m_w;
    QString m_path;
    MainWindow::Xform m_x;
    bool m_first = true;
};

} // namespace

// Map annotation geometry through an image rotation/flip. w/h are the image
// dimensions BEFORE the transform; pixel centres sit at integer coordinates,
// so a flip maps x -> (w-1)-x.
static void transformAnnotations(std::vector<Annotation>& anns, MainWindow::Xform t, int w, int h) {
    using Xform = MainWindow::Xform;
    auto mapPt = [&](double& x, double& y) {
        const double ox = x, oy = y;
        switch (t) {
            case Xform::RotCW:  x = (h - 1) - oy; y = ox; break;
            case Xform::RotCCW: x = oy; y = (w - 1) - ox; break;
            case Xform::FlipH:  x = (w - 1) - ox; break;
            case Xform::FlipV:  y = (h - 1) - oy; break;
        }
    };
    for (Annotation& a : anns) {
        mapPt(a.x, a.y);
        if (a.type == Annotation::Type::Line) mapPt(a.x2, a.y2);
        if (a.type == Annotation::Type::Ellipse) {
            switch (t) {
                case Xform::RotCW:  a.angleDeg += 90; break;
                case Xform::RotCCW: a.angleDeg -= 90; break;
                case Xform::FlipH:  a.angleDeg = 180 - a.angleDeg; break;
                case Xform::FlipV:  a.angleDeg = -a.angleDeg; break;
            }
        }
    }
}

// Map annotation geometry through an arbitrary rotation — the same forward map
// as the pixels and the WCS: p' = M(p - cOld) + cNew, M = [[c,s],[-s,c]],
// positive angle = visually CCW. Ellipse/text angles turn with the image.
static void rotateAnnotationsBy(std::vector<Annotation>& anns, double angleDeg,
                                int w, int h, int nw, int nh) {
    const double th = angleDeg * M_PI / 180.0;
    const double c = std::cos(th), s = std::sin(th);
    const double cox = (w - 1) / 2.0,  coy = (h - 1) / 2.0;
    const double cnx = (nw - 1) / 2.0, cny = (nh - 1) / 2.0;
    auto mapPt = [&](double& x, double& y) {
        const double dx = x - cox, dy = y - coy;
        x =  c * dx + s * dy + cnx;
        y = -s * dx + c * dy + cny;
    };
    for (Annotation& a : anns) {
        mapPt(a.x, a.y);
        if (a.type == Annotation::Type::Line) mapPt(a.x2, a.y2);
        if (a.type == Annotation::Type::Ellipse) a.angleDeg -= angleDeg;
    }
}

// Forward pixel maps (old scene → new scene) as QTransforms, matching the
// annotation/WCS maps exactly — used to carry view-link calibrations through.
static QTransform rotForwardTransform(double angleDeg, int w, int h, int nw, int nh) {
    const double th = angleDeg * M_PI / 180.0;
    const double c = std::cos(th), s = std::sin(th);
    const double cox = (w - 1) / 2.0,  coy = (h - 1) / 2.0;
    const double cnx = (nw - 1) / 2.0, cny = (nh - 1) / 2.0;
    // x' = c(x-cox)+s(y-coy)+cnx ; y' = -s(x-cox)+c(y-coy)+cny
    return QTransform(c, -s, s, c, cnx - c * cox - s * coy, cny + s * cox - c * coy);
}
static QTransform xformForwardTransform(MainWindow::Xform x, int w, int h) {
    using X = MainWindow::Xform;
    switch (x) {
        case X::RotCW:  return QTransform(0, 1, -1, 0, h - 1.0, 0);      // x'=(h-1)-y, y'=x
        case X::RotCCW: return QTransform(0, -1, 1, 0, 0, w - 1.0);      // x'=y, y'=(w-1)-x
        case X::FlipH:  return QTransform(-1, 0, 0, 1, w - 1.0, 0);
        case X::FlipV:  return QTransform(1, 0, 0, -1, 0, h - 1.0);
    }
    return QTransform();
}

void MainWindow::applyTransform(Xform x) {
    if (!m_image.isValid()) return;
    doTransform(x);
    m_undo->push(new TransformCmd(this, m_currentPath, x));   // first redo is skipped
    normalizeOrientation();
}

void MainWindow::doTransform(Xform x) {
    if (!m_image.isValid()) return;
    m_rotBasePath.clear();     // 90°/flip changes geometry — next rotation re-bases
    const int ow = m_image.width(), oh = m_image.height();   // pre-transform dims
    switch (x) {
        case Xform::RotCW:  m_image = rotate90(m_image, true);  break;
        case Xform::RotCCW: m_image = rotate90(m_image, false); break;
        case Xform::FlipH:  m_image = flipHorizontal(m_image);  break;
        case Xform::FlipV:  m_image = flipVertical(m_image);    break;
    }
    // Values are unchanged, so stretch/stats stay valid; only geometry differs.
    m_view->setSource(&m_image);
    updateDisplay();
    // Annotations live in image-pixel coordinates — carry them through the
    // same transform (and mark unsaved: the sidecar on disk is now stale).
    auto it = m_annByPath.find(m_currentPath);
    if (it != m_annByPath.end() && !it.value().empty()) {
        transformAnnotations(it.value(), x, ow, oh);
        m_annDirty.insert(m_currentPath);
    }
    // The astrometric solution follows the same pixel remap.
    if (m_wcs.valid()) {
        const Wcs::PixelXform px =
            x == Xform::RotCW  ? Wcs::PixelXform::RotCW  :
            x == Xform::RotCCW ? Wcs::PixelXform::RotCCW :
            x == Xform::FlipH  ? Wcs::PixelXform::FlipH  : Wcs::PixelXform::FlipV;
        m_wcs = m_wcs.transformed(px, ow, oh);
    }
    // Record the op so blink-back and the sidecar can reproduce the orientation
    // (an op followed by its inverse cancels instead of accumulating).
    QStringList& hist = m_xformByPath[m_currentPath];
    const QString inv = xformName(inverseXform(x));
    if (!hist.isEmpty() && hist.last() == inv) hist.removeLast();
    else hist << xformName(x);
    bumpXformRev(m_currentPath);
    m_grid->remapActiveScene(xformForwardTransform(x, ow, oh));   // links survive
    refreshAnnotations();
    const bool rotated = (x == Xform::RotCW || x == Xform::RotCCW);
    if (rotated) { m_view->zoomToFit(); m_lastW = m_image.width(); m_lastH = m_image.height(); }
}

// Arbitrary rotation: same pipeline as doTransform, but resampling. History
// records "rot:<deg>"; consecutive rotations merge (and cancel near 0°/360°).
void MainWindow::doRotateArbitrary(double angleDeg) {
    if (!m_image.isValid()) return;
    const int ow = m_image.width(), oh = m_image.height();
    m_image = rotateArbitrary(m_image, angleDeg);
    const int nw = m_image.width(), nh = m_image.height();
    m_grid->remapActiveScene(rotForwardTransform(angleDeg, ow, oh, nw, nh));   // links survive
    m_view->setSource(&m_image);
    updateDisplay();
    auto it = m_annByPath.find(m_currentPath);
    if (it != m_annByPath.end() && !it.value().empty()) {
        rotateAnnotationsBy(it.value(), angleDeg, ow, oh, nw, nh);
        m_annDirty.insert(m_currentPath);
    }
    if (m_wcs.valid()) m_wcs = m_wcs.rotated(angleDeg, ow, oh, nw, nh);
    // Record the op EXACTLY as applied — never merged. rot:a then rot:-a is NOT
    // the identity for the pixels (the canvas expands both times), so a merged
    // or cancelled entry would desynchronize the history from the pixels and
    // break disk-frame imports. rotateToAngle() keeps the chain short anyway by
    // restoring the base history before appending its single op.
    m_xformByPath[m_currentPath] << QStringLiteral("rot:%1").arg(angleDeg, 0, 'f', 4);
    bumpXformRev(m_currentPath);
    refreshAnnotations();
    m_view->zoomToFit();
    m_lastW = nw; m_lastH = nh;
    statusBar()->showMessage(
        tr("Rotated %1\u00b0 — resampled onto %2\u00d7%3 (corners are blank)")
            .arg(angleDeg).arg(nw).arg(nh), 4000);
}

double MainWindow::currentRotationAngle() const {
    // User-facing total: the sum of all rotation ops in the history. (Ops are
    // recorded individually because they are not pixel-wise mergeable.)
    double total = 0.0;
    for (const QString& n : m_xformByPath.value(m_currentPath))
        if (n.startsWith(QLatin1String("rot:"))) total += n.mid(4).toDouble();
    return std::remainder(total, 360.0);
}

// Absolute rotation from the stashed base. The base is the image state before
// the FIRST arbitrary rotation (captured lazily), so successive rotations are
// always one resample from the original data — never rotation-of-rotation.
void MainWindow::rotateToAngle(double totalDeg) {
    if (!m_image.isValid()) return;
    if (m_rotBasePath != m_currentPath || !m_rotBase.isValid()) {
        m_rotBase = m_image;
        m_rotBaseWcs = m_wcs;
        m_rotBaseHist = m_xformByPath.value(m_currentPath);
        m_rotBasePath = m_currentPath;
        m_rotBaseAngle = currentRotationAngle();
    }
    // Annotations are NOT restored from a stash — they may have been imported or
    // edited since the base capture. Instead, map the CURRENT set back to the
    // base frame with the exact inverse affine (vector data: no resampling), so
    // everything survives the round trip and rotates forward with the image.
    std::vector<Annotation> anns = m_annByPath.value(m_currentPath);
    const double relOld = currentRotationAngle() - m_rotBaseAngle;
    if (std::fabs(relOld) > 1e-6)
        rotateAnnotationsBy(anns, -relOld, m_image.width(), m_image.height(),
                            m_rotBase.width(), m_rotBase.height());
    // The base restore itself is a scene remap (inverse of the rotation that
    // produced the current state) — carry the link calibration through it too.
    if (std::fabs(relOld) > 1e-6)
        m_grid->remapActiveScene(rotForwardTransform(relOld, m_rotBase.width(), m_rotBase.height(),
                                                     m_image.width(), m_image.height()).inverted());
    restoreImageState(m_currentPath, m_rotBase, anns, m_rotBaseWcs, m_rotBaseHist);
    const double rel = totalDeg - m_rotBaseAngle;
    if (std::fabs(rel) > 1e-6) doRotateArbitrary(rel);
}

void MainWindow::pushRotateTo(double totalDeg) {
    const double cur = currentRotationAngle();
    if (std::fabs(totalDeg - cur) < 1e-4) return;
    rotateToAngle(totalDeg);
    m_undo->push(new RotateAngleCmd(this, m_currentPath, cur, totalDeg));  // first redo skipped
    normalizeOrientation();
}

void MainWindow::restoreImageState(const QString& path, const ImageData& img,
                                   const std::vector<Annotation>& anns,
                                   const Wcs& wcs, const QStringList& xformHist) {
    m_image = img;
    m_view->setSource(&m_image);
    updateDisplay();
    m_annByPath[path] = anns;
    m_annDirty.insert(path);
    m_wcs = wcs;
    m_xformByPath[path] = xformHist;
    refreshAnnotations();
    m_view->zoomToFit();
    m_lastW = m_image.width(); m_lastH = m_image.height();
}

// ---- user-configurable shortcuts -------------------------------------------

// Standard Qt pattern: a QSettings INI file holding "action = key sequence"
// pairs. On first run every default is written out, so the file is a complete,
// self-documenting template; edits override the defaults on the next start.
// Key strings use QKeySequence portable syntax: "Ctrl+Shift+F", "F11", "Tab",
// "Meta+Ctrl+F" (Meta = ⌘ on macOS). An empty value disables the shortcut.
void MainWindow::applyUserShortcuts(const QHash<QString, QAction*>& acts,
                                    const QHash<QString, QShortcut*>& keys) {
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QStringLiteral("NebulaScope"), QStringLiteral("shortcuts"));
    m_shortcutFile = s.fileName();
    s.beginGroup(QStringLiteral("shortcuts"));

    // Code-side defaults; entries missing from the INI are written out so the
    // file stays a complete, self-documenting template.
    //
    // Each entry also records the DEFAULT it was written with ("<name>.default")
    // so a later build can tell a user customisation from a stale default: if
    // the stored value still equals its recorded default, the user never
    // touched it, and a renamed code default may replace it (e.g. register
    // moved from Shift+R to M). A value that differs from its recorded default
    // is a deliberate customisation and is kept verbatim. Entries written by
    // builds before this scheme have no ".default" — treat the stored value as
    // the recorded default (the only thing that could have written it).
    QHash<QString, QString> defs, vals;
    for (auto it = acts.cbegin(); it != acts.cend(); ++it)
        defs[it.key()] = it.value()->shortcut().toString(QKeySequence::PortableText);
    for (auto it = keys.cbegin(); it != keys.cend(); ++it)
        defs[it.key()] = it.value()->key().toString(QKeySequence::PortableText);
    for (auto it = defs.cbegin(); it != defs.cend(); ++it) {
        const QString defKey = it.key() + QStringLiteral(".default");
        if (!s.contains(it.key())) {
            s.setValue(it.key(), it.value());
            s.setValue(defKey, it.value());
        } else {
            const QString stored = s.value(it.key()).toString();
            const QString recordedDefault = s.contains(defKey) ? s.value(defKey).toString() : stored;
            if (stored == recordedDefault && recordedDefault != it.value()) {
                // Untouched stale default: adopt the new code default.
                s.setValue(it.key(), it.value());
            }
            s.setValue(defKey, it.value());          // always track the current default
        }
        vals[it.key()] = s.value(it.key()).toString();
    }

    // Resolve clashes: a stale INI can still bind a key that a NEW default now
    // uses (e.g. Backspace was prev_image before delete_annotation existed).
    // Ambiguous Qt shortcuts fire nothing at all, so every entry involved in a
    // clash reverts to its code default — defaults are clash-free by design.
    QHash<QString, QStringList> bySeq;
    for (auto it = vals.cbegin(); it != vals.cend(); ++it) {
        const QString seq = QKeySequence::fromString(it.value(), QKeySequence::PortableText)
                                .toString(QKeySequence::PortableText);
        if (!seq.isEmpty()) bySeq[seq] << it.key();
    }
    for (auto it = bySeq.cbegin(); it != bySeq.cend(); ++it)
        if (it.value().size() > 1)
            for (const QString& name : it.value()) {
                vals[name] = defs[name];
                s.setValue(name, defs[name]);
            }

    for (auto it = acts.cbegin(); it != acts.cend(); ++it)
        it.value()->setShortcut(QKeySequence::fromString(vals[it.key()], QKeySequence::PortableText));
    for (auto it = keys.cbegin(); it != keys.cend(); ++it)
        it.value()->setKey(QKeySequence::fromString(vals[it.key()], QKeySequence::PortableText));
    s.endGroup();
}

void MainWindow::showShortcutSettings() {
    QMessageBox::information(this, tr("Configure Shortcuts"),
        tr("Shortcuts are read at startup from:<br><code>%1</code><br><br>"
                "Edit the <b>[shortcuts]</b> section using Qt key strings \u2014 e.g. "
                "<code>Ctrl+Shift+F</code>, <code>F11</code>, <code>Meta+Ctrl+F</code> "
                "(Meta = \u2318 on macOS). An empty value disables a shortcut. "
                "Restart NebulaScope to apply changes.").arg(m_shortcutFile));
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_shortcutFile).absolutePath()));
}

void MainWindow::showAbout() {
    QMessageBox box(this);
    box.setWindowTitle(tr("About NebulaScope"));
    box.setIconPixmap(windowIcon().pixmap(96, 96));
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<h2 style='margin:0'>NebulaScope</h2>"
        "<p style='color:#9fabb8;margin:2px 0 10px'>Astronomical image inspector — v%1</p>"
        "<p>Interactive FITS / XISF / JPEG / PNG / TIFF / WebP viewer with precise RGB"
        " histogram control, Generalised Hyperbolic Stretch, false-colour maps,"
        " and blink comparison.</p>"
        "<p style='color:#7e8b98;font-size:11px'>%2<br>Built with Qt, CFITSIO/CCfits and libXISF.</p>"
        "%3")
        // Version headline carries the exact build id (git describe) right
        // beside the official version, where a tester's eye actually goes.
        .arg(QString::fromUtf8(appinfo::kVersion) + QStringLiteral("  ·  ")
                 + QString::fromUtf8(appbuild::gitDescribe()),
             QString::fromUtf8(appinfo::kCopyright),
             QString::fromUtf8(appinfo::kAboutExtraHtml)));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

// ---- Copy / Paste Stretch ---------------------------------------------------

// Derive black/mid/white (as fractions of [min,max]) from a target image's own
// median+MAD using the copied robust anchors ((value-median)/MAD). This is what
// makes a "normalized" paste reproduce the same visual stretch on a frame with a
// completely different data range (exposure/filter/modality) without collapsing
// the signal into a few output levels.
static void applyAnchorsToStats(StretchModel::State& st, const std::vector<ChannelStats>& stats) {
    const int ns = int(stats.size());
    for (int c = 0; c < 3; ++c) {
        const int si = std::min(c, ns - 1);
        if (si < 0) continue;
        const double med = stats[si].median;
        const double lo = stats[si].min, hi = stats[si].max;
        const double range = std::max(1e-12, hi - lo);
        double mad = stats[si].mad;
        if (mad < 1e-12) mad = 0.01 * range;             // guard flat channels
        auto frac = [&](double anchor) {
            const double f = (med + anchor * mad - lo) / range;
            return f < 0 ? 0.0 : (f > 1 ? 1.0 : f);
        };
        double b = frac(st.aBlack[c]), m = frac(st.aMid[c]), w = frac(st.aWhite[c]);
        const double e = 0.0005;                          // keep black < mid < white
        if (m < b + e) m = std::min(1.0, b + e);
        if (w < m + e) w = std::min(1.0, m + e);
        if (b > w - 2 * e) b = std::max(0.0, w - 2 * e);
        st.chan[c].black = b; st.chan[c].mid = m; st.chan[c].white = w;
        st.lo[c] = lo; st.hi[c] = hi;
    }
}

void MainWindow::copyStretch() {
    if (m_currentPath.isEmpty() || !m_image.isValid()) return;
    m_copiedStretch = m_model.state();                   // absolute lo/hi + fractional points
    // Robust anchors: express each point as (value - median)/MAD per channel, so
    // a normalized paste can rebuild an equivalent window on any data range.
    m_copiedStretch.anchored = false;
    if (!m_curStats.empty()) {
        const int ns = int(m_curStats.size());
        for (int c = 0; c < 3; ++c) {
            const int si = std::min(c, ns - 1);
            const double med = m_curStats[si].median;
            const double lo = m_model.lo(c), hi = m_model.hi(c);
            const double range = std::max(1e-12, hi - lo);
            double mad = m_curStats[si].mad;
            if (mad < 1e-12) mad = 0.01 * range;
            const ChannelStretch cs = m_model.channel(c);
            auto anchor = [&](double f){ return (lo + f * range - med) / mad; };
            m_copiedStretch.aBlack[c] = anchor(cs.black);
            m_copiedStretch.aMid[c]   = anchor(cs.mid);
            m_copiedStretch.aWhite[c] = anchor(cs.white);
        }
        m_copiedStretch.anchored = true;
    }
    statusBar()->showMessage(tr("Copied stretch — right-click a list entry to paste"), 2500);
}

// Apply the copied stretch to one file. Normalized rebuilds the window from the
// target's robust stats (median+MAD anchors); absolute carries the source's exact
// data-unit window.
void MainWindow::applyCopiedStretch(const QString& path, bool normalized) {
    if (!m_copiedStretch.valid || path.isEmpty()) return;
    StretchModel::State s = m_copiedStretch;
    s.valid = true;

    if (path == m_currentPath && m_image.isValid()) {
        // Target is decoded and on screen — apply live.
        if (normalized) {
            if (s.anchored && !m_curStats.empty()) applyAnchorsToStats(s, m_curStats);
            else for (int c = 0; c < 3; ++c) { s.lo[c] = m_model.lo(c); s.hi[c] = m_model.hi(c); }
        }
        if (s.count == 1)
            for (int c = 1; c < 3; ++c) { s.chan[c] = s.chan[0]; s.lo[c] = s.lo[0]; s.hi[c] = s.hi[0]; }
        s.count = m_image.channels();
        s.renormalize = false;
        m_model.setState(s);                             // changed handler persists it
    } else {
        // Not decoded yet — stash; displayPath() finalizes on next visit.
        s.renormalize = normalized;                      // defer window to target's range
        m_stfByPath.insert(path, s);
    }
}

void MainWindow::pasteStretchToSelected(bool normalized) {
    if (!m_copiedStretch.valid) { statusBar()->showMessage(tr("No stretch copied yet"), 2000); return; }
    auto sel = m_fileList->selectedItems();
    if (sel.isEmpty() && m_fileList->currentItem()) sel << m_fileList->currentItem();
    int n = 0;
    for (QListWidgetItem* it : sel) { applyCopiedStretch(it->data(Qt::UserRole).toString(), normalized); ++n; }
    statusBar()->showMessage(tr("Pasted %1 stretch to %2 image(s)")
        .arg(normalized ? tr("normalized") : tr("absolute")).arg(n), 3000);
}

void MainWindow::pasteStretchToAll(bool normalized) {
    if (!m_copiedStretch.valid) { statusBar()->showMessage(tr("No stretch copied yet"), 2000); return; }
    for (int i = 0; i < m_fileList->count(); ++i)
        applyCopiedStretch(m_fileList->item(i)->data(Qt::UserRole).toString(), normalized);
    statusBar()->showMessage(tr("Pasted %1 stretch to all %2 image(s)")
        .arg(normalized ? tr("normalized") : tr("absolute")).arg(m_fileList->count()), 3000);
}

void MainWindow::onListContextMenu(const QPoint& pos) {
    QListWidgetItem* clicked = m_fileList->itemAt(pos);
    // If the right-clicked row isn't part of the current selection, target just
    // it — but don't switch the displayed image (no setCurrentItem).
    if (clicked && !clicked->isSelected()) {
        m_fileList->clearSelection();
        clicked->setSelected(true);
    }
    const int nSel = m_fileList->selectedItems().size();

    QMenu menu(this);
    QAction* aCopy = menu.addAction(tr("Copy Stretch"));
    aCopy->setEnabled(!m_currentPath.isEmpty() && m_image.isValid());
    menu.addSeparator();
    QAction* aPasteN = menu.addAction(tr("Paste Stretch — Normalized (%1)").arg(nSel));
    QAction* aPasteA = menu.addAction(tr("Paste Stretch — Absolute (%1)").arg(nSel));
    QAction* aAllN = menu.addAction(tr("Paste Stretch to All — Normalized"));
    const bool canPaste = m_copiedStretch.valid && nSel > 0;
    aPasteN->setEnabled(canPaste);
    aPasteA->setEnabled(canPaste);
    aAllN->setEnabled(m_copiedStretch.valid && m_fileList->count() > 0);
    menu.addSeparator();
    // Blink culling (checked = keep): tag the selection, then act on tags.
    QAction* aCheckSel   = menu.addAction(tr("Check Selected (%1)").arg(nSel));
    QAction* aUncheckSel = menu.addAction(tr("Uncheck Selected (%1)").arg(nSel));
    aCheckSel->setEnabled(nSel > 0);
    aUncheckSel->setEnabled(nSel > 0);
    QAction* aSortTag  = menu.addAction(tr("Sort: Checked First"));
    QAction* aMoveUnch = menu.addAction(tr("Move Unchecked To…"));
    QAction* aMoveChk  = menu.addAction(tr("Move Checked To…"));
    QAction* aRemUnch  = menu.addAction(tr("Remove Unchecked from List"));
    QAction* aRemChk   = menu.addAction(tr("Remove Checked from List"));
    menu.addSeparator();
    QAction* aRemove = menu.addAction(tr("Close && Remove from List"));
    aRemove->setEnabled(nSel > 0);
    QAction* aClear = menu.addAction(tr("Clear List && Close All"));
    aClear->setEnabled(m_fileList->count() > 0);

    QAction* chosen = menu.exec(m_fileList->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == aCopy) copyStretch();
    else if (chosen == aPasteN) pasteStretchToSelected(true);
    else if (chosen == aPasteA) pasteStretchToSelected(false);
    else if (chosen == aAllN) pasteStretchToAll(true);
    else if (chosen == aCheckSel)   setSelectedTags(true);
    else if (chosen == aUncheckSel) setSelectedTags(false);
    else if (chosen == aSortTag)  sortListByTag();
    else if (chosen == aMoveUnch) moveTaggedFiles(false);
    else if (chosen == aMoveChk)  moveTaggedFiles(true);
    else if (chosen == aRemUnch)  removeTaggedFromList(false);
    else if (chosen == aRemChk)   removeTaggedFromList(true);
    else if (chosen == aRemove) removeSelected();
    else if (chosen == aClear) clearImageList();
}

// One-time wiring for every view the grid creates. Handlers act on the ACTIVE
// cell's state (m_view/m_annotations/m_currentPath); a press inside any cell
// activates it BEFORE these signals fire (ViewCell's event filter), so by the
// time a press-derived signal arrives, sender == m_view. Hover is the one
// signal that arrives without a press — gate it to the active view.
void MainWindow::connectViewSignals(ImageView* v) {
    connect(v, &ImageView::pixelHovered, this,
            [this, v](int x, int y, double r, double g, double b, bool valid) {
        if (v == m_view) onPixelHovered(x, y, r, g, b, valid);
    });
    connect(v, &ImageView::contextMenuRequested, this, &MainWindow::onImageContextMenu);
    // A list row dropped on this view: activate its cell, then show the row
    // there (same path as click-to-activate + click-the-row).
    connect(v, &ImageView::listKeyDropped, this, [this, v](const QString& key) {
        for (int i = 0; ViewCell* c = m_grid->cellAt(i); ++i)
            if (c->view() == v) { m_grid->activate(c); break; }
        for (int r = 0; r < m_fileList->count(); ++r) {
            QListWidgetItem* it = m_fileList->item(r);
            if (it->data(Qt::UserRole).toString() == key) {
                if (m_fileList->currentItem() == it) displayPath(key);
                else m_fileList->setCurrentItem(it, QItemSelectionModel::ClearAndSelect);
                break;
            }
        }
    });
    connect(v, &ImageView::ellipseDrawn, this, &MainWindow::onEllipseDrawn);
    connect(v, &ImageView::skyPatchPicked, this, &MainWindow::onSkyPatchPicked);
    connect(v, &ImageView::lineDrawn, this, &MainWindow::onLineDrawn);
    connect(v, &ImageView::textPointPicked, this, &MainWindow::onTextPointPicked);
    connect(v, &ImageView::registerPointPicked, this,
            [this, v](double x, double y) { onRegisterPointPicked(v, x, y); });
    connect(v, &ImageView::annotationPressed, this, [this](const QPointF& sp, bool isHandle) {
        if (isHandle) return;                       // dragging a handle — keep the set
        m_annotations->setActive(m_annotations->hitTest(sp));
    });
    connect(v, &ImageView::annotationDoubleClicked, this, [this](const QPointF& sp) {
        editAnnotationDialog(m_annotations->hitTest(sp));
    });
    connect(v, &ImageView::annotationDragged, this, [this] {
        m_annotations->syncHandles();               // handles track a live move
    });
    connect(v, &ImageView::annotationsEdited, this, [this] {
        std::vector<Annotation> before = m_annByPath.value(m_currentPath);
        if (m_annotations->commitMoves(m_annByPath[m_currentPath])) {
            m_annDirty.insert(m_currentPath);
            refreshAnnotations();
            pushAnnotationEdit(tr("move/resize annotation"), m_currentPath, std::move(before));
        }
    });
    connect(v, &ImageView::drawToolFinished, this, [this] {
        for (QAction* a : { m_toolEllipse, m_toolLine, m_toolText })
            if (a) a->setChecked(false);
    });
}

// Move the current-image state into the deactivating cell and adopt the newly
// activated cell's. All per-path machinery (stretch memory, annotations,
// orientation history, undo) is keyed by m_currentPath, so it follows along.
// RAII marker for PROGRAMMATIC stretch changes (image switches, undo/redo,
// transport fits): flushes any pending user gesture first, suppresses
// recording inside the scope, and resyncs the undo baseline on exit.
struct StretchSquelch {
    MainWindow* w;
    explicit StretchSquelch(MainWindow* win) : w(win) {
        w->flushStretchUndo();
        ++w->m_squelchStretch;
    }
    ~StretchSquelch() {
        if (--w->m_squelchStretch == 0) w->resyncStretchUndoBase();
    }
};

void MainWindow::flushStretchUndo() {
    if (!m_stretchUndoTimer || !m_stretchUndoTimer->isActive()) return;
    m_stretchUndoTimer->stop();
    pushStretchUndo();
}

void MainWindow::resyncStretchUndoBase() {
    m_undoBase = m_model.state();
    m_undoBasePath = m_currentPath;
}

void MainWindow::onCellSwap(ViewCell* oldC, ViewCell* newC) {
    if (!newC || oldC == newC) return;
    StretchSquelch sq(this);               // cell adoption is not a stretch edit
    if (oldC) {
        oldC->image = std::move(m_image);
        m_image = ImageData();
        oldC->header = m_header;
        oldC->path = m_currentPath;
        oldC->wcs = m_wcs;
        oldC->stats = m_curStats;
        oldC->stretch = m_model.state();
        oldC->hasStretch = oldC->image.isValid();
        if (!oldC->path.isEmpty()) m_stfByPath[oldC->path] = oldC->stretch;
        oldC->xformRev = m_xformRev.value(oldC->path, 0);
        oldC->view()->setSource(&oldC->image);      // pixel readout keeps working
    }
    m_image = std::move(newC->image);
    newC->image = ImageData();
    m_header = newC->header;
    updateIccTransform();
    m_currentPath = newC->path;
    m_wcs = newC->wcs;
    m_curStats = newC->stats;
    m_view = newC->view();
    m_annotations = newC->layer();
    m_view->setSource(&m_image);
    m_hist->setSource(&m_image);
    m_info->setData(&m_image, &m_header, m_curStats);
    // Keep the file list's highlight on the active cell's image (block the
    // currentRowChanged → showRow round trip; the image is already decoded).
    if (m_fileList) {
        QSignalBlocker blk(m_fileList);
        for (int i = 0; i < m_fileList->count(); ++i)
            if (m_fileList->item(i)->data(Qt::UserRole).toString() == m_currentPath)
                m_fileList->setCurrentRow(i, QItemSelectionModel::ClearAndSelect);
    }
    if (m_image.isValid()) {
        m_model.setChannelCount(m_image.channels());
        m_lastW = m_image.width();
        m_lastH = m_image.height();
    }
    if (newC->hasStretch) m_model.setState(newC->stretch);   // changed() re-renders this view
    // Stale-view guard: the image's orientation changed (in another cell or via
    // a tool) after this stash was made — the pixels no longer match the
    // recorded history. Re-derive them from source + history.
    if (!m_currentPath.isEmpty() &&
        newC->xformRev != m_xformRev.value(m_currentPath, 0)) {
        newC->xformRev = m_xformRev.value(m_currentPath, 0);
        displayPath(m_currentPath);                 // fresh decode + replay + stretch restore
        return;                                     // displayPath refreshed annotations/panels
    }
    if (m_cmapCombo) {
        const bool mono = m_image.isValid() && m_image.channels() == 1;
        m_cmapCombo->setEnabled(mono);
        QSignalBlocker blk(m_cmapCombo);
        m_cmapCombo->setCurrentIndex(int(mono ? m_model.colormap() : Colormap::Gray));
        if (m_invertCheck) m_invertCheck->setEnabled(mono);
    }
    refreshAnnotations();
}

void MainWindow::buildMenusAndToolbar() {
    QHash<QString, QAction*> acts;      // registry for user-configurable shortcuts
    QHash<QString, QShortcut*> keys;

    // File
    QMenu* file = menuBar()->addMenu(tr("&File"));
    acts["open"] = file->addAction(tr("&Open…"), QKeySequence::Open, this, &MainWindow::openFile);
    m_recentImagesMenu = file->addMenu(tr("Open &Recent"));
    m_recentJsonMenu = file->addMenu(tr("Recent A&nnotations"));
    rebuildRecentMenus();
    acts["save_data_as"] = file->addAction(tr("&Save Data As…"), QKeySequence::SaveAs, this, &MainWindow::saveFile);
    acts["save_stretched_as"] = file->addAction(tr("Save Stretc&hed As…"), this, &MainWindow::saveStretched);
    acts["export_view"] = file->addAction(tr("&Export View As…"), QKeySequence("Ctrl+E"), this, &MainWindow::exportView);
    acts["export_region"] = file->addAction(tr("Export &Zoomed Region As…"), QKeySequence("Ctrl+Shift+E"), this, &MainWindow::exportRegion);
    file->addSeparator();
    // Sidecar save/load reachable from the menu too (they were context-menu
    // only): the sidecar carries the display state, so "keep this look" is a
    // File operation as much as an annotation one.
    acts["save_annotations"] = file->addAction(tr("Save &Annotations && Display"),
                                               this, &MainWindow::saveAnnotations);
    acts["save_annotations_as"] = file->addAction(tr("Save Annotations && Display As…"),
                                                  this, &MainWindow::saveAnnotationsAs);
    file->addSeparator();
    acts["export_list"] = file->addAction(tr("Export Image &List…"), this, &MainWindow::exportList);
    acts["import_list"] = file->addAction(tr("&Import Image List…"), this, &MainWindow::importList);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    // Edit — undo/redo for annotation edits and image transforms.
    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction* aUndo = m_undo->createUndoAction(this, tr("&Undo"));
    aUndo->setShortcut(QKeySequence::Undo);
    QAction* aRedo = m_undo->createRedoAction(this, tr("&Redo"));
    aRedo->setShortcut(QKeySequence::Redo);
    editMenu->addAction(aUndo);
    editMenu->addAction(aRedo);
    acts["undo"] = aUndo;               // registry: remappable + scriptable
    acts["redo"] = aRedo;

    // View
    QMenu* view = menuBar()->addMenu(tr("&View"));
    QAction* aLeft = m_leftDock->toggleViewAction();
    aLeft->setShortcuts({ QKeySequence("F2"), QKeySequence("Shift+L") });
    QAction* aRight = m_rightDock->toggleViewAction();
    aRight->setShortcut(QKeySequence("F3"));
    QAction* aInfo = m_infoDock->toggleViewAction();
    aInfo->setShortcuts({ QKeySequence("F4"), QKeySequence("P") });
    view->addAction(aLeft);
    view->addAction(aInfo);
    view->addAction(aRight);
    acts["toggle_image_list"] = aLeft;
    acts["toggle_info_panel"] = aInfo;
    acts["toggle_histogram"]  = aRight;
    acts["close_image"] = view->addAction(tr("&Close Selected Images"), QKeySequence("C"), this, [this] {
        // Close every highlighted row (fallback: the current one), reusing
        // removeSelected()'s cleanup (decoded data, stretch memory,
        // annotations, HDU children, next-row pick).
        if (m_fileList->selectedItems().isEmpty()) {
            QListWidgetItem* it = m_fileList->currentItem();
            if (!it) return;
            it->setSelected(true);
        }
        removeSelected();
    });
    view->addSeparator();
    // Receiver must be resolved at INVOCATION time: binding m_view directly
    // would freeze the receiver to whichever cell was active when the menu was
    // built — in split views, F/1 would then always act on cell 0.
    acts["zoom_to_fit"] = view->addAction(tr("Zoom to &Fit"), QKeySequence("F"),
                                          this, [this] { m_view->zoomToFit(); });
    acts["zoom_to_width"] = view->addAction(tr("Zoom to &Width"), QKeySequence("W"),
                                            this, [this] { m_view->zoomToWidth(); });
    acts["zoom_actual_size"] = view->addAction(tr("Zoom &1:1"), QKeySequence("1"),
                                               this, [this] { m_view->zoomActualSize(); });
    // Keyboard zoom (no wheel needed): > / < coarse, . / , fine — step
    // percentages configurable in Preferences. Lambdas read m_view live so
    // they always target the ACTIVE cell.
    auto zoomStep = [this](bool fine, bool in) {
        const double pct = fine ? double(Preferences::get().zoomStepFine)
                                : double(Preferences::get().zoomStepCoarse);
        const double f = 1.0 + pct / 100.0;
        if (m_view && m_image.isValid()) m_view->zoomBy(in ? f : 1.0 / f);
    };
    acts["zoom_in"]       = view->addAction(tr("Zoom In"),  QKeySequence(">"), this, [zoomStep] { zoomStep(false, true);  });
    acts["zoom_out"]      = view->addAction(tr("Zoom Out"), QKeySequence("<"), this, [zoomStep] { zoomStep(false, false); });
    acts["zoom_in_fine"]  = view->addAction(tr("Zoom In (Fine)"),  QKeySequence("."), this, [zoomStep] { zoomStep(true, true);  });
    acts["zoom_out_fine"] = view->addAction(tr("Zoom Out (Fine)"), QKeySequence(","), this, [zoomStep] { zoomStep(true, false); });
    // QKeySequence::FullScreen is the platform-correct binding (⌃⌘F on macOS —
    // F11 there is taken by the system — and F11 on Windows/Linux).
    acts["fullscreen"] = view->addAction(tr("&Fullscreen"), QKeySequence::FullScreen, this, [this] {
        isFullScreen() ? showNormal() : showFullScreen();
    });
#ifdef Q_OS_MACOS
    // The green button enters NATIVE full screen (own Space, menu bar and
    // title hidden) — ⌥F must match it exactly, not plain maximize.
    acts["maximize"] = view->addAction(tr("Full Screen (Green Button)"), QKeySequence("Alt+F"),
                                       this, [this] {
        isFullScreen() ? showNormal() : showFullScreen();
    });
#else
    acts["maximize"] = view->addAction(tr("&Maximize"), QKeySequence("Alt+F"), this, [this] {
        isMaximized() ? showNormal() : showMaximized();
    });
#endif
    acts["image_only"] = view->addAction(tr("&Image Only"), QKeySequence("Tab"), this, &MainWindow::toggleImageOnly);
    acts["clear_list"] = view->addAction(tr("Clear List && Close All"), QKeySequence("Alt+C"),
                                         this, &MainWindow::clearImageList);
    // Interop: refresh images that PixInsight/Siril/GraXpert overwrite on disk.
    acts["reload_original"] = view->addAction(tr("Reload Origi&nal"), QKeySequence("Ctrl+Shift+R"),
                                              this, &MainWindow::reloadOriginal);
    m_autoReloadAct = view->addAction(tr("Auto-&Reload Changed Files"));
    m_autoReloadAct->setCheckable(true);
    m_autoReloadAct->setChecked(true);
    m_autoReloadAct->setToolTip(tr("Re-decode a list image when another program overwrites it"));
    acts["auto_reload"] = m_autoReloadAct;
    view->addSeparator();
    QAction* aGrid = view->addAction(tr("Coordinate &Grid"), QKeySequence("Shift+G"), this, [this](bool) {
        m_annotations->setGridVisible(!m_annotations->gridVisible());
        refreshAnnotations();
    });
    aGrid->setCheckable(true);
    acts["toggle_grid"] = aGrid;
    QAction* aAnnVis = view->addAction(tr("Show &Annotations"), QKeySequence("A"), this, [this] {
        m_annotations->setAnnotationsVisible(!m_annotations->annotationsVisible());
        refreshAnnotations();
    });
    aAnnVis->setCheckable(true);
    aAnnVis->setChecked(true);
    m_annVisAct = aAnnVis;
    acts["toggle_annotations"] = aAnnVis;
    // Values Everywhere: while comparing split views, show the coordinates
    // and values under the pointer in ALL cells (each from its own data at
    // the corresponding pixel), independent of the histogram panel's line.
    QAction* aVals = view->addAction(tr("&Values in All Views"), QKeySequence("V"), this, [this] {
        m_valuesEverywhere = !m_valuesEverywhere;
        if (!m_valuesEverywhere) clearReadouts();
        else if (m_hoverValid) updateReadouts(m_hoverX, m_hoverY, true);
        statusBar()->showMessage(m_valuesEverywhere
            ? tr("Values in all views: on — hover to read every cell at the pointer")
            : tr("Values in all views: off"), 3000);
    });
    aVals->setCheckable(true);
    acts["values_everywhere"] = aVals;
    // "Match": M / Shift+M. (R and its modifier variants belong to the
    // histogram workflow — Reset Stretch, rotate, reload — and a Shift+R
    // next to a key hammered all day invites mis-keys both ways.)
    acts["register_views"] = view->addAction(tr("&Match Views: Pick a Feature"),
                                             QKeySequence("M"), this, [this] { startRegister(false); });
    acts["register_views_2"] = view->addAction(tr("Match: Add Second Pair (scale+rotation)"),
                                               QKeySequence("Shift+M"), this, [this] { startRegister(true); });
    // Hide the scrollbars ("elevators") for a clean canvas — pans still work
    // (right-drag / Shift-drag / middle-drag). Applies to every split cell.
    QAction* aScroll = view->addAction(tr("Hide Scroll&bars"), QKeySequence("H"), this, [this] {
        m_grid->setScrollBarsVisible(!m_grid->scrollBarsVisible());
    });
    aScroll->setCheckable(true);
    aScroll->setChecked(false);
    acts["toggle_scrollbars"] = aScroll;
    QAction* aOverlay = view->addAction(tr("&Overlay Panels"), QKeySequence("O"), this, [this] {
        setOverlayPanels(!m_overlay);
    });
    aOverlay->setCheckable(true);
    aOverlay->setChecked(true);
    aOverlay->setShortcutContext(Qt::ApplicationShortcut);
    acts["overlay_panels"] = aOverlay;
    // Overlay is the default layout; docked panels remain one 'O' away.
    QTimer::singleShot(0, this, [this, aOverlay] {
        setOverlayPanels(true);
        aOverlay->setChecked(true);
    });
    // In overlay mode the L/P/F3 dock toggles are intercepted: the dock briefly
    // becomes visible, we re-hide it and flip the matching overlay box instead.
    auto hookDock = [this](QDockWidget* d, QWidget* MainWindow::* box) {
        connect(d, &QDockWidget::visibilityChanged, this, [this, d, box](bool vis) {
            if (!m_overlay || !vis) return;
            QTimer::singleShot(0, this, [this, d, box] {
                if (!m_overlay) return;
                d->hide();
                if (QWidget* b = this->*box) { b->setVisible(!b->isVisible()); layoutOverlayPanels(); }
            });
        });
    };
    hookDock(m_leftDock,  &MainWindow::m_ovList);
    hookDock(m_infoDock,  &MainWindow::m_ovInfo);
    hookDock(m_rightDock, &MainWindow::m_ovHist);

    // Split main view — compare several decoded images side by side. Same-size
    // images pan/zoom together (each cell's ⇄ button opts out).
    QMenu* split = view->addMenu(tr("Split &View"));
    auto addPreset = [&](const QString& label, int r, int c) {
        split->addAction(label, this, [this, r, c] { m_grid->setGrid(r, c); });
    };
    addPreset(tr("Single"), 1, 1);
    addPreset(tr("1 \u00d7 2 (side by side)"), 1, 2);
    addPreset(tr("2 \u00d7 1 (stacked)"), 2, 1);
    addPreset(tr("2 \u00d7 2"), 2, 2);
    split->addSeparator();
    split->addAction(tr("Custom\u2026"), this, [this] {
        // One dialog, two spinboxes (little up/down arrows), 1-5 each.
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Split view"));
        auto* form = new QFormLayout(&dlg);
        auto* rowsSpin = new QSpinBox(); rowsSpin->setRange(1, 5); rowsSpin->setValue(m_grid->rows());
        auto* colsSpin = new QSpinBox(); colsSpin->setRange(1, 5); colsSpin->setValue(m_grid->cols());
        form->addRow(tr("Rows:"), rowsSpin);
        form->addRow(tr("Columns:"), colsSpin);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(bb);
        if (dlg.exec() == QDialog::Accepted)
            m_grid->setGrid(rowsSpin->value(), colsSpin->value());
    });

    auto* esc = new QShortcut(QKeySequence("Esc"), this);
    connect(esc, &QShortcut::activated, this, [this] {
        if (m_regArmed) { cancelRegister(); return; }    // Esc: abandon a pick in progress
        if (m_imageOnly) toggleImageOnly();
    });

    // Image — lossless 90° rotations and flips (applied to the pixel data).
    QMenu* image = menuBar()->addMenu(tr("&Image"));
    acts["rotate_cw"]  = image->addAction(tr("Rotate 90\u00b0 CW"),  QKeySequence("]"),       this, [this]{ applyTransform(Xform::RotCW); });
    acts["rotate_ccw"] = image->addAction(tr("Rotate 90\u00b0 CCW"), QKeySequence("["),       this, [this]{ applyTransform(Xform::RotCCW); });
    acts["rotate_by_angle"] = image->addAction(tr("Rotate by &Angle\u2026"), QKeySequence("Ctrl+R"), this, [this]{
        RotateDialog* dlg = makeRotateDialog();
        if (!dlg) return;
        if (dlg->exec() == QDialog::Accepted) pushRotateTo(dlg->angle());
        dlg->deleteLater();
    });
    image->addSeparator();
    acts["flip_horizontal"] = image->addAction(tr("Flip &Horizontal"), QKeySequence("Ctrl+H"), this, [this]{ applyTransform(Xform::FlipH); });
    acts["flip_vertical"]   = image->addAction(tr("Flip &Vertical"),   QKeySequence("Ctrl+J"), this, [this]{ applyTransform(Xform::FlipV); });

    image->addSeparator();
    acts["crop_visible"] = image->addAction(tr("&Crop to Visible Region"), QKeySequence("Shift+C"), this, [this] {
        // Inner rect: with a rotated navigation the visible region is a quad;
        // an axis-aligned data crop must stay INSIDE it, not take its bbox.
        if (m_image.isValid()) cropCurrentToRect(m_view->visibleInnerRect());
    });

    // Debayer: per-image pattern mode + the global algorithm.
    image->addSeparator();
    QMenu* deb = image->addMenu(tr("De&bayer"));
    auto* modeGroup = new QActionGroup(this);
    const struct { const char* label; int mode; const char* key; } kModes[] = {
        { QT_TR_NOOP("&Auto-Detect (header)"), 0,  "debayer_auto" },
        { QT_TR_NOOP("Force RGGB"),            1,  "debayer_rggb" },
        { QT_TR_NOOP("Force BGGR"),            2,  "debayer_bggr" },
        { QT_TR_NOOP("Force GRBG"),            3,  "debayer_grbg" },
        { QT_TR_NOOP("Force GBRG"),            4,  "debayer_gbrg" },
        { QT_TR_NOOP("&Off (raw mosaic)"),     -1, "debayer_off" },
    };
    for (int i = 0; i < 6; ++i) {
        const int mode = kModes[i].mode;
        QAction* a = deb->addAction(tr(kModes[i].label), this, [this, mode] {
            requestDebayerChange(mode, kKeepDebayer);      // undoable
        });
        a->setCheckable(true);
        a->setActionGroup(modeGroup);
        m_debayerModeActs[i] = a;
        acts[kModes[i].key] = a;
    }
    m_debayerModeActs[0]->setChecked(true);
    deb->addSeparator();
    auto* methodGroup = new QActionGroup(this);
    const struct { const char* label; int m; const char* key; } kMeth[] = {
        { QT_TR_NOOP("Superpixel (half size)"), 0, "debayer_superpixel" },
        { QT_TR_NOOP("Bilinear"),               1, "debayer_bilinear" },
        { QT_TR_NOOP("RCD (best)"),             2, "debayer_rcd" },
    };
    for (int i = 0; i < 3; ++i) {
        const int m = kMeth[i].m;
        QAction* a = deb->addAction(tr(kMeth[i].label), this, [this, m] {
            requestDebayerChange(kKeepDebayer, m);         // undoable
        });
        a->setCheckable(true);
        a->setActionGroup(methodGroup);
        m_debayerMethodActs[i] = a;
        acts[kMeth[i].key] = a;
    }
    m_debayerMethodActs[qBound(0, Preferences::get().debayerMethod, 2)]->setChecked(true);
    deb->addSeparator();
    // One capture stream = one sensor: force the pattern once, stamp it on
    // every listed frame (same idiom as the histogram's Apply to All).
    acts["debayer_apply_all"] = deb->addAction(tr("Apply Choice to All &in List"),
                                               this, &MainWindow::applyDebayerToAll);
    image->addSeparator();
    acts["reset_orientation"] = image->addAction(tr("Reset &Orientation"), this, &MainWindow::resetOrientation);
    image->addSeparator();
    acts["apply_saved_orientation"] = image->addAction(tr("Apply &Saved Orientation"), this, &MainWindow::applySavedOrientation);

    // Stretch — transfer the current image's stretch to others in the list.
    QMenu* stretch = menuBar()->addMenu(tr("&Stretch"));
    // Transfer-function radio group: I / L / S / G (the list and grid view
    // toggles moved to Shift+L / Shift+G to free the mnemonics).
    auto* fnGroup = new QActionGroup(this);
    const struct { const char* label; const char* key; const char* reg; StretchFn fn; } kFns[] = {
        { QT_TR_NOOP("L&inear"), "I", "fn_linear", StretchFn::Linear },
        { QT_TR_NOOP("&Log"),    "L", "fn_log",    StretchFn::Log },
        { QT_TR_NOOP("A&sinh"),  "S", "fn_asinh",  StretchFn::Asinh },
        { QT_TR_NOOP("&GHS"),    "G", "fn_ghs",    StretchFn::GHS },
    };
    QAction* fnActs[4];
    for (int i = 0; i < 4; ++i) {
        const StretchFn fn = kFns[i].fn;
        QAction* a = stretch->addAction(tr(kFns[i].label), QKeySequence(kFns[i].key),
                                        this, [this, fn] { m_model.setFn(fn); });
        a->setCheckable(true);
        a->setActionGroup(fnGroup);
        fnActs[i] = a;
        acts[kFns[i].reg] = a;
    }
    fnActs[0]->setChecked(true);
    connect(&m_model, &StretchModel::changed, this, [this, fnActs] {
        const int i = int(m_model.fn());
        for (int k = 0; k < 4; ++k) {
            QSignalBlocker b(fnActs[k]);
            fnActs[k]->setChecked(k == i);
        }
    });
    stretch->addSeparator();
    acts["auto_stf"] = stretch->addAction(tr("Auto ST&F"), QKeySequence("U"), this, [this] {
        if (!m_curStats.empty()) m_model.autoStretch(m_curStats);
    });
    acts["auto_stf_linked"] = stretch->addAction(tr("Auto STF (&Linked)"), QKeySequence("Shift+U"), this, [this] {
        if (!m_curStats.empty()) m_model.autoStretchLinked(m_curStats);
    });
    acts["reset_stretch"] = stretch->addAction(tr("&Reset Stretch"), QKeySequence("R"), this, [this] {
        m_model.reset();                                   // fn, GHS, adjustments
        if (!m_curStats.empty()) m_model.linearWindow(m_curStats);   // the first-view ramps
    });
    acts["apply_stf_all"] = stretch->addAction(tr("Apply Stretch to All"), QKeySequence("Shift+A"),
                                               this, &MainWindow::applyStretchToAllList);
    // Neutral black from a hand-picked sky patch: with per-filter pedestals
    // the ESTIMATE is the risky part (a mode can land on nebulosity), so the
    // user points at truly empty sky and the patch median becomes each
    // channel's black point — three simultaneous B-drags, M carried as a
    // ratio, W untouched. Windows carry into every mode, GHS included.
    acts["black_from_patch"] = stretch->addAction(tr("Neutralize &Background from Sky Patch"), this, [this] {
        if (!m_image.isValid()) return;
        m_view->setDrawTool(ImageView::DrawTool::SkyPatch);
        statusBar()->showMessage(tr("Drag a small rectangle over empty sky — the background becomes a neutral grey at its current brightness"), 8000);
    });
    stretch->addSeparator();
    acts["copy_stretch"] = stretch->addAction(tr("&Copy Stretch"), QKeySequence("Ctrl+Alt+C"), this, &MainWindow::copyStretch);
    acts["paste_stretch_normalized"] = stretch->addAction(tr("&Paste Stretch (Normalized)"), QKeySequence("Ctrl+Alt+V"), this, [this]{ pasteStretchToSelected(true); });
    acts["paste_stretch_absolute"] = stretch->addAction(tr("Paste Stretch (&Absolute)"), QKeySequence("Ctrl+Alt+Shift+V"), this, [this]{ pasteStretchToSelected(false); });
    stretch->addSeparator();
    acts["paste_stretch_all"] = stretch->addAction(tr("Paste Stretch to &All"), this, [this]{ pasteStretchToAll(true); });

    // Tools — pixel-math utilities.
    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    acts["combine_channels"] = tools->addAction(tr("&Combine Channels…"), this, &MainWindow::combineChannels);
    acts["combine_stars"] = tools->addAction(tr("Combine &Stars (screen)…"), this, &MainWindow::combineStars);
    acts["transport_colors"] = tools->addAction(tr("&Transport Colors from Reference…"), this, &MainWindow::transportColorsFromRef);
    acts["measure_psf"] = tools->addAction(tr("&Measure PSF (Stars)…"), this, &MainWindow::measurePsfAction);
    acts["deconvolve"] = tools->addAction(tr("&Deconvolve to Target PSF…"), this, &MainWindow::deconvolveAction);
    acts["import_sextractor"] = tools->addAction(tr("Import &SExtractor Catalog…"), this, &MainWindow::importSexCatalog);

    // Help — the About action carries AboutRole, so on macOS Qt moves it into
    // the application menu (“NebulaScope ▸ About NebulaScope”) automatically.
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Configure &Shortcuts…"), this, &MainWindow::showShortcutSettings);
    QAction* prefsAct = help->addAction(tr("&Preferences…"), this, [this] {
        const int oldDebayer = Preferences::get().debayerMethod;
        PreferencesDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            m_annColor = Preferences::get().annColor;   // new-annotation default
            refreshAnnotations();                       // grid density, stroke width
            m_imgCache.setBudgetBytes(qint64(Preferences::get().imageCacheMB) * 1024 * 1024);
            const int newDebayer = Preferences::get().debayerMethod;
            if (newDebayer != oldDebayer) {
                // Route through the undo stack: rewind the pref the dialog
                // already wrote, then apply as one undoable change.
                Preferences::get().debayerMethod = oldDebayer;
                requestDebayerChange(kKeepDebayer, newDebayer);
                syncDebayerMenu();                      // covers the no-image case
            }
        }
    });
    prefsAct->setMenuRole(QAction::PreferencesRole);   // macOS: app menu ▸ Settings…
    QAction* about = help->addAction(tr("&About NebulaScope"), this, &MainWindow::showAbout);
    about->setMenuRole(QAction::AboutRole);
    QAction* aboutQt = help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
    aboutQt->setMenuRole(QAction::AboutQtRole);

    // Walk the loaded-image list: Space = next, Shift+Space = previous.
    auto* next = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(next, &QShortcut::activated, this, &MainWindow::nextImage);
    auto* prev = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Space), this);
    connect(prev, &QShortcut::activated, this, &MainWindow::prevImage);
    keys["next_image"] = next;
    keys["prev_image"] = prev;
    // Blink culling: B flips the shown frame's keep-tag mid-blink.
    auto* tag = new QShortcut(QKeySequence(Qt::Key_B), this);
    connect(tag, &QShortcut::activated, this, &MainWindow::toggleCurrentTag);
    keys["toggle_tag"] = tag;
    // Delete (Backspace on macOS) removes the selected annotation — or, with
    // nothing selected, the most recently added one. Undoable.
    auto* delAnn = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    connect(delAnn, &QShortcut::activated, this, &MainWindow::deleteActiveAnnotation);
    // If some other binding still collides, Qt reports the press as "ambiguous"
    // instead of activating — treat that as a plain activation.
    connect(delAnn, &QShortcut::activatedAmbiguously, this, &MainWindow::deleteActiveAnnotation);
    keys["delete_annotation"] = delAnn;
    // Copy the selected annotation / paste it at the cursor position.
    auto* copyAnn = new QShortcut(QKeySequence("Ctrl+Shift+C"), this);
    connect(copyAnn, &QShortcut::activated, this, &MainWindow::copySelectedAnnotation);
    keys["copy_annotation"] = copyAnn;
    auto* pasteAnn = new QShortcut(QKeySequence("Ctrl+Shift+V"), this);
    connect(pasteAnn, &QShortcut::activated, this, &MainWindow::pasteAnnotationAtCursor);
    keys["paste_annotation"] = pasteAnn;

    applyUserShortcuts(acts, keys);     // user INI overrides the defaults above
    m_actionRegistry = acts;            // kept: ScriptRunner triggers by name

    // Toolbar
    QToolBar* tb = addToolBar(tr("Main"));
    tb->setObjectName("mainToolbar");
    tb->setMovable(false);
    tb->addAction(tr("Open"), this, &MainWindow::openFile);
    tb->addAction(tr("Save"), this, &MainWindow::saveFile);
    tb->addAction(tr("Export"), this, &MainWindow::exportView);
    tb->addSeparator();
    tb->addAction(tr("Fit"), this, [this] { m_view->zoomToFit(); });
    tb->addAction("1:1", this, [this] { m_view->zoomActualSize(); });
    tb->addSeparator();
    tb->addAction("\u21bb", this, [this]{ applyTransform(Xform::RotCW); })->setToolTip(tr("Rotate 90\u00b0 clockwise ( ] )"));
    tb->addAction("\u21ba", this, [this]{ applyTransform(Xform::RotCCW); })->setToolTip(tr("Rotate 90\u00b0 counter-clockwise ( [ )"));
    tb->addAction("\u2194", this, [this]{ applyTransform(Xform::FlipH); })->setToolTip(tr("Flip horizontal (Ctrl+H)"));
    tb->addAction("\u2195", this, [this]{ applyTransform(Xform::FlipV); })->setToolTip(tr("Flip vertical (Ctrl+J)"));
    tb->addSeparator();

    // False-colour map for mono images.
    tb->addWidget(new QLabel(tr(" Colormap ")));
    m_cmapCombo = new QComboBox();
    for (int i = 0; i < kColormapCount; ++i)
        m_cmapCombo->addItem(colormapName(static_cast<Colormap>(i)));
    tb->addWidget(m_cmapCombo);
    // currentIndexChanged (not activated): the script `cmap` command drives
    // this combo programmatically, which `activated` never reports. The
    // model-sync path sets the index under a QSignalBlocker, so no loop.
    connect(m_cmapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (i >= 0) m_model.setColormap(static_cast<Colormap>(i));
    });

    // Modifiers that compose with any base map.
    m_invertCheck = new QCheckBox(tr("Inv"));
    m_invertCheck->setToolTip(tr("Invert the colormap (reverse the ramp)"));
    tb->addWidget(m_invertCheck);
    connect(m_invertCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_model.setCmapInvert(on);
    });

    m_splitCheck = new QCheckBox(tr("Split"));
    m_splitCheck->setToolTip(tr("Fold the ramp at a threshold: inverted below, normal above"));
    tb->addWidget(m_splitCheck);
    connect(m_splitCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_model.setCmapSplit(on);
        if (m_splitWidget) m_splitWidget->setVisible(on);
    });

    // Split break-point slider (visible only when Split is enabled).
    m_splitWidget = new QWidget();
    auto* sl = new QHBoxLayout(m_splitWidget);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->addWidget(new QLabel(tr(" break ")));
    m_splitSlider = new QSlider(Qt::Horizontal);
    m_splitSlider->setRange(0, 100);
    m_splitSlider->setValue(int(m_model.splitThreshold() * 100));
    m_splitSlider->setFixedWidth(110);
    sl->addWidget(m_splitSlider);
    tb->addWidget(m_splitWidget);
    m_splitWidget->setVisible(false);
    connect(m_splitSlider, &QSlider::valueChanged, this, [this](int v) {
        m_model.setSplitThreshold(v / 100.0);
    });
    tb->addSeparator();
    tb->addAction(aLeft);
    tb->addAction(aRight);
    tb->addAction(acts["image_only"]);

    // Annotation drawing tools: exclusive-optional, one shape per arm.
    tb->addSeparator();
    auto* toolGroup = new QActionGroup(this);
    toolGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    m_toolEllipse = tb->addAction(tr("\u25ef Ellipse"));
    m_toolEllipse->setToolTip(tr("Draw an ellipse annotation — drag outward from the centre"));
    m_toolLine = tb->addAction(tr("\u2571 Line"));
    m_toolLine->setToolTip(tr("Draw a line annotation — drag from start to end"));
    m_toolText = tb->addAction(tr("T Text"));
    m_toolText->setToolTip(tr("Place a text annotation — click the anchor point"));
    for (QAction* a : { m_toolEllipse, m_toolLine, m_toolText }) {
        a->setCheckable(true);
        toolGroup->addAction(a);
    }
    connect(toolGroup, &QActionGroup::triggered, this, [this](QAction* a) {
        ImageView::DrawTool t = ImageView::DrawTool::None;
        if (a->isChecked()) {
            if (a == m_toolEllipse)   t = ImageView::DrawTool::Ellipse;
            else if (a == m_toolLine) t = ImageView::DrawTool::Line;
            else                      t = ImageView::DrawTool::Text;
        }
        m_view->setDrawTool(t);
    });
}

static QString splitHduKey(const QString& key, int& hdu);   // defined below
static QString splitHduBase(const QString& key);            // defined below

// Where the Open/Append dialogs start. An empty dir means Qt falls back to
// the process working directory — "/" when Finder launched the app, which is
// useless. Prefer the directory of the most recently loaded image, then the
// last directory a file dialog picked (persisted), then home.
QString MainWindow::openDialogDir() const {
    int hdu = -1;
    const QString base = splitHduKey(m_currentPath, hdu);
    if (!base.isEmpty() && !base.startsWith(QLatin1String("mem://"))) {
        const QFileInfo fi(base);
        if (fi.dir().exists()) return fi.absolutePath();
    }
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QStringLiteral("NebulaScope"), QStringLiteral("recent"));
    const QString last = s.value(QStringLiteral("last_open_dir")).toString();
    if (!last.isEmpty() && QDir(last).exists()) return last;
    return QDir::homePath();
}

void MainWindow::rememberOpenDialogDir(const QString& firstPath) {
    const QFileInfo fi(firstPath);
    if (!fi.exists()) return;
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QStringLiteral("NebulaScope"), QStringLiteral("recent"));
    s.setValue(QStringLiteral("last_open_dir"), fi.absolutePath());
}

void MainWindow::openFile() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open image(s)"), openDialogDir(),
        tr("Astronomy & images (*.fits *.fit *.fts *.fz *.xisf *.jpg *.jpeg *.png *.tif *.tiff *.webp);;All files (*)"));
    if (!paths.isEmpty()) { rememberOpenDialogDir(paths.first()); addPaths(paths); }
}

void MainWindow::openPaths(const QStringList& paths) {
    addPaths(paths);
    // Visible confirmation even when the window is buried — Finder "Open
    // With" reports of silent no-ops are otherwise undiagnosable.
    if (!paths.isEmpty())
        statusBar()->showMessage(tr("Opened %n file(s)", nullptr, int(paths.size())), 4000);
}

// ---- multi-HDU list keys ----------------------------------------------------
// A list row can point at one HDU inside a FITS file. The row's UserRole then
// holds "<path>||hdu=<n>"; splitHduKey() recovers the file path and HDU index.
static QString makeHduKey(const QString& base, int hdu) {
    return base + QStringLiteral("||hdu=%1").arg(hdu);
}
static QString splitHduBase(const QString& key) {
    int hdu = -1;
    return splitHduKey(key, hdu);
}

static QString splitHduKey(const QString& key, int& hdu) {
    hdu = -1;
    const int at = key.lastIndexOf(QLatin1String("||hdu="));
    if (at < 0) return key;
    bool ok = false;
    const int n = key.mid(at + 6).toInt(&ok);
    if (!ok) return key;
    hdu = n;
    return key.left(at);
}

// Sidecar annotation file for an image: "<basename>_annotation.json" in the
// image's directory (multi-HDU keys share the file's sidecar). Empty for
// in-memory images.
static QString annotationSidecar(const QString& key) {
    int hdu = -1;
    const QString base = splitHduKey(key, hdu);
    if (base.isEmpty() || base.startsWith(QLatin1String("mem://"))) return {};
    const QFileInfo fi(base);
    return fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName()
         + QStringLiteral("_annotation.json");
}

// Append entries to the list without decoding. Selecting one (here or via the
// keyboard) is what triggers the actual load in showRow().
void MainWindow::addPaths(const QStringList& paths) {
    // The list holds each image ONCE. Re-opening a listed image selects its
    // existing row instead of appending a duplicate (status bar notes it).
    QSet<QString> listed;
    for (int r = 0; r < m_fileList->count(); ++r)
        listed << m_fileList->item(r)->data(Qt::UserRole).toString();

    QList<QListWidgetItem*> added;
    QListWidgetItem* firstExisting = nullptr;
    int nDup = 0;
    for (const QString& p : paths) {
        if (p.isEmpty()) continue;
        if (listed.contains(p)) {
            ++nDup;
            if (!firstExisting)
                for (int r = 0; r < m_fileList->count(); ++r)
                    if (m_fileList->item(r)->data(Qt::UserRole).toString() == p) {
                        firstExisting = m_fileList->item(r);
                        break;
                    }
            continue;
        }
        listed << p;
        int hduReq = -1;
        const QString base = splitHduKey(p, hduReq);   // re-imported lists may carry ||hdu=
        auto* it = new QListWidgetItem(
            hduReq < 0 ? QFileInfo(base).fileName()
                       : tr("%1 [HDU %2]").arg(QFileInfo(base).fileName()).arg(hduReq),
            m_fileList);
        it->setData(Qt::UserRole, p);
        it->setToolTip(p);
        // Culling tag: checked = keep. Blink through, uncheck the rejects (B),
        // then act on the tags from the list's context menu.
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
        added << it;

        // Multi-extension FITS: probed ASYNCHRONOUSLY (scheduleHduProbe) so a
        // large batch appears instantly; child HDU rows fill in behind.
        if (hduReq < 0) {
            const QString ext = QFileInfo(base).suffix().toLower();
            if (ext == "fits" || ext == "fit" || ext == "fts" || ext == "fz")
                m_hduProbeQueue << base;
        }
    }
    scheduleHduProbe();
    // Newly added images fill the EMPTY visible cells in raster order, in the
    // order they were given (command line, Finder selection, dialog): first
    // image -> first empty view (else the active view, as before), second
    // image -> next empty view, and so on. Same pattern as applySplitLayout:
    // activate, decode via showRow, then render SYNCHRONOUSLY — occupied()
    // only flips once a frame lands (so the async pipeline would keep
    // reporting the same cell "empty"), and async frames are dropped as soon
    // as the next cell activates.
    if (!added.isEmpty()) {
        ViewCell* firstCell = nullptr;
        for (QListWidgetItem* it : added) {
            // firstEmptyVisible() skips the active cell — but an EMPTY active
            // cell is the most natural first target (fresh split: top-left).
            ViewCell* target = !m_grid->activeCell()->occupied()
                                   ? m_grid->activeCell()
                                   : m_grid->firstEmptyVisible();
            if (!target) {
                if (firstCell) break;                   // views exhausted
                target = m_grid->activeCell();          // no empty cell: use active
            }
            m_grid->activate(target);
            if (!firstCell) firstCell = target;
            showRow(m_fileList->row(it));
            if (m_image.isValid()) {
                m_view->setDisplayImage(renderDisplayImage(m_image, m_model));
                m_view->zoomToFit();
            }
        }
        if (firstCell) {
            m_grid->activate(firstCell);                // first image's cell stays active
            QSignalBlocker blk(m_fileList);             // highlight without re-decoding
            m_fileList->setCurrentItem(added.first(), QItemSelectionModel::ClearAndSelect);
        }
    } else if (firstExisting) {
        // Everything was already listed: honour the open by SHOWING it.
        if (m_grid->activeCell()->occupied())
            if (ViewCell* empty = m_grid->firstEmptyVisible()) m_grid->activate(empty);
        if (m_fileList->currentItem() == firstExisting)
            displayPath(firstExisting->data(Qt::UserRole).toString());
        else
            m_fileList->setCurrentItem(firstExisting, QItemSelectionModel::ClearAndSelect);
    }
    if (nDup > 0)
        statusBar()->showMessage(tr("%n image(s) already in the list — not added again",
                                    nullptr, nDup), 4000);
    syncFileWatcher();
}

// Probe one queued FITS per event-loop tick for image HDUs; when a file has
// more than one, decorate its row and insert indented child rows behind it.
void MainWindow::scheduleHduProbe() {
    if (m_hduProbeQueue.isEmpty()) return;
    QTimer::singleShot(0, this, [this] {
        if (m_hduProbeQueue.isEmpty()) return;
        const QString base = m_hduProbeQueue.takeFirst();
        const QList<io::FitsHduEntry> hdus = io::listFitsImageHdus(base);
        if (hdus.size() > 1) {
            for (int i = 0; i < m_fileList->count(); ++i) {
                QListWidgetItem* it = m_fileList->item(i);
                if (it->data(Qt::UserRole).toString() != base) continue;
                it->setText(it->text() + tr("  ▾ %1 HDUs").arg(hdus.size()));
                int row = i;
                for (const io::FitsHduEntry& e : hdus) {
                    auto* child = new QListWidgetItem(
                        tr("    ⤷ HDU %1 · %2").arg(e.hdu).arg(e.summary));
                    child->setData(Qt::UserRole, makeHduKey(base, e.hdu));
                    child->setToolTip(tr("%1 — HDU %2").arg(base).arg(e.hdu));
                    child->setForeground(QColor("#8fa3b8"));
                    m_fileList->insertItem(++row, child);
                }
                break;
            }
        }
        scheduleHduProbe();                       // next file, next tick
    });
}

// Share the displayed stretch with the whole session: write it into every
// list image's stretch memory (it applies as each image is shown), and
// re-render already-visible cells immediately.
void MainWindow::applyStretchToAllList() {
    if (m_currentPath.isEmpty() || !m_image.isValid()) return;
    const StretchModel::State st = m_model.state();
    int n = 0;
    for (int i = 0; i < m_fileList->count(); ++i) {
        const QString key = m_fileList->item(i)->data(Qt::UserRole).toString();
        if (key.isEmpty() || key == m_currentPath) continue;
        m_stfByPath.insert(key, st);
        ++n;
    }
    for (int i = 0; i < m_grid->rows() * m_grid->cols(); ++i) {
        ViewCell* c = m_grid->cellAt(i);
        if (!c || c == m_grid->activeCell() || !c->image.isValid()) continue;
        c->stretch = st;
        c->hasStretch = true;
        StretchModel cm;
        cm.setChannelCount(c->image.channels());
        cm.setState(st);
        c->view()->setDisplayImage(DisplayRenderer::render(c->image, cm));
    }
    statusBar()->showMessage(
        tr("Stretch shared with %1 other image(s) — applies as each loads").arg(n), 4000);
}

void MainWindow::sharedStfStartup() {
    if (!m_image.isValid() || m_curStats.empty()) return;
    m_model.autoStretch(m_curStats);
    applyStretchToAllList();
}

// ---- crop -------------------------------------------------------------------

// Copy rect `r` of the current image into a new in-memory list entry at full
// depth. The plate solution survives EXACTLY: a crop only translates CRPIX,
// and the rebased solution is written as standard FITS cards — so even a
// property-only PixInsight XISF yields a crop that stays solved when saved.
void MainWindow::cropCurrentToRect(QRect r) {
    if (!m_image.isValid()) return;
    r = r.intersected(QRect(0, 0, m_image.width(), m_image.height()));
    if (r.width() < 2 || r.height() < 2) {
        statusBar()->showMessage(tr("Crop region is empty"), 3000);
        return;
    }
    const int ch = m_image.channels();
    ImageData out(r.width(), r.height(), ch, SampleFormat::Float32,
                  ch == 3 ? ColorSpace::RGB : ColorSpace::Gray);
    for (int c = 0; c < ch; ++c) {
        const float* src = m_image.plane<float>(c);
        float* dst = out.plane<float>(c);
        for (int y = 0; y < r.height(); ++y)
            std::memcpy(dst + std::size_t(y) * r.width(),
                        src + std::size_t(y + r.y()) * m_image.width() + r.x(),
                        std::size_t(r.width()) * sizeof(float));
    }

    // Header: keep everything except the now-stale WCS cards, then append the
    // rebased solution (also covers sources that were property-only).
    ImageHeader hdr = m_header;
    static const char* kWcsKeys[] = { "CTYPE1","CTYPE2","CRVAL1","CRVAL2",
        "CRPIX1","CRPIX2","CD1_1","CD1_2","CD2_1","CD2_2",
        "PC1_1","PC1_2","PC2_1","PC2_2","CDELT1","CDELT2","CROTA2" };
    hdr.cards.erase(std::remove_if(hdr.cards.begin(), hdr.cards.end(),
        [](const HeaderCard& c) {
            for (const char* k : kWcsKeys)
                if (c.key.compare(QLatin1String(k), Qt::CaseInsensitive) == 0) return true;
            return false;
        }), hdr.cards.end());
    const Wcs cw = m_wcs.valid() ? m_wcs.cropped(r.x(), r.y()) : Wcs();
    if (cw.valid()) cw.appendFitsCards(hdr);
    hdr.container = "In-memory";
    hdr.structure = QStringList{
        QStringLiteral("Crop of %1 · origin (%2, %3) · %4×%5%6")
            .arg(QFileInfo(m_currentPath).fileName()).arg(r.x()).arg(r.y())
            .arg(r.width()).arg(r.height())
            .arg(cw.valid() ? QStringLiteral(" · plate solution rebased") : QString()) };

    // Annotations translate with the pixels.
    std::vector<Annotation> anns = m_annByPath.value(m_currentPath);
    for (Annotation& a : anns) {
        a.x -= r.x(); a.y -= r.y();
        a.x2 -= r.x(); a.y2 -= r.y();
    }

    const StretchModel::State st = m_model.state();       // keep the look
    const QString srcName = QFileInfo(m_currentPath).completeBaseName();
    const QString key = addSyntheticImage(srcName + QStringLiteral("_crop"),
                                          std::move(out));
    m_syntheticHeaders.insert(key, hdr);
    if (!anns.empty()) m_annByPath.insert(key, anns);
    m_stfByPath.insert(key, st);
    displayPath(key);                                     // re-display with header+stretch
    statusBar()->showMessage(
        tr("Cropped %1×%2 at (%3, %4)%5 — Save Data As… keeps it")
            .arg(r.width()).arg(r.height()).arg(r.x()).arg(r.y())
            .arg(cw.valid() ? tr(", plate solution rebased") : QString()), 5000);
}

// ---- blink culling ----------------------------------------------------------

// The tag lives on the list row (checked = keep). Child HDU rows share their
// parent file, so file operations dedup by base path and skip mem:// entries.

void MainWindow::toggleCurrentTag() {
    // Group toggle over the highlighted rows (falling back to the current
    // one): if any is unchecked, check them all; only when every row is
    // already checked does B uncheck.
    QList<QListWidgetItem*> rows;
    for (QListWidgetItem* it : m_fileList->selectedItems())
        if (it->flags() & Qt::ItemIsUserCheckable) rows.append(it);
    if (rows.isEmpty()) {
        QListWidgetItem* cur = m_fileList->currentItem();
        if (cur && (cur->flags() & Qt::ItemIsUserCheckable)) rows.append(cur);
    }
    if (rows.isEmpty()) return;
    bool anyUnchecked = false;
    for (QListWidgetItem* it : rows)
        anyUnchecked = anyUnchecked || it->checkState() != Qt::Checked;
    const bool nowChecked = anyUnchecked;
    m_tagPropagating = true;
    for (QListWidgetItem* it : rows)
        it->setCheckState(nowChecked ? Qt::Checked : Qt::Unchecked);
    m_tagPropagating = false;
    if (rows.size() == 1)
        statusBar()->showMessage(tr("%1 — %2")
            .arg(rows.first()->text().trimmed(),
                 nowChecked ? tr("checked (keep)") : tr("unchecked")), 2000);
    else
        statusBar()->showMessage(
            nowChecked ? tr("%n image(s) checked (keep)", nullptr, int(rows.size()))
                       : tr("%n image(s) unchecked", nullptr, int(rows.size())), 2000);
}

void MainWindow::setSelectedTags(bool checked) {
    m_tagPropagating = true;
    for (QListWidgetItem* it : m_fileList->selectedItems())
        if (it->flags() & Qt::ItemIsUserCheckable)
            it->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    m_tagPropagating = false;
}

void MainWindow::sortListByTag() {
    // Stable partition: checked rows first, original order kept within each
    // group. Rebuild via takeItem so the widgets (and their states) survive.
    const QString curKey = m_fileList->currentItem()
        ? m_fileList->currentItem()->data(Qt::UserRole).toString() : QString();
    QList<QListWidgetItem*> checkedRows, uncheckedRows;
    while (m_fileList->count() > 0) {
        QListWidgetItem* it = m_fileList->takeItem(0);
        (it->checkState() == Qt::Checked ? checkedRows : uncheckedRows).append(it);
    }
    QSignalBlocker blk(m_fileList);
    for (QListWidgetItem* it : checkedRows)   m_fileList->addItem(it);
    for (QListWidgetItem* it : uncheckedRows) m_fileList->addItem(it);
    for (int i = 0; i < m_fileList->count(); ++i)
        if (m_fileList->item(i)->data(Qt::UserRole).toString() == curKey) {
            m_fileList->setCurrentRow(i, QItemSelectionModel::ClearAndSelect);
            break;
        }
}

void MainWindow::removeTaggedFromList(bool checked) {
    QSignalBlocker blk(m_fileList);
    m_fileList->clearSelection();
    for (int i = 0; i < m_fileList->count(); ++i) {
        QListWidgetItem* it = m_fileList->item(i);
        if ((it->checkState() == Qt::Checked) == checked) it->setSelected(true);
    }
    blk.unblock();
    removeSelected();                          // shared row/sidecar/memory logic
}

// Rekey every per-path map when a file moves (same idea as the mem:// rename
// in saveFile, factored for reuse).
void MainWindow::migratePathState(const QString& oldKey, const QString& newKey) {
    if (m_stfByPath.contains(oldKey))      m_stfByPath.insert(newKey, m_stfByPath.take(oldKey));
    if (m_annByPath.contains(oldKey))      m_annByPath.insert(newKey, m_annByPath.take(oldKey));
    if (m_annDirty.remove(oldKey))         m_annDirty.insert(newKey);
    if (m_xformByPath.contains(oldKey))    m_xformByPath.insert(newKey, m_xformByPath.take(oldKey));
    if (m_diskSizeByPath.contains(oldKey)) m_diskSizeByPath.insert(newKey, m_diskSizeByPath.take(oldKey));
    if (m_debayerByPath.contains(oldKey))  m_debayerByPath.insert(newKey, m_debayerByPath.take(oldKey));
    if (m_currentPath == oldKey)           m_currentPath = newKey;
}

void MainWindow::moveTaggedFiles(bool checked, const QString& destDir) {
    // Collect the FILES behind matching top-level rows (skip in-memory and
    // child HDU rows; a parent row moves the whole file).
    QStringList files;
    for (int i = 0; i < m_fileList->count(); ++i) {
        QListWidgetItem* it = m_fileList->item(i);
        if ((it->checkState() == Qt::Checked) != checked) continue;
        const QString key = it->data(Qt::UserRole).toString();
        int hdu = -1;
        const QString base = splitHduKey(key, hdu);
        if (hdu >= 0 || base.startsWith(QLatin1String("mem://"))) continue;
        if (QFileInfo::exists(base) && !files.contains(base)) files << base;
    }
    if (files.isEmpty()) {
        QMessageBox::information(this, tr("Move Frames"),
            tr("No %1 files to move.").arg(checked ? tr("checked") : tr("unchecked")));
        return;
    }
    QString dir = destDir;
    if (dir.isEmpty())
        dir = QFileDialog::getExistingDirectory(this,
            tr("Move %1 %2 frame(s) to…")
                .arg(files.size()).arg(checked ? tr("checked") : tr("unchecked")),
            openDialogDir());
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);                        // scripted destinations may be new

    int moved = 0;
    QStringList failed;
    for (const QString& base : files) {
        const QString dest = dir + QLatin1Char('/') + QFileInfo(base).fileName();
        if (QFileInfo::exists(dest)) { failed << QFileInfo(base).fileName() + tr(" (exists)"); continue; }
        if (!QFile::rename(base, dest)) {
            // Cross-volume: copy then remove.
            if (!QFile::copy(base, dest) || !QFile::remove(base)) {
                failed << QFileInfo(base).fileName();
                QFile::remove(dest);           // don't leave half a copy
                continue;
            }
        }
        // The annotation sidecar travels with its image.
        const QString sc = annotationSidecar(base);
        if (!sc.isEmpty() && QFileInfo::exists(sc)) {
            const QString scDest = dir + QLatin1Char('/') + QFileInfo(sc).fileName();
            if (!QFile::rename(sc, scDest)) {
                if (QFile::copy(sc, scDest)) QFile::remove(sc);
            }
        }
        // Rekey rows (parent + HDU children) and every per-path map.
        for (int i = 0; i < m_fileList->count(); ++i) {
            QListWidgetItem* it = m_fileList->item(i);
            const QString key = it->data(Qt::UserRole).toString();
            int hdu = -1;
            if (splitHduKey(key, hdu) != base) continue;
            const QString newKey = (hdu < 0) ? dest : makeHduKey(dest, hdu);
            it->setData(Qt::UserRole, newKey);
            it->setToolTip(hdu < 0 ? dest
                                   : QStringLiteral("%1 — HDU %2").arg(dest).arg(hdu));
            migratePathState(key, newKey);
        }
        ++moved;
    }
    syncFileWatcher();
    QString msg = tr("Moved %1 file(s) to %2").arg(moved).arg(QDir(dir).dirName());
    if (!failed.isEmpty())
        msg += tr(" — FAILED: %1").arg(failed.join(QLatin1String(", ")));
    statusBar()->showMessage(msg, 6000);
}

// ---- debayer ----------------------------------------------------------------

namespace {
// A wholesale stretch-state change (e.g. the colour-transport stretch fit):
// applied by the caller first (first-redo skip), guarded on the image still
// being current — the RotateAngleCmd idiom.
class StretchStateCmd : public QUndoCommand {
public:
    StretchStateCmd(MainWindow* w, QString path,
                    StretchModel::State prev, StretchModel::State next,
                    const QString& text)
        : m_w(w), m_path(std::move(path)),
          m_prev(std::move(prev)), m_next(std::move(next)) { setText(text); }
    void undo() override {
        if (m_w->currentPath() != m_path) { setObsolete(true); return; }
        m_w->applyStretchState(m_prev);
    }
    void redo() override {
        if (m_first) { m_first = false; return; }
        if (m_w->currentPath() != m_path) { setObsolete(true); return; }
        m_w->applyStretchState(m_next);
    }
private:
    MainWindow* m_w;
    QString m_path;
    StretchModel::State m_prev, m_next;
    bool m_first = true;
};

// Debayer change (per-image mode and/or global algorithm): applied by the
// caller first, so redo skips its first invocation (RotateAngleCmd idiom).
class DebayerCmd : public QUndoCommand {
public:
    DebayerCmd(MainWindow* w, QString path, int prevMode, int nextMode,
               int prevMethod, int nextMethod)
        : m_w(w), m_path(std::move(path)),
          m_prevMode(prevMode), m_nextMode(nextMode),
          m_prevMethod(prevMethod), m_nextMethod(nextMethod) {
        static const char* meth[] = { "superpixel", "bilinear", "RCD" };
        QString t = QCoreApplication::translate("astro::MainWindowHelpers", "debayer");
        if (prevMode != nextMode)
            t += QStringLiteral(" %1").arg(nextMode == -1 ? QCoreApplication::translate("astro::MainWindowHelpers", "off")
                 : nextMode == 0 ? QCoreApplication::translate("astro::MainWindowHelpers", "auto")
                 : QLatin1String(bayerPatternName(static_cast<BayerPattern>(nextMode))));
        if (prevMethod != nextMethod)
            t += QStringLiteral(" %1").arg(QLatin1String(meth[qBound(0, nextMethod, 2)]));
        setText(t);
    }
    void undo() override {
        if (m_w->currentPath() != m_path) { setObsolete(true); return; }
        m_w->applyDebayerChange(m_path, m_prevMode, m_prevMethod);
    }
    void redo() override {
        if (m_first) { m_first = false; return; }
        if (m_w->currentPath() != m_path) { setObsolete(true); return; }
        m_w->applyDebayerChange(m_path, m_nextMode, m_nextMethod);
    }
private:
    MainWindow* m_w;
    QString m_path;
    int m_prevMode, m_nextMode, m_prevMethod, m_nextMethod;
    bool m_first = true;
};
} // namespace

// The push half lives here, after StretchStateCmd's definition.
void MainWindow::pushStretchUndo() {
    if (m_undoBasePath.isEmpty()) return;
    m_undo->push(new StretchStateCmd(this, m_undoBasePath, m_undoBase,
                                     m_undoPendingNext, tr("stretch edit")));
    m_undoBase = m_undoPendingNext;
}

void MainWindow::applyStretchState(const StretchModel::State& st) {
    // Undo/redo path: must not re-record itself as a fresh edit.
    StretchSquelch sq(this);
    m_model.setState(st);
}

void MainWindow::applyDebayerChange(const QString& path, int mode, int method) {
    m_debayerByPath[path] = mode;
    m_imgCache.removeFile(splitHduBase(path));    // cached entries are POST-debayer
    if (method >= 0 && method != Preferences::get().debayerMethod) {
        Preferences::get().debayerMethod = method;
        Preferences::get().save();
        m_imgCache.clear();                       // global algorithm: everything stale
    }
    syncDebayerMenu();
    if (path == m_currentPath) displayPath(m_currentPath);
}

// Image ▸ Debayer ▸ Apply Choice to All in List: stamp the shown image's
// mode onto every listed frame (one capture stream = one sensor pattern).
// Not undoable — like the histogram's Apply to All, it's a bulk utility; the
// per-image radio (or another apply-all) reverses it.
void MainWindow::applyDebayerToAll() {
    if (m_currentPath.isEmpty()) return;
    const int mode = m_debayerByPath.value(m_currentPath, 0);
    m_imgCache.clear();                           // bulk debayer change
    int n = 0;
    for (int i = 0; i < m_fileList->count(); ++i) {
        const QString key = m_fileList->item(i)->data(Qt::UserRole).toString();
        if (key.isEmpty() || key == m_currentPath) continue;
        if (mode == 0) m_debayerByPath.remove(key);            // back to auto
        else           m_debayerByPath[key] = mode;
        ++n;
    }
    // Non-active cells showing a listed mono frame re-decode through the new
    // mode (same idiom as the auto-reload refresh).
    for (int i = 0; ViewCell* c = m_grid->cellAt(i); ++i) {
        if (c == m_grid->activeCell() || c->path.isEmpty()) continue;
        int hduReq = -1;
        const QString base = splitHduKey(c->path, hduReq);
        if (base.startsWith(QLatin1String("mem://"))) continue;
        io::LoadOptions lopts;
        lopts.fitsHdu = hduReq;
        io::LoadResult res = io::loadImage(base, lopts);
        if (!res.ok) continue;
        c->image = applyDebayer(std::move(res.image), res.header, c->path);
        c->header = std::move(res.header);
        c->stats = computeStats(c->image);
        c->view()->setSource(&c->image);
        StretchModel cellModel;
        cellModel.setChannelCount(c->image.channels());
        if (c->hasStretch) cellModel.setState(c->stretch);
        c->view()->setDisplayImage(DisplayRenderer::render(c->image, cellModel));
    }
    const QString what = (mode == 0)  ? tr("auto")
                       : (mode == -1) ? tr("off")
                       : QLatin1String(bayerPatternName(static_cast<BayerPattern>(mode)));
    statusBar()->showMessage(tr("Debayer “%1” applied to %n other list image(s)",
                                nullptr, n).arg(what), 4000);
}

void MainWindow::requestDebayerChange(int newMode, int newMethod) {
    if (m_currentPath.isEmpty()) return;
    const int oldMode   = m_debayerByPath.value(m_currentPath, 0);
    const int oldMethod = Preferences::get().debayerMethod;
    const int nm    = (newMode   == kKeepDebayer) ? oldMode   : newMode;
    const int nmeth = (newMethod == kKeepDebayer) ? oldMethod : newMethod;
    if (nm == oldMode && nmeth == oldMethod) return;
    applyDebayerChange(m_currentPath, nm, nmeth);
    m_undo->push(new DebayerCmd(this, m_currentPath, oldMode, nm, oldMethod, nmeth));
}

// Demosaic a freshly loaded mono frame according to the image's mode (auto /
// forced pattern / off) and the global algorithm preference. Non-CFA frames
// pass through untouched.
// Pure form of the debayer decision: mode/method passed as VALUES so the
// prefetch worker can replicate the display decode off the GUI thread with
// no shared state (and no possibility of the two drifting apart).
static ImageData applyDebayerPure(ImageData&& img, ImageHeader& hdr, int mode, int methodIdx) {
    if (!img.isValid() || img.channels() != 1) return std::move(img);
    if (mode < 0) return std::move(img);                       // forced off
    BayerPattern p = (mode == 0) ? bayerPatternFromHeader(hdr)
                                 : static_cast<BayerPattern>(mode);
    if (p == BayerPattern::None) return std::move(img);
    const auto method = static_cast<DebayerMethod>(qBound(0, methodIdx, 2));
    ImageData rgb = debayer(img, p, method);
    if (!rgb.isValid()) return std::move(img);
    const char* mname = method == DebayerMethod::RCD ? "RCD"
                      : method == DebayerMethod::Bilinear ? "bilinear" : "superpixel";
    hdr.structure << QStringLiteral("Debayered: %1, %2%3")
                         .arg(QLatin1String(bayerPatternName(p)), QLatin1String(mname),
                              method == DebayerMethod::Superpixel
                                  ? QStringLiteral(" (half size)") : QString());
    return rgb;
}

ImageData MainWindow::applyDebayer(ImageData&& img, ImageHeader& hdr, const QString& key) {
    return applyDebayerPure(std::move(img), hdr, m_debayerByPath.value(key, 0),
                            Preferences::get().debayerMethod);
}

// Keep the Image ▸ Debayer submenu radio state in step with the shown image.
void MainWindow::syncDebayerMenu() {
    const int mode = m_debayerByPath.value(m_currentPath, 0);
    const int idx = (mode == -1) ? 5 : mode;                   // off is the last entry
    for (int i = 0; i < 6; ++i)
        if (m_debayerModeActs[i]) {
            QSignalBlocker b(m_debayerModeActs[i]);
            m_debayerModeActs[i]->setChecked(i == idx);
        }
    const int m = qBound(0, Preferences::get().debayerMethod, 2);
    for (int i = 0; i < 3; ++i)
        if (m_debayerMethodActs[i]) {
            QSignalBlocker b(m_debayerMethodActs[i]);
            m_debayerMethodActs[i]->setChecked(i == m);
        }
}

// ---- auto-reload on external change -----------------------------------------

// Watch exactly the on-disk files behind the current list (multi-HDU rows
// share their file; mem:// entries have none).
void MainWindow::syncFileWatcher() {
    if (!m_fileWatcher) return;
    QSet<QString> want;
    for (int i = 0; i < m_fileList->count(); ++i) {
        int hduDummy = -1;
        const QString base = splitHduKey(m_fileList->item(i)->data(Qt::UserRole).toString(), hduDummy);
        if (!base.isEmpty() && !base.startsWith(QLatin1String("mem://")) &&
            QFileInfo::exists(base))
            want.insert(base);
    }
    const QStringList old = m_fileWatcher->files();
    if (!old.isEmpty()) m_fileWatcher->removePaths(old);
    if (!want.isEmpty()) m_fileWatcher->addPaths(QStringList(want.begin(), want.end()));
}

void MainWindow::onWatchedFileChanged(const QString& path) {
    // The decoded-image cache must never mask an external overwrite: evict
    // eagerly (the per-hit mtime check is the backstop).
    m_imgCache.removeFile(path);
    // Write-then-rename replaces the inode and silently drops the watch;
    // re-arm as soon as the new file exists.
    if (QFileInfo::exists(path) && !m_fileWatcher->files().contains(path))
        m_fileWatcher->addPath(path);
    if (m_autoReloadAct && !m_autoReloadAct->isChecked()) return;
    m_reloadPending.insert(path);
    m_reloadTimer->start();                       // restart the debounce window
}

void MainWindow::reloadChangedFiles() {
    const QSet<QString> pending = m_reloadPending;
    m_reloadPending.clear();
    for (const QString& base : pending) {
        if (!QFileInfo::exists(base)) continue;   // deleted / still being replaced
        int hduDummy = -1;
        // Active image first: full re-display (stretch memory and, for
        // unchanged dimensions, zoom/pan are preserved by displayPath).
        if (splitHduKey(m_currentPath, hduDummy) == base)
            displayPath(m_currentPath);
        // Inactive cells holding this file: re-decode and re-render through
        // the cell's own remembered stretch.
        for (int i = 0; i < m_grid->rows() * m_grid->cols(); ++i) {
            ViewCell* c = m_grid->cellAt(i);
            if (!c || c == m_grid->activeCell()) continue;
            int hduReq = -1;
            if (splitHduKey(c->path, hduReq) != base) continue;
            io::LoadOptions lopts;
            lopts.fitsHdu = hduReq;
            io::LoadResult res = io::loadImage(base, lopts);
            if (!res.ok) continue;
            c->image = applyDebayer(std::move(res.image), res.header, c->path);
            c->header = std::move(res.header);
            c->stats = computeStats(c->image);
            c->view()->setSource(&c->image);
            StretchModel cellModel;
            cellModel.setChannelCount(c->image.channels());
            if (c->hasStretch) cellModel.setState(c->stretch);
            c->view()->setDisplayImage(DisplayRenderer::render(c->image, cellModel));
        }
        statusBar()->showMessage(
            tr("Reloaded (changed on disk): %1").arg(QFileInfo(base).fileName()), 4000);
    }
}

// Register an in-memory image (e.g. a channel combine) and show it. It gets a
// synthetic "mem://" key so displayPath() serves it from m_synthetic instead of
// touching the disk; Save Data As… can later write it to a real file.
QString MainWindow::addSyntheticImage(const QString& name, ImageData&& img) {
    static int counter = 0;
    const QString key = QStringLiteral("mem://%1#%2").arg(name).arg(++counter);
    m_synthetic.insert(key, std::make_shared<ImageData>(std::move(img)));
    auto* it = new QListWidgetItem(name, m_fileList);
    it->setData(Qt::UserRole, key);
    it->setToolTip(name + tr("  (in-memory combine — use Save Data As… to keep)"));
    // Synthetic results are list rows like any other: they carry the culling
    // keep-check too (checked by default, like freshly opened files).
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(Qt::Checked);
    m_fileList->setCurrentItem(it, QItemSelectionModel::ClearAndSelect); // triggers showRow -> displayPath
    m_undo->push(new SyntheticImageCmd(this, key, name, m_synthetic.value(key)));
    return key;
}

void MainWindow::removeSyntheticEntry(const QString& key) {
    for (int i = 0; i < m_fileList->count(); ++i) {
        QListWidgetItem* it = m_fileList->item(i);
        if (it->data(Qt::UserRole).toString() != key) continue;
        m_fileList->clearSelection();
        it->setSelected(true);
        // No prompt: this runs from the undo stack; a modal mid-undo would
        // block the command machinery (and mem:// rows have no sidecar).
        removeSelectedRows(false); // full cleanup: stretch memory, synthetic map, empty state
        return;
    }
}

void MainWindow::restoreSyntheticEntry(const QString& key, const QString& name,
                                       std::shared_ptr<ImageData> img) {
    m_synthetic.insert(key, std::move(img));
    auto* it = new QListWidgetItem(name, m_fileList);
    it->setData(Qt::UserRole, key);
    it->setToolTip(name + tr("  (in-memory combine — use Save Data As… to keep)"));
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(Qt::Checked);
    m_fileList->setCurrentItem(it, QItemSelectionModel::ClearAndSelect);
}

// Rotate-by-angle dialog, configured for the current image: display
// thumbnail + the north-up preset measured from the WCS. Shared by the menu
// slot (exec) and ScriptRunner (show). Caller owns the returned dialog.
RotateDialog* MainWindow::makeRotateDialog() {
    if (!m_image.isValid()) return nullptr;
    // Small preview of the current display for the dialog's live thumbnail.
    const QImage thumb = DisplayRenderer::render(m_image, m_model)
        .scaled(360, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // North-up preset: measure the screen direction of celestial north at
    // the image centre; the rotation that sends it to "up" (screen angle
    // -90°, with visual-CCW positive) is rel = phi + 90.
    double northUp = std::numeric_limits<double>::quiet_NaN();
    if (m_wcs.valid()) {
        const double cx = (m_image.width() - 1) / 2.0, cy = (m_image.height() - 1) / 2.0;
        double ra = 0, dec = 0, nx = 0, ny = 0;
        if (m_wcs.pixelToSky(cx, cy, ra, dec)) {
            const bool south = dec > 89.0;               // sample away from the pole
            const double dd = south ? -1.0 / 60.0 : 1.0 / 60.0;
            if (m_wcs.skyToPixel(ra, dec + dd, nx, ny)) {
                double phi = qRadiansToDegrees(std::atan2(ny - cy, nx - cx));
                if (south) phi += 180.0;
                double t = currentRotationAngle() + phi + 90.0;
                while (t > 180.0) t -= 360.0;
                while (t < -180.0) t += 360.0;
                northUp = t;
            }
        }
    }
    auto* dlg = new RotateDialog(thumb, currentRotationAngle(), northUp, this);
    connect(dlg, &RotateDialog::applyRequested, this, [this](double a){ pushRotateTo(a); });
    return dlg;
}

// Combine-channels dialog over every MONO image in the list (loading those
// not yet decoded). Shared by the menu slot (exec) and ScriptRunner (show).
// Caller owns the returned dialog; null (+ *whyNot) when under two monos.
CombineDialog* MainWindow::makeCombineDialog(QString* whyNot) {
    std::vector<CombineDialog::Source> mono;
    for (int i = 0; i < m_fileList->count(); ++i) {
        QListWidgetItem* item = m_fileList->item(i);
        const QString p = item->data(Qt::UserRole).toString();
        std::shared_ptr<ImageData> img;
        auto syn = m_synthetic.constFind(p);
        if (syn != m_synthetic.constEnd()) img = syn.value();
        else {
            int hduReq = -1;
            const QString base = splitHduKey(p, hduReq);
            io::LoadOptions lopts;
            lopts.fitsHdu = hduReq;
            io::LoadResult res = io::loadImage(base, lopts);
            if (!res.ok) continue;
            img = std::make_shared<ImageData>(std::move(res.image));
        }
        if (img && img->channels() == 1) {
            // Bake this image's current view stretch (its stretch memory, or the
            // live model if it's the displayed image) into a value mapper, for
            // the dialog's "As displayed (view stretch)" pre-normalize mode.
            std::function<float(float)> viewMap;
            StretchModel::State st = (p == m_currentPath) ? m_model.state()
                                                          : m_stfByPath.value(p);
            if (st.valid) {
                const int N = 4096;
                auto lut = std::make_shared<std::vector<float>>(
                    buildLut(st.fn, st.chan[0], st.ghs, N));
                const double lo = st.lo[0], hi = st.hi[0];
                const ChannelStretch cs = st.chan[0];
                // INTERPOLATE the LUT, exactly as the display renderer does:
                // nearest-entry lookup quantizes the input to 4096 steps, and
                // through a steep transfer (a strong GHS around its symmetry
                // point) adjacent entries differ by slope/4096 — output steps
                // of a percent and more, i.e. visible posterization baked
                // into the combined DATA while the screen (interpolated)
                // looked smooth.
                viewMap = [lut, lo, hi, cs](float v) -> float {
                    if (!std::isfinite(v)) return 0.0f;
                    const double t = windowCoord(v, lo, hi, cs);
                    const double f = t * 4095.0;
                    const int i0 = f < 0 ? 0 : (f >= 4095.0 ? 4094 : int(f));
                    const double fr = std::min(1.0, std::max(0.0, f - i0));
                    return float((*lut)[std::size_t(i0)] * (1.0 - fr)
                               + (*lut)[std::size_t(i0) + 1] * fr);
                };
            }
            mono.push_back({ item->text(), img, std::move(viewMap) });
        }
    }
    if (mono.size() < 2) {
        if (whyNot) *whyNot = tr(
            "Load at least two single-channel (mono) images into the list first.");
        return nullptr;
    }
    return new CombineDialog(std::move(mono), this);
}

void MainWindow::combineChannels() {
    QString whyNot;
    CombineDialog* dlgp = makeCombineDialog(&whyNot);
    if (!dlgp) {
        QMessageBox::information(this, tr("Combine Channels"), whyNot);
        return;
    }
    if (dlgp->exec() == QDialog::Accepted) adoptCombineResult(*dlgp);
    dlgp->deleteLater();
}

// Land an accepted combine result in the session (shared by the modal slot
// and the ScriptRunner's non-modal dialog, via QDialog::accepted).
void MainWindow::adoptCombineResult(CombineDialog& dlg) {
    if (!dlg.hasResult()) return;
    // Land the result in an empty view when one exists (multi-view HOO/SHO
    // workflow); otherwise it replaces the active view's image.
    if (ViewCell* empty = m_grid->firstEmptyVisible()) m_grid->activate(empty);
    ImageData out = dlg.result();                 // copy out of the dialog
    addSyntheticImage(dlg.resultName(), std::move(out));
    if (dlg.resultDisplayReady()) {
        // Data is already display-stretched [0,1]: show it 1:1 (identity
        // linear window), not through a fresh auto-STF.
        for (int c = 0; c < 3; ++c) {
            m_model.setRange(c, 0.0, 1.0);
            ChannelStretch cs; cs.black = 0.0; cs.mid = 0.5; cs.white = 1.0;
            m_model.setChannel(c, cs);
        }
        m_model.setFn(StretchFn::Linear);
    }
}

// Tools ▸ Combine Stars: gather the RGB (or mono) images in the list, run the
// screen-blend dialog on their DISPLAY renditions, add the result to the list.
void MainWindow::combineStars() {
    std::vector<StarCombineDialog::Source> srcs;
    for (int i = 0; i < m_fileList->count(); ++i) {
        QListWidgetItem* item = m_fileList->item(i);
        const QString p = item->data(Qt::UserRole).toString();
        std::shared_ptr<ImageData> img;
        auto syn = m_synthetic.constFind(p);
        if (syn != m_synthetic.constEnd()) img = syn.value();
        else {
            int hduReq = -1;
            const QString base = splitHduKey(p, hduReq);
            io::LoadOptions lopts;
            lopts.fitsHdu = hduReq;
            io::LoadResult res = io::loadImage(base, lopts);
            if (!res.ok) continue;
            img = std::make_shared<ImageData>(std::move(res.image));
        }
        if (!img || !img->isValid()) continue;
        // "As displayed": each image goes through its own stretch memory (or
        // the live model for the shown image) — screening operates on the two
        // renditions the user actually prepared.
        StretchModel::State st = (p == m_currentPath) ? m_model.state()
                                                      : m_stfByPath.value(p);
        std::function<void(ImageData&)> toDisplay;
        if (st.valid) {
            toDisplay = [st](ImageData& d) {
                StretchModel local;
                local.setState(st);
                d = DisplayRenderer::renderFloat(d, local);
            };
        }
        srcs.push_back({ item->text(), img, std::move(toDisplay) });
    }
    if (srcs.size() < 2) {
        QMessageBox::information(this, tr("Combine Stars"),
            tr("Load the starless and the stars-only image into the list first."));
        return;
    }
    StarCombineDialog dlg(std::move(srcs), this);
    if (dlg.exec() == QDialog::Accepted && dlg.hasResult()) {
        if (ViewCell* empty = m_grid->firstEmptyVisible()) m_grid->activate(empty);
        ImageData out = dlg.result();
        addSyntheticImage(dlg.resultName(), std::move(out));
        // Screen output is display-ready [0,1]: show it 1:1.
        for (int c = 0; c < 3; ++c) {
            m_model.setRange(c, 0.0, 1.0);
            ChannelStretch cs; cs.black = 0.0; cs.mid = 0.5; cs.white = 1.0;
            m_model.setChannel(c, cs);
        }
        m_model.setFn(StretchFn::Linear);
    }
}

void MainWindow::appendToList() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Append image(s)"), openDialogDir(),
        tr("Astronomy & images (*.fits *.fit *.fts *.fz *.xisf *.jpg *.jpeg *.png *.tif *.tiff *.webp);;All files (*)"));
    if (!paths.isEmpty()) { rememberOpenDialogDir(paths.first()); addPaths(paths); }
}

void MainWindow::removeSelected() {
    // Interactive close prompts once per batch when unsaved annotation edits
    // would be lost; scripted runs never block on a modal.
    removeSelectedRows(!m_scriptDriving);
}

void MainWindow::removeSelectedRows(bool promptForAnnotations) {
    const auto sel = m_fileList->selectedItems();
    if (sel.isEmpty()) return;
    // Removing a file row also removes its indented HDU child rows (their keys
    // are "<path>||hdu=N"). Collect first, then delete.
    QList<QListWidgetItem*> doomed = sel;
    for (QListWidgetItem* it : sel) {
        const QString p = it->data(Qt::UserRole).toString();
        int hduDummy = -1;
        if (splitHduKey(p, hduDummy) != p) continue;         // it's already a child row
        const QString prefix = p + QStringLiteral("||hdu=");
        for (int i = 0; i < m_fileList->count(); ++i) {
            QListWidgetItem* other = m_fileList->item(i);
            if (other != it && other->data(Qt::UserRole).toString().startsWith(prefix)
                && !doomed.contains(other))
                doomed.append(other);
        }
    }
    // One modal for the whole batch when unsaved annotation edits would go
    // down with the closed images: save them all to their default sidecars
    // (overwriting), discard them, or cancel the close entirely and sort it
    // out image by image. Only rows with something to lose count — an
    // annotation set, or an orientation history the sidecar hasn't seen.
    if (promptForAnnotations) {
        QStringList dirtyKeys;
        for (QListWidgetItem* it : doomed) {
            const QString p = it->data(Qt::UserRole).toString();
            if (m_annDirty.contains(p)
                && (!m_annByPath.value(p).empty() || !m_xformByPath.value(p).isEmpty()))
                dirtyKeys << p;
        }
        if (!dirtyKeys.isEmpty()) {
            QMessageBox box(QMessageBox::Warning, tr("Unsaved annotations"),
                tr("%n image(s) being closed have unsaved annotations.",
                   nullptr, int(dirtyKeys.size())),
                QMessageBox::NoButton, this);
            QPushButton* save = box.addButton(tr("Save Annotations"),
                                              QMessageBox::AcceptRole);
            QPushButton* ignore = box.addButton(tr("Ignore and Close"),
                                                QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(save);
            box.exec();
            if (box.clickedButton() == save) {
                int unsavable = 0;
                for (const QString& k : dirtyKeys) {
                    const QString sc = annotationSidecar(k);
                    if (sc.isEmpty() || !writeAnnotationsFileFor(k, sc))
                        ++unsavable;             // mem:// or write failure
                }
                if (unsavable > 0)
                    statusBar()->showMessage(
                        tr("%n image(s) had no sidecar to save to (in-memory or write failure)",
                           nullptr, unsavable), 5000);
            } else if (box.clickedButton() != ignore) {
                return;                          // Cancel: nothing closes
            }
        }
    }
    // Close = free everything the app holds for the path: per-image side
    // tables, in-memory synthetics, and any decoded copy still stashed in a
    // non-active view cell (its pixmap included). Remaining unsaved
    // annotation edits are discarded with the image (counted for the status
    // message so the quit-time warning never blames a phantom row).
    QSet<QString> removedKeys;
    int lostAnn = 0;
    // Block currentRowChanged while rows go: Qt re-picks a current row after
    // each takeItem, and each pick would otherwise decode an image. One
    // display fix-up at the end replaces up to N intermediate decodes.
    QSignalBlocker rowBlk(m_fileList);
    for (QListWidgetItem* it : doomed) {
        const QString p = it->data(Qt::UserRole).toString();
        removedKeys.insert(p);
        m_stfByPath.remove(p);
        m_synthetic.remove(p);                           // free any in-memory combine
        m_syntheticHeaders.remove(p);
        m_annByPath.remove(p);
        if (m_annDirty.remove(p)) ++lostAnn;
        m_xformByPath.remove(p);
        m_sidecarOrientByPath.remove(p);
        m_diskSizeByPath.remove(p);
        m_debayerByPath.remove(p);
        for (int i = 0; ViewCell* c = m_grid->cellAt(i); ++i)
            if (c != m_grid->activeCell() && c->path == p) c->clearContent();
        delete m_fileList->takeItem(m_fileList->row(it));
    }
    if (lostAnn > 0)
        statusBar()->showMessage(
            tr("%n closed image(s) had unsaved annotations (discarded)",
               nullptr, lostAnn), 5000);
    rowBlk.unblock();
    syncFileWatcher();
    if (m_fileList->count() == 0) {
        // Last image closed: empty every view cell and the live state.
        m_currentPath.clear();
        m_image = ImageData();
        m_header = ImageHeader();
        updateIccTransform();
        m_wcs = Wcs();
        m_annotations->rebuild(0, 0, m_wcs, {});
        m_grid->clearAll();
        m_hist->setSource(nullptr);
        m_info->setData(nullptr, nullptr, {});
        m_pixelLabel->setText("\u2014");
        setWindowTitle(tr("NebulaScope \u2014 Inspector"));
    } else if (m_fileList->currentRow() < 0) {
        m_fileList->setCurrentRow(0, QItemSelectionModel::ClearAndSelect);
    } else if (removedKeys.contains(m_currentPath)) {
        // The displayed image was among the closed ones but another row kept
        // current status (no row-change signal fired): show it explicitly so
        // the active view doesn't keep presenting freed data.
        displayPath(m_fileList->currentItem()->data(Qt::UserRole).toString());
    }
}

// Empty the list and every view. Reuses the removeSelected() machinery: it
// already frees in-memory synthetics, drops HDU children, forgets stretch
// memory, and empties all cells when the last row goes.
// View ▸ Reload Original: back to "as if NebulaScope had just been started
// and this image opened" — fresh decode from disk, per-image stretch memory
// forgotten, so the first-view rules re-run (saved display function if the
// file carries one, else the plain ramp). Undoable as a single stretch step.
void MainWindow::reloadOriginal() {
    if (m_currentPath.isEmpty()) return;
    if (m_currentPath.startsWith(QLatin1String("mem://"))) {
        statusBar()->showMessage(tr("In-memory image — nothing on disk to reload"), 4000);
        return;
    }
    const QString path = m_currentPath;
    const StretchModel::State prev = m_model.state();
    flushStretchUndo();
    m_stfByPath.remove(path);            // forget display memory
    displayPath(path);                   // fresh decode + first-view stretch rules
    m_undo->push(new StretchStateCmd(this, path, prev, m_model.state(),
                                     tr("reload original")));
    statusBar()->showMessage(tr("Reloaded from disk — display as freshly opened"), 5000);
}

void MainWindow::clearImageList() {
    if (m_fileList->count() == 0) return;
    m_fileList->selectAll();
    removeSelected();
    statusBar()->showMessage(tr("List cleared — all images closed"), 4000);
}

void MainWindow::exportList() {
    if (m_fileList->count() == 0) return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export image list"), openDialogDir() + QStringLiteral("/images.txt"), tr("Text file (*.txt);;All files (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export failed"), tr("Could not write %1").arg(path));
        return;
    }
    QTextStream out(&f);
    for (int i = 0; i < m_fileList->count(); ++i)
        out << m_fileList->item(i)->data(Qt::UserRole).toString() << '\n';
    statusBar()->showMessage(tr("Exported list of %1 file(s)").arg(m_fileList->count()), 3000);
}

void MainWindow::importList() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import image list"), openDialogDir(), tr("Text file (*.txt);;All files (*)"));
    if (!path.isEmpty()) importListFile(path);
}

void MainWindow::applySplitLayout(int rows, int cols) {
    m_grid->setGrid(rows, cols);
    if (m_grid->layout()) m_grid->layout()->activate();   // final cell geometry now
    const int n = std::min(rows * cols, m_fileList->count());
    for (int i = 0; i < n; ++i) {                 // raster order: row-major cells
        m_grid->activate(m_grid->cellAt(i));      // swap state into cell i
        showRow(i);                               // decode list row i into it
        // Render synchronously: the async pipeline would drop this frame as
        // soon as the next cell activates (identity check) — the cell would
        // keep its pixels but never get a pixmap. Fit: the grid is new.
        if (m_image.isValid()) {
            m_view->setDisplayImage(renderDisplayImage(m_image, m_model));
            m_view->zoomToFit();
        }
    }
    if (n > 0) m_grid->activate(m_grid->cellAt(0));
}

void MainWindow::importListFile(const QString& listPath) {
    QFile f(listPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Import failed"), tr("Could not read %1").arg(listPath));
        return;
    }
    // Relative paths in the list are resolved against the list file's directory.
    const QDir base = QFileInfo(listPath).absoluteDir();
    QStringList paths;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;   // skip blanks/comments
        paths << (QFileInfo(line).isAbsolute() ? line : base.absoluteFilePath(line));
    }
    if (paths.isEmpty()) {
        statusBar()->showMessage(tr("List file had no entries"), 3000);
        return;
    }
    addPaths(paths);
    statusBar()->showMessage(tr("Imported %1 file(s)").arg(paths.size()), 3000);
}

void MainWindow::showRow(int row) {
    if (row < 0 || row >= m_fileList->count()) return;
    const QString path = m_fileList->item(row)->data(Qt::UserRole).toString();
    if (!path.isEmpty()) displayPath(path);
}

void MainWindow::nextImage() {
    const int n = m_fileList->count();
    if (n == 0) return;
    const int row = m_fileList->currentRow();
    m_fileList->setCurrentRow((row + 1) % n, QItemSelectionModel::ClearAndSelect); // wrap to top after last
}

void MainWindow::prevImage() {
    const int n = m_fileList->count();
    if (n == 0) return;
    const int row = m_fileList->currentRow();
    m_fileList->setCurrentRow((row - 1 + n) % n, QItemSelectionModel::ClearAndSelect); // wrap to bottom before first
}

// Discard the stored rotate/flip history for the current image: annotations
// walk back to the disk pixel frame with the exact inverse of that history,
// then the pixels are re-decoded from disk (no inverse resampling — a true
// restore). The cleared orientation persists on the next annotation save, and
// the undo stack is reset (its recorded frames no longer exist).
// Transfer the colour distribution of another loaded image onto the displayed
// one (sliced optimal transport). Both are taken AS DISPLAYED — the reference
// through its remembered stretch (or an auto-STF), the source through the live
// model — so "make this look like that" means what the user sees. The result
// is a new display-ready list entry; nothing is overwritten.
void MainWindow::transportColorsFromRef() {
    if (!m_image.isValid()) return;
    QStringList names;
    QList<QString> keys;
    for (int i = 0; i < m_fileList->count(); ++i) {
        QListWidgetItem* item = m_fileList->item(i);
        const QString p = item->data(Qt::UserRole).toString();
        if (p.isEmpty() || p == m_currentPath) continue;
        names << item->text();
        keys << p;
    }
    if (names.isEmpty()) {
        QMessageBox::information(this, tr("Transport Colors"),
            tr("Load a second image to use as the colour reference."));
        return;
    }
    bool ok = false;
    // Reference picker + transport strength in one small dialog.
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Transport Colors"));
    auto* form = new QFormLayout(&dlg);
    auto* combo = new QComboBox();
    combo->addItems(names);
    form->addRow(tr("Reference (colours to adopt):"), combo);
    auto* strengthRow = new QHBoxLayout();
    auto* strength = new QSlider(Qt::Horizontal);
    strength->setRange(0, 100);
    static int s_lastStrength = 100;               // remembered for the session
    strength->setValue(s_lastStrength);
    auto* strengthLbl = new QLabel(QStringLiteral("%1%").arg(strength->value()));
    strengthLbl->setMinimumWidth(40);
    connect(strength, &QSlider::valueChanged, strengthLbl,
            [strengthLbl](int v) { strengthLbl->setText(QStringLiteral("%1%").arg(v)); });
    strengthRow->addWidget(strength, 1);
    strengthRow->addWidget(strengthLbl);
    form->addRow(tr("Strength:"), strengthRow);
    auto* hint = new QLabel(tr("100% = full palette adoption; lower values blend\n"
                            "the transported colours with the original."));
    hint->setStyleSheet("color:#7e8b98; font-size:11px;");
    form->addRow(QString(), hint);
    // Both lossless options default ON (user call, 2026-08-30): the stretch
    // fit cannot posterize, and without the colour stage a starless pair —
    // whose transport is mostly a cross-channel rotation — comes back washed.
    static bool s_lastAsStretch = true, s_lastColourFit = true;
    auto* asStretchBox = new QCheckBox(tr("Apply as stretch fit (non-destructive)"));
    asStretchBox->setToolTip(tr("Instead of writing new pixels, fit per-channel B/M/W so the\n"
                             "display matches the transported colours — the data is untouched,\n"
                             "so nothing can posterize. Colour match is close, not exact\n"
                             "(cross-channel rotations are outside the stretch family)."));
    form->addRow(QString(), asStretchBox);
    asStretchBox->setChecked(s_lastAsStretch);
    auto* colourFitBox = new QCheckBox(tr("Also fit the cross-channel colour mix"));
    colourFitBox->setToolTip(tr("A second stage fitting a full 3\u00d73 colour mixer, alternated\n"
                             "with the curves \u2014 the cross-channel part per-channel curves\n"
                             "cannot express (a starless pair's transport is mostly exactly\n"
                             "that). Try with and without: both are one Undo apart."));
    colourFitBox->setChecked(s_lastColourFit);
    colourFitBox->setEnabled(asStretchBox->isChecked());
    connect(asStretchBox, &QCheckBox::toggled, colourFitBox, &QCheckBox::setEnabled);
    form->addRow(QString(), colourFitBox);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    ok = dlg.exec() == QDialog::Accepted;
    if (!ok) return;
    s_lastStrength = strength->value();
    s_lastAsStretch = asStretchBox->isChecked();
    s_lastColourFit = colourFitBox->isChecked();
    QString terr;
    if (!runColorTransport(keys[combo->currentIndex()], strength->value(), &terr,
                           asStretchBox->isChecked(),
                           asStretchBox->isChecked() && colourFitBox->isChecked()) &&
        !terr.isEmpty())
        QMessageBox::warning(this, tr("Transport Colors"), terr);
}

// The transport proper, shared by the picker slot and ScriptRunner: reference
// by list key, strength in percent. False (+ *errOut) on failure.
bool MainWindow::runColorTransport(const QString& key, int strengthPct,
                                   QString* errOut, bool asStretch,
                                   bool fitColourAdj) {
    if (!m_image.isValid()) { if (errOut) *errOut = tr("no image displayed"); return false; }
    // Decode the reference (or fetch the in-memory synthetic).
    std::shared_ptr<ImageData> refImg;
    auto syn = m_synthetic.constFind(key);
    if (syn != m_synthetic.constEnd()) refImg = syn.value();
    else {
        int hduReq = -1;
        const QString base = splitHduKey(key, hduReq);
        io::LoadOptions lopts;
        lopts.fitsHdu = hduReq;
        io::LoadResult res = io::loadImage(base, lopts);
        if (!res.ok) { if (errOut) *errOut = res.error; return false; }
        refImg = std::make_shared<ImageData>(std::move(res.image));
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    // Reference as displayed: its stretch memory, else an auto-STF.
    StretchModel refModel;
    refModel.setChannelCount(refImg->channels());
    StretchModel::State st = m_stfByPath.value(key);
    if (st.valid && !st.renormalize) refModel.setState(st);
    else refModel.autoStretch(computeStats(*refImg));
    const ImageData refDisp = DisplayRenderer::renderFloat(*refImg, refModel);

    // If the source was rotated/flipped in-session, run the transport in the
    // DISK frame: the rotated canvas carries black expansion borders that would
    // otherwise be baked into the result as real pixels (and its dark corner
    // pixels would tug the distribution). The result then ADOPTS the source's
    // orientation history, so it displays rotated identically — but reset /
    // rotate-back keep working and no border is ever baked.
    const QStringList srcOps = m_xformByPath.value(m_currentPath);
    std::shared_ptr<ImageData> baseHold;          // keeps a fresh decode alive
    const ImageData* srcPix = &m_image;
    if (!srcOps.isEmpty()) {
        auto syn2 = m_synthetic.constFind(m_currentPath);
        if (syn2 != m_synthetic.constEnd()) srcPix = syn2.value().get();
        else {
            int hduReq = -1;
            const QString base = splitHduKey(m_currentPath, hduReq);
            io::LoadOptions lopts2;
            lopts2.fitsHdu = hduReq;
            io::LoadResult lr = io::loadImage(base, lopts2);
            if (lr.ok) { baseHold = std::make_shared<ImageData>(std::move(lr.image)); srcPix = baseHold.get(); }
            // decode failure: fall back to the rotated pixels (old behaviour)
        }
    }
    const bool diskFrame = (srcPix != &m_image);
    const ImageData srcDisp = DisplayRenderer::renderFloat(*srcPix, m_model);

    // Restrict the distribution estimate to what each view actually SHOWS —
    // off-screen features (frame edges, unrelated field) must not steer the
    // match. Source: the active view (mapped back to the disk frame when the
    // transport runs there). Reference: its cell, if displayed.
    auto toRoi = [](const QRect& r) {
        TransportRoi t; t.x = r.x(); t.y = r.y(); t.w = r.width(); t.h = r.height(); return t;
    };
    // Inner rects throughout: a calibrated Match can put a rotation into the
    // navigation, and the visible-quad BOUNDING BOX would then sample
    // off-screen sky into the distribution estimate — the "khaki wash" when
    // reference framing and shown sky no longer correspond.
    TransportRoi srcRoi;
    if (!diskFrame) {
        srcRoi = toRoi(m_view->visibleInnerRect());
    } else {
        const QTransform T = diskToViewTransform(srcOps, QSize(srcPix->width(), srcPix->height()));
        const QRect diskRect = T.inverted().mapRect(QRectF(m_view->visibleInnerRect()))
                                   .toAlignedRect()
                                   .intersected(QRect(0, 0, srcPix->width(), srcPix->height()));
        srcRoi = toRoi(diskRect);
    }
    TransportRoi refRoi;                          // whole image unless shown in a cell
    for (int i = 0; i < m_grid->rows() * m_grid->cols(); ++i) {
        ViewCell* c = m_grid->cellAt(i);
        if (c && c != m_grid->activeCell() && c->path == key) {
            const QStringList refOps = m_xformByPath.value(key);
            QRect vis = c->view()->visibleInnerRect();
            if (!refOps.isEmpty()) {
                // refDisp is decoded from disk (unrotated); map the cell's view
                // rect back to that frame.
                const QTransform TR = diskToViewTransform(refOps, QSize(refImg->width(), refImg->height()));
                vis = TR.inverted().mapRect(QRectF(vis)).toAlignedRect()
                          .intersected(QRect(0, 0, refImg->width(), refImg->height()));
            }
            refRoi = toRoi(vis);
            break;
        }
    }
    // Make the one deviation from "only what is on screen" explicit: a
    // reference that is not displayed in any cell has no framing to honour,
    // so its WHOLE image feeds the estimate — say so, appended to whichever
    // success message follows.
    const QString refNote = refRoi.valid()
        ? QString()
        : tr(" · reference not displayed — matched over its full image");

    ColorTransportResult res = transportColors(srcDisp, refDisp, 15, 200000, srcRoi, refRoi);
    QApplication::restoreOverrideCursor();
    if (!res.ok) {
        if (errOut) *errOut = QString::fromStdString(res.error);
        return false;
    }
    // Partial transport: blend transported colours back toward the original.
    const double s = strengthPct / 100.0;
    if (s < 0.999) {
        for (int c = 0; c < res.image.channels(); ++c) {
            float* o = res.image.plane<float>(c);
            const float* orig = srcDisp.plane<float>(std::min(c, srcDisp.channels() - 1));
            const std::size_t np = res.image.samplesPerChannel();
            for (std::size_t i = 0; i < np; ++i)
                o[i] = float(orig[i] + s * (o[i] - orig[i]));
        }
    }
    // "As stretch": fit per-channel Linear B/M/W so the DISPLAY matches the
    // transported result — no pixel is touched, so nothing can posterize.
    // Cross-channel OT rotations are outside this family: report the RMSE.
    if (asStretch) {
        StretchModel::State st = m_model.state();
        const std::size_t np = srcDisp.samplesPerChannel();
        const std::size_t stride = std::max<std::size_t>(1, np / 60000);
        const int nch = std::min(3, srcPix->channels());
        QStringList rms;
        for (int c = 0; c < nch; ++c) {
            const int rc = std::min(c, res.image.channels() - 1);
            const double e = fitChannelStretch(srcPix->plane<float>(c),
                                               res.image.plane<float>(rc),
                                               np, stride,
                                               st.lo[c], st.hi[c], st.chan[c],
                                               /*intensityWeight=*/true);
            rms << QString::number(e, 'f', 4);
        }
        if (nch == 1) { st.chan[1] = st.chan[0]; st.chan[2] = st.chan[0]; }
        st.fn = StretchFn::Linear;
        st.adj = AdjustParams{};             // the fit absorbed the old display
        // Stage 2 (optional): fit the cross-channel colour adjustments so the
        // stretched display tracks OT's hue behaviour too. ALTERNATED with the
        // per-channel curves: fitting curves first against a rotated target
        // and rotating afterwards is a sequential compromise whose MMSE escape
        // is desaturation (the field symptom: starless pairs came back grey).
        // Block coordinate descent instead — each round re-fits the curves
        // against the colour-INVERTED target, then the colour params against
        // the curves' output; both stages end up solving the joint problem.
        QString colourNote;
        if (fitColourAdj && nch == 3 && res.image.channels() == 3) {
            std::vector<float> rawS[3], tv[3], tinv[3], d[3];
            for (int c = 0; c < 3; ++c) {
                const float* rawp = srcPix->plane<float>(c);
                const float* tgtp = res.image.plane<float>(c);
                rawS[c].reserve(np / stride + 1);
                tv[c].reserve(np / stride + 1);
                for (std::size_t i = 0; i < np; i += stride) {
                    rawS[c].push_back(rawp[i]);
                    tv[c].push_back(tgtp[i]);
                }
                tinv[c].resize(rawS[c].size());
                d[c].resize(rawS[c].size());
            }
            const std::size_t nS = rawS[0].size();
            AdjustParams fitted;                 // identity mix in round 0
            double e2 = 1.0;
            for (int round = 0; round < 3; ++round) {
                for (std::size_t i = 0; i < nS; ++i) {
                    float r = tv[0][i], g = tv[1][i], b = tv[2][i];
                    applyColorInverse(r, g, b, fitted);
                    tinv[0][i] = r; tinv[1][i] = g; tinv[2][i] = b;
                }
                rms.clear();
                for (int c = 0; c < 3; ++c) {
                    const double e = fitChannelStretch(rawS[c].data(), tinv[c].data(),
                                                       nS, 1,
                                                       st.lo[c], st.hi[c], st.chan[c],
                                                       /*intensityWeight=*/true);
                    rms << QString::number(e, 'f', 4);
                }
                for (int c = 0; c < 3; ++c) {
                    const ChannelStretch& cs = st.chan[c];
                    const double denom = std::max(1e-6, cs.white - cs.black);
                    const double m = std::min(0.999, std::max(0.001, (cs.mid - cs.black) / denom));
                    for (std::size_t i = 0; i < nS; ++i)
                        d[c][i] = float(mtf(windowCoord(rawS[c][i], st.lo[c], st.hi[c], cs), m));
                }
                // The colour stage is a full 3x3 mixer, solved in closed form —
                // strictly more expressive than the slider family (temperature,
                // tint, hue and saturation are all linear in RGB).
                e2 = fitColorMatrix(d[0].data(), d[1].data(), d[2].data(),
                                    tv[0].data(), tv[1].data(), tv[2].data(),
                                    nS, 1, fitted.mix);
            }
            st.adj = fitted;
            colourNote = tr(" · with colour fit: %1").arg(e2, 0, 'f', 4);
        }
        const StretchModel::State prev = m_model.state();
        {
            StretchSquelch sq(this);       // pushes its own command below
            m_model.setState(st);
        }
        m_undo->push(new StretchStateCmd(this, m_currentPath, prev, st,
                                         tr("colour-match stretch")));
        statusBar()->showMessage(
            tr("Colour match fitted as stretch (non-destructive) — RMSE %1%2")
                .arg(rms.join(QLatin1String(" / ")), colourNote) + refNote, 6000);
        return true;
    }

    if (ViewCell* empty = m_grid->firstEmptyVisible()) m_grid->activate(empty);
    const QSize srcDiskSize(srcPix->width(), srcPix->height());
    const QString newKey = addSyntheticImage(
        QStringLiteral("%1_ct").arg(QFileInfo(m_currentPath).completeBaseName()),
        std::move(res.image));
    // Result is display-ready [0,1]: show it 1:1.
    for (int c = 0; c < 3; ++c) {
        m_model.setRange(c, 0.0, 1.0);
        m_model.setChannel(c, ChannelStretch{});
    }
    m_model.setFn(StretchFn::Linear);
    if (diskFrame) {
        // Adopt the source's orientation so the result shows rotated the same
        // way; re-display to replay it onto the clean disk-frame pixels.
        m_xformByPath[newKey] = srcOps;
        m_diskSizeByPath[newKey] = srcDiskSize;
        displayPath(newKey);
    }
    statusBar()->showMessage(tr("Colours transported from %1")
                                 .arg(QFileInfo(key).fileName()) + refNote, 4000);
    return true;
}

// FORCEFUL by design (field report): rotation state lives in three places —
// per-image data histories (rot90/flip/rotate), per-cell calibrated-link
// "world" transforms, and each view's navigation (a calibrated Match can
// legitimately put a ROTATION into the viewport, which same-size auto-links
// then propagate to every other view). A reset that touches only one of the
// three leaves the user in a rotated state with no visible cause and no
// exit — closing and reopening images does not help, because cell view
// state deliberately survives for same-size stepping. So: vacate ALL of it,
// everywhere, and redisplay as freshly read. No confirmation — the result
// is exactly the well-defined "just opened" state.
void MainWindow::resetOrientation() {
    // 1. Data orientation histories, all images (annotations walked back to
    //    the disk frame first so their positions survive).
    for (auto it = m_xformByPath.begin(); it != m_xformByPath.end(); ++it) {
        auto an = m_annByPath.find(it.key());
        if (an != m_annByPath.end() && !an.value().empty()) {
            unmapAnnotationsToDiskFrame(an.value(), it.value());
            m_annDirty.insert(it.key());
        }
        bumpXformRev(it.key());
    }
    m_xformByPath.clear();
    m_rotBasePath.clear();                    // stale rotation-dialog base
    m_rotBase = ImageData();
    m_undo->clear();

    // 2. Calibrated-link worlds and every view's navigation (rotation lives
    //    in the viewport transform after a calibrated Match).
    for (int i = 0; ViewCell* c = m_grid->cellAt(i); ++i) {
        c->world = QTransform();
        c->calibrated = false;
        c->view()->resetTransform();
        c->view()->setMarker(-1, -1);
        c->setReadout(QString());
    }
    cancelRegister();

    // 3. Redisplay: non-active occupied cells re-decode from disk through
    //    their own stretch; the active image re-runs displayPath.
    for (int i = 0; ViewCell* c = m_grid->cellAt(i); ++i) {
        if (c == m_grid->activeCell() || c->path.isEmpty()) continue;
        int hduReq = -1;
        const QString base = splitHduKey(c->path, hduReq);
        if (base.startsWith(QLatin1String("mem://"))) { c->view()->zoomToFit(); continue; }
        io::LoadOptions lopts;
        lopts.fitsHdu = hduReq;
        io::LoadResult res = io::loadImage(base, lopts);
        if (!res.ok) continue;
        c->image = applyDebayer(std::move(res.image), res.header, c->path);
        c->header = std::move(res.header);
        c->stats = computeStats(c->image);
        c->view()->setSource(&c->image);
        StretchModel cellModel;
        cellModel.setChannelCount(c->image.channels());
        if (c->hasStretch) cellModel.setState(c->stretch);
        c->view()->setDisplayImage(DisplayRenderer::render(c->image, cellModel));
        c->xformRev = m_xformRev.value(c->path, 0);
        c->view()->zoomToFit();
    }
    if (!m_currentPath.isEmpty()) {
        displayPath(m_currentPath);           // fresh decode; replay is now a no-op
        m_view->zoomToFit();
    }
    statusBar()->showMessage(tr("Orientation reset everywhere — data, view links and navigation as freshly read"), 5000);
}

// Replay the orientation stashed from the annotation sidecar on the current
// image, through the SAME machinery as manual transforms (pixels, annotations,
// WCS, view links, and history recording all stay consistent — so a later
// annotation save re-records it).
void MainWindow::applySavedOrientation() {
    if (m_currentPath.isEmpty() || !m_image.isValid()) return;
    const QStringList ops = m_sidecarOrientByPath.value(m_currentPath);
    if (ops.isEmpty()) {
        statusBar()->showMessage(tr("No saved orientation for this image"), 3000);
        return;
    }
    for (const QString& n : ops) {
        if (n.startsWith(QLatin1String("rot:"))) doRotateArbitrary(n.mid(4).toDouble());
        else { Xform x; if (xformFromName(n, x)) doTransform(x); }
    }
    m_sidecarOrientByPath.remove(m_currentPath);   // now carried by the live history
    normalizeOrientation();
    statusBar()->showMessage(tr("Saved orientation applied (%1×%2)")
                                 .arg(m_image.width()).arg(m_image.height()), 4000);
}

// ---- Overlay panels --------------------------------------------------------
// Overlay mode lifts the three dock contents out of their docks and floats
// them over the image canvas in translucent boxes, so the panels stop
// displacing the display. Toggling back re-docks the SAME widgets — nothing
// is rebuilt, all state (list, histogram, info) survives.
QWidget* MainWindow::makeOverlayBox(QWidget* content) {
    auto* box = new QWidget(centralWidget());
    box->setAttribute(Qt::WA_StyledBackground, true);
    // Opaque panels let Qt CLIP their area out of the image view's repaints
    // (same fast path as docked panels). Any translucency forces the view AND
    // the panels to recomposite on every zoom/pan tick — user's choice.
    const double op = Preferences::get().overlayOpacity;
    if (op >= 0.999) {
        box->setStyleSheet("background: rgb(9,14,19); border: 1px solid #22303e; border-radius: 10px;");
        box->setAutoFillBackground(true);
        box->setAttribute(Qt::WA_OpaquePaintEvent, true);
    } else {
        box->setStyleSheet(QStringLiteral("background: rgba(9,14,19,%1); border: 1px solid #22303e; border-radius: 10px;")
                               .arg(op, 0, 'f', 2));
    }
    auto* l = new QVBoxLayout(box);
    l->setContentsMargins(7, 7, 7, 7);
    l->addWidget(content);
    // The box's cursor is the edge-grip affordance. The content must NEVER
    // inherit it: Qt does not deliver Leave to the box when the pointer moves
    // onto a child (field-verified), so an inherited grip cursor would stick
    // over the whole panel. Pin an explicit arrow; widgets with their own
    // cursors (line edits etc.) still override it.
    content->setCursor(Qt::ArrowCursor);
    box->setMouseTracking(true);
    box->installEventFilter(this);      // edge-drag resizing
    box->hide();
    return box;
}

void MainWindow::setOverlayPanels(bool on) {
    if (on == m_overlay) return;
    m_overlay = on;
    if (on) {
        if (!m_listContent) m_listContent = m_leftDock->widget();
        m_ovList = makeOverlayBox(m_listContent);
        m_ovInfo = makeOverlayBox(m_info);
        m_ovHist = makeOverlayBox(m_hist);
        m_listContent->setStyleSheet("QListWidget { background: transparent; }");
        m_leftDock->hide(); m_infoDock->hide(); m_rightDock->hide();
        m_ovList->show(); m_ovInfo->show(); m_ovHist->show();
        layoutOverlayPanels();
    } else {
        // Re-dock the contents first (setWidget reparents them out of the
        // boxes), THEN delete the empty boxes.
        m_listContent->setStyleSheet(QString());
        m_leftDock->setWidget(m_listContent);
        m_infoDock->setWidget(m_info);
        m_rightDock->setWidget(m_hist);
        delete m_ovList;  m_ovList = nullptr;
        delete m_ovInfo;  m_ovInfo = nullptr;
        delete m_ovHist;  m_ovHist = nullptr;
        m_leftDock->show(); m_infoDock->show(); m_rightDock->show();
    }
}

void MainWindow::layoutOverlayPanels() {
    if (!m_overlay || !centralWidget()) return;
    const QRect r = centralWidget()->rect();
    const int m = 14, gap = 10;
    if (m_ovLeftW <= 0) m_ovLeftW = std::min(280, std::max(200, r.width() / 5));
    if (m_ovHistW <= 0) m_ovHistW = std::min(430, std::max(300, r.width() / 3));
    const int lw = std::min(m_ovLeftW, r.width() / 2);
    const int hw = std::min(m_ovHistW, r.width() / 2);
    const int fullH = r.height() - 2 * m;
    const bool listOn = m_ovList && m_ovList->isVisible();
    const bool infoOn = m_ovInfo && m_ovInfo->isVisible();
    const int listH = infoOn && listOn ? int(fullH * m_ovSplit) : fullH;
    if (listOn) m_ovList->setGeometry(m, m, lw, listH);
    if (infoOn) m_ovInfo->setGeometry(m, listOn ? m + listH + gap : m, lw,
                                      listOn ? fullH - listH - gap : fullH);
    if (m_ovHist && m_ovHist->isVisible())
        m_ovHist->setGeometry(r.width() - m - hw, m, hw, fullH);
    for (QWidget* b : { m_ovList, m_ovInfo, m_ovHist }) if (b) b->raise();
}

// Edge-drag resizing for the overlay boxes: right edge of the left column,
// left edge of the histogram, and the seam under the list (list/info split).
bool MainWindow::eventFilter(QObject* o, QEvent* e) {
    auto* box = qobject_cast<QWidget*>(o);
    const bool isLeft = box && (box == m_ovList || box == m_ovInfo);
    const bool isHist = box && box == m_ovHist;
    if (m_overlay && (isLeft || isHist)) {
        const int grip = 8;
        auto* me = static_cast<QMouseEvent*>(e);
        switch (e->type()) {
        case QEvent::MouseMove: {
            const QPoint p = me->position().toPoint();
            const bool onW = isLeft ? (box->width() - p.x() <= grip) : (p.x() <= grip);
            const bool onS = (box == m_ovList) && m_ovInfo && m_ovInfo->isVisible()
                             && (box->height() - p.y() <= grip);
            if (m_ovDrag == 1) { m_ovLeftW = std::max(160, me->globalPosition().toPoint().x() - centralWidget()->mapToGlobal(QPoint(14,0)).x()); layoutOverlayPanels(); return true; }
            if (m_ovDrag == 2) { m_ovHistW = std::max(240, centralWidget()->mapToGlobal(QPoint(centralWidget()->width()-14,0)).x() - me->globalPosition().toPoint().x()); layoutOverlayPanels(); return true; }
            if (m_ovDrag == 3) { const int fullH = centralWidget()->height() - 28; m_ovSplit = qBound(0.15, double(me->globalPosition().toPoint().y() - centralWidget()->mapToGlobal(QPoint(0,14)).y()) / std::max(1, fullH), 0.85); layoutOverlayPanels(); return true; }
            // unsetCursor (not ArrowCursor) off the grips: children inherit the
            // container's cursor, so an explicit set would shadow theirs.
            if (onS)      box->setCursor(Qt::SplitVCursor);
            else if (onW) box->setCursor(Qt::SizeHorCursor);
            else          box->unsetCursor();
            break;
        }
        case QEvent::Leave:
            // Fires when the pointer moves INTO a child widget (list, info,
            // histogram) as well as off the panel — without this the resize
            // cursor set at the edge sticks over the whole panel.
            if (!m_ovDrag) box->unsetCursor();
            break;
        case QEvent::MouseButtonPress:
            if (me->button() == Qt::LeftButton) {
                const QPoint p = me->position().toPoint();
                if ((box == m_ovList) && m_ovInfo && m_ovInfo->isVisible() && box->height() - p.y() <= grip) { m_ovDrag = 3; return true; }
                if (isLeft && box->width() - p.x() <= grip) { m_ovDrag = 1; return true; }
                if (isHist && p.x() <= grip) { m_ovDrag = 2; return true; }
            }
            break;
        case QEvent::MouseButtonRelease:
            if (m_ovDrag) { m_ovDrag = 0; return true; }
            break;
        default: break;
        }
    }
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    if (m_overlay) layoutOverlayPanels();
}

void MainWindow::displayPath(const QString& path) {
    StretchSquelch sq(this);               // loading applies state, doesn't edit it
    ImageData loaded; ImageHeader hdr;
    auto syn = m_synthetic.constFind(path);
    if (syn != m_synthetic.constEnd() && syn.value()) {
        loaded = *syn.value();                           // in-memory result (copy)
        auto sh = m_syntheticHeaders.constFind(path);
        if (sh != m_syntheticHeaders.constEnd()) {
            hdr = sh.value();                            // crop: carried header (WCS etc.)
        } else {
            hdr.container  = "In-memory";
            hdr.nativeType = "32-bit float (channel combine)";
            hdr.structure  = QStringList{ QString("RGB combine · %1×%2 · 3 channels")
                                            .arg(loaded.width()).arg(loaded.height()) };
        }
    } else if (auto cached = m_imgCache.get(path, splitHduBase(path))) {
        // Decoded-image cache hit: the multi-second decode (inflate, unshuffle,
        // float promotion, debayer, stats) collapses to a memcpy. The entry is
        // the DISK-FRAME decode — the rotation replay below applies on top,
        // exactly as it would on a fresh read.
        loaded = *cached->image;
        hdr = cached->header;
        m_cachedStats = cached->stats;
    } else {
        int hduReq = -1;
        const QString base = splitHduKey(path, hduReq);
        io::LoadOptions lopts;
        lopts.fitsHdu = hduReq;                          // -1 = first image HDU
        io::LoadResult res = io::loadImage(base, lopts); // promoteToFloat = true
        if (!res.ok) {
            // Scripts must never block on a modal (a headless run would hang
            // forever); the failure lands in the status bar and on stderr,
            // and the next script assertion reports it.
            if (m_scriptDriving) {
                statusBar()->showMessage(tr("Open failed: %1").arg(res.error), 8000);
                fprintf(stderr, "open failed: %s\n", res.error.toLocal8Bit().constData());
            } else {
                QMessageBox::warning(this, tr("Open failed"), res.error);
            }
            return;
        }
        loaded = std::move(res.image);
        hdr    = std::move(res.header);
        loaded = applyDebayer(std::move(loaded), hdr, path);
        m_cacheInsertHdr = hdr;                          // pristine, pre-append
        m_cacheInsertPending = true;
    }
    m_image = std::move(loaded);
    m_header = std::move(hdr);
    updateIccTransform();
    syncDebayerMenu();

    // Metadata-less mosaic sniff: planetary/solar tools dump raw CFA frames
    // into plain grayscale PNG/TIFF, so no header will ever say BAYERPAT.
    // If the pixels carry the 2×2 signature, say so once per image — naming
    // the two patterns that fit the detected green diagonal (R vs B needs a
    // colour prior only the user has: the wrong twin shows a blue Sun).
    if (m_image.channels() == 1 && m_debayerByPath.value(path, 0) == 0 &&
        bayerPatternFromHeader(m_header) == BayerPattern::None &&
        !m_cfaHinted.contains(path)) {
        m_cfaHinted.insert(path);
        const CfaSniff sniff = sniffCfaMosaic(m_image);
        if (sniff.likely) {
            const QString msg = tr("Looks like an undecoded colour mosaic — try "
                                   "Image ▸ Debayer ▸ %1 or %2")
                .arg(QLatin1String(bayerPatternName(sniff.candidateA)),
                     QLatin1String(bayerPatternName(sniff.candidateB)));
            QTimer::singleShot(0, this, [this, msg] {
                statusBar()->showMessage(msg, 10000);
            });
        }
    }

    // Astrometric solution (FITS WCS keywords; PixInsight embeds the same
    // keywords in XISF). Enables the RA/Dec hover readout when present.
    m_wcs = Wcs::fromHeader(m_header);
    if (m_wcs.valid()) {
        m_header.structure << QStringLiteral("Astrometric solution: %1").arg(m_wcs.summary());
    } else {
        // No plate solution — surface the capture software's pointing keywords
        // (RA/DEC, OBJCTRA/OBJCTDEC) so the user still sees where the frame is.
        double ra = 0, dec = 0;
        if (Wcs::parsePointing(m_header, ra, dec))
            m_header.structure << QStringLiteral("Telescope pointing (no plate solution): %1 %2")
                                      .arg(Wcs::formatRa(ra), Wcs::formatDec(dec));
    }

    m_model.setChannelCount(m_image.channels());
    // Disk-frame statistics: reused from the cache on a hit (they were
    // computed from these very pixels), recomputed and stored on a miss.
    const std::vector<ChannelStats> stats =
        !m_cachedStats.empty() ? m_cachedStats : computeStats(m_image);
    m_curStats = stats;                                  // cache for Copy/Paste Stretch anchors
    if (m_cacheInsertPending) {
        m_cacheInsertPending = false;
        ImageCache::Entry e;
        e.image = std::make_shared<const ImageData>(m_image);   // disk frame (replay is later)
        e.header = std::move(m_cacheInsertHdr);
        e.stats = stats;
        m_imgCache.insert(path, splitHduBase(path), std::move(e));
    }
    m_cachedStats.clear();

    // Per-image STF memory: restore this file's last stretch, or auto-stretch on
    // first visit. Set m_currentPath first so the change handler saves correctly.
    m_currentPath = path;
    {   // Recent-images history (strip any ||hdu= suffix; skip in-memory results).
        int hduDummy = -1;
        rememberRecent(QStringLiteral("recentImages"), splitHduKey(path, hduDummy),
                       Preferences::get().recentImagesMax);
    }

    // Auto-load the sidecar annotation file on the first visit to this image.
    bool sidecarAdjValid = false;              // adjustments read from the sidecar,
    AdjustParams sidecarAdj;                   // applied after the stretch is set up
    if (Preferences::get().autoLoadSidecar && !m_annByPath.contains(path)) {
        const QString sc = annotationSidecar(path);
        if (!sc.isEmpty() && QFile::exists(sc)) {
            QFile f(sc);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                if (doc.object().contains(QLatin1String("adjustments"))) {
                    sidecarAdj = adjustFromJson(doc.object()["adjustments"].toObject());
                    sidecarAdjValid = true;
                }
                // A saved display state seeds the per-image STF memory: the
                // remembered-stretch branch below then applies it exactly as
                // if this session had set it, winning over auto-STF and any
                // embedded display function — the sidecar is the user's
                // explicit save, so it has the last word.
                if (doc.object().contains(QLatin1String("display"))) {
                    const StretchModel::State st = StretchModel::stateFromJson(
                        doc.object()["display"].toObject());
                    if (st.valid && !m_stfByPath.contains(path))
                        m_stfByPath.insert(path, st);
                }
                std::vector<Annotation> anns = AnnotationLayer::fromJson(doc);
                if (!anns.empty()) {
                    // The sidecar records the orientation the annotations were
                    // made in — as the LITERAL op sequence of that session. The
                    // image itself is always shown as stored on disk: walk the
                    // annotations back to the disk frame and only STASH the
                    // (canonicalized) orientation. It is applied on demand via
                    // Image ▸ Apply Saved Orientation, never automatically.
                    QStringList fileOps;
                    for (const auto& v : doc.object()["orientation"].toArray())
                        fileOps << v.toString();
                    m_diskSizeByPath[path] = QSize(m_image.width(), m_image.height());
                    if (!fileOps.isEmpty())
                        unmapAnnotationsToDiskFrame(anns, fileOps);   // exact inverse walk
                    const QStringList canon = canonicalXforms(fileOps);
                    if (!canon.isEmpty()) m_sidecarOrientByPath[path] = canon;
                    m_annByPath[path] = std::move(anns);
                    mapAnnotationsFromDiskFrame(m_annByPath[path]);   // through in-session ops, if any
                    statusBar()->showMessage(
                        tr("Loaded %1 annotation(s) from %2%3")
                            .arg(m_annByPath[path].size()).arg(QFileInfo(sc).fileName(),
                                 canon.isEmpty() ? QString()
                                 : tr(" — saved orientation available (Image ▸ Apply Saved Orientation)")), 6000);
                }
            }
        }
    }
    m_diskSizeByPath[path] = QSize(m_image.width(), m_image.height());  // pre-orientation dims
    reapplyStoredXforms();      // image reloads unrotated from disk; catch it up
    refreshAnnotations();
    auto remembered = m_stfByPath.constFind(path);
    if (remembered != m_stfByPath.constEnd() && remembered.value().valid) {
        StretchModel::State st = remembered.value();
        // A pasted "normalized" stretch defers its window to the target: derive
        // black/mid/white from THIS image's own robust stats (median+MAD anchors)
        // so the look carries across differing data ranges without posterizing.
        if (st.renormalize) {
            if (st.anchored) {
                applyAnchorsToStats(st, stats);
            } else {
                for (int c = 0; c < 3; ++c) {
                    const int si = std::min(c, int(stats.size()) - 1);
                    if (si >= 0) { st.lo[c] = stats[si].min; st.hi[c] = stats[si].max; }
                }
            }
            st.renormalize = false;
        }
        // Adapt a mono-sourced stretch to an RGB target (and vice versa).
        if (st.count == 1)
            for (int c = 1; c < 3; ++c) { st.chan[c] = st.chan[0]; st.lo[c] = st.lo[0]; st.hi[c] = st.hi[0]; }
        st.count = m_image.channels();
        m_model.setState(st);                            // re-apply remembered/pasted STF
        m_stfByPath.insert(path, st);                    // persist finalized (flag cleared)
    } else {
        m_model.linearWindow(stats);                     // first visit: gentle linear window (min → p99)
        // Adjustments never leak across images: reset to identity, or to what
        // this image's sidecar carries.
        m_model.setAdjust(sidecarAdjValid ? sidecarAdj : AdjustParams{});
        if (m_header.displayFn.valid && m_image.isValid()) {
            // The file carries the producing app's screen stretch (XISF
            // DisplayFunction — PixInsight's STF): open looking exactly as it
            // did there. Absolute shadows/highlights are windowed into this
            // image's model range; PI's midtones parameter IS the MTF pivot
            // ratio, so mid = b + m·(w − b). The white point may exceed the
            // data maximum (PI clips at 1.0) — the render algebra extrapolates
            // the window exactly, no clamp needed.
            const DisplayFunction& df = m_header.displayFn;
            bool refitted = false;
            // Pass 1 — per-channel stretches in each channel's normalized
            // window, plus each ORIGINAL curve's display value at its data
            // maximum. PI's STF lives on the [0,1] container, so its white
            // point (usually 1.0) can sit FAR beyond the data — verbatim
            // import parks the W handle ~1/tmax plot-widths off-screen and
            // degrades every histogram control.
            const int nch = std::min(m_image.channels(), 3);
            ChannelStretch pcs[3];
            bool allFar = true;
            double S = 0.0;
            // The far-white algebra below reads t = 1 as THIS channel's data
            // maximum (farWhiteEndpoint / rebaseFarWhiteTo), so the import
            // runs on per-channel ranges regardless of the axis policy; the
            // common-axis pooling afterwards re-expresses the result
            // display-invariantly.
            for (int c = 0; c < nch && c < int(stats.size()); ++c)
                m_model.setRange(c, stats[c].min,
                                 std::max(double(stats[c].min) + 1e-6, double(stats[c].max)));
            for (int c = 0; c < nch; ++c) {
                const double lo = m_model.lo(c), hi = m_model.hi(c);
                if (!(hi > lo)) { allFar = false; continue; }
                const int k = (m_image.channels() >= 3) ? c : 0;   // mono: K component
                ChannelStretch cs;
                cs.black = std::max(0.0, (df.s[k] - lo) / (hi - lo));
                cs.white = std::max(cs.black + 1e-6, (df.h[k] - lo) / (hi - lo));
                cs.mid   = cs.black + df.m[k] * (cs.white - cs.black);
                pcs[c] = cs;
                allFar = allFar && cs.white > 1.001;
                S = std::max(S, farWhiteEndpoint(cs));
            }
            // Pass 2 — rebase against ONE common output level (the brightest
            // channel's endpoint): its white lands exactly at its data max,
            // the others proportionally, so the calibrated inter-channel
            // balance survives untouched. Exact in closed form.
            for (int c = 0; c < nch; ++c) {
                ChannelStretch cs = pcs[c];
                if (allFar && S > 0.0) {
                    cs = rebaseFarWhiteTo(cs, S);
                    refitted = true;
                }
                m_model.setChannel(c, cs);
            }
            // An UNLINKED saved STF (per-channel midtones differing widely)
            // equalizes the channels on screen — it visually cancels a
            // calibrated (e.g. SPCC) colour balance. Field-validated remedy:
            // the linked auto-stretch. Say so.
            bool unlinked = false;
            if (nch >= 3) {
                double mn = 1e9, mx = 0.0;
                for (int c = 0; c < nch; ++c) {
                    const double m = std::max(1e-9, df.m[c]);
                    mn = std::min(mn, m);
                    mx = std::max(mx, m);
                }
                unlinked = mx / mn > 1.25;
            }
            QString msg = refitted
                ? tr("PixInsight display function applied (rebased to the data range)")
                : tr("PixInsight display function applied");
            msg += unlinked
                ? tr(" — unlinked STF: Shift+U preserves calibrated colour")
                : tr(" — Reset (R) for the plain ramp");
            // Deferred: later messages in this same call stack (the image
            // info line, openPaths' count) would instantly overwrite it.
            const int dur = unlinked ? 8000 : 6000;
            QTimer::singleShot(0, this, [this, msg, dur] {
                statusBar()->showMessage(msg, dur);
            });
        }
    }

    m_view->setSource(&m_image);
    m_hist->setSource(&m_image);
    m_info->setData(&m_image, &m_header, stats);

    // Colormap selector: only meaningful for mono images; reflect remembered map.
    if (m_cmapCombo) {
        const bool mono = m_image.channels() == 1;
        m_cmapCombo->setEnabled(mono);
        QSignalBlocker blk(m_cmapCombo);
        const Colormap cm = mono ? m_model.colormap() : Colormap::Gray;
        m_cmapCombo->setCurrentIndex(int(cm));
        if (m_invertCheck) {
            m_invertCheck->setEnabled(mono);
            QSignalBlocker bi(m_invertCheck);
            m_invertCheck->setChecked(mono && m_model.cmapInvert());
        }
        if (m_splitCheck) {
            m_splitCheck->setEnabled(mono);
            QSignalBlocker bc(m_splitCheck);
            m_splitCheck->setChecked(mono && m_model.cmapSplit());
        }
        if (m_splitSlider) {
            QSignalBlocker bs(m_splitSlider);
            m_splitSlider->setValue(int(m_model.splitThreshold() * 100));
        }
        if (m_splitWidget) m_splitWidget->setVisible(mono && m_model.cmapSplit());
    }

    // Common axis: whichever path set the ranges above (remembered state,
    // renormalized paste, an imported display function — all per channel),
    // re-express them on ONE pooled range. Display unchanged; only the plot's
    // axis is, so channel offsets show as offsets.
    if (m_model.commonAxis() && m_image.channels() >= 3 && stats.size() >= 3) {
        bool differ = false;
        for (int c = 1; c < 3; ++c)
            differ = differ || m_model.lo(c) != m_model.lo(0) || m_model.hi(c) != m_model.hi(0);
        if (differ) {
            double pmn, pmx;
            StretchModel::pooledRange(stats, 3, pmn, pmx);
            const double lo[3] = { pmn, pmn, pmn };
            const double hi[3] = { std::max(pmn + 1e-6, pmx), std::max(pmn + 1e-6, pmx), std::max(pmn + 1e-6, pmx) };
            StretchSquelch sq(this);
            m_model.rebaseRanges(lo, hi);
        }
    }

    updateDisplay();
    // Preserve zoom/pan when stepping between images of identical geometry so a
    // zoomed-in region stays put for comparison; otherwise fit the new image.
    if (m_image.width() != m_lastW || m_image.height() != m_lastH)
        m_view->zoomToFit();
    m_lastW = m_image.width();
    m_lastH = m_image.height();

    const QString name = QFileInfo(path).fileName();
    setWindowTitle(tr("NebulaScope \u2014 %1").arg(name));
    // Surface the demosaic decision right where the eye lands on open.
    QString debayerNote;
    for (const QString& s : m_header.structure)
        if (s.startsWith(QLatin1String("Debayered: "))) {
            debayerNote = tr("   \u00b7 debayered %1").arg(s.mid(11));
            break;
        }
    statusBar()->showMessage(tr("%1   %2\u00d7%3   %4 ch   [%5/%6]%7")
        .arg(name).arg(m_image.width()).arg(m_image.height()).arg(m_image.channels())
        .arg(m_fileList->currentRow() + 1).arg(m_fileList->count())
        .arg(debayerNote), 4000);

    schedulePrefetch();
}

// While the user looks at this image, decode its list neighbours into the
// cache — the direction of the next blink is unknown, so both. The worker
// touches no GUI state: the debayer decision is captured as values and
// replayed through the same pure function the display decode uses.
void MainWindow::schedulePrefetch() {
    if (m_imgCache.budgetBytes() <= 0) return;
    const int n = m_fileList->count();
    const int row = m_fileList->currentRow();
    if (n < 2 || row < 0) return;
    m_prefetchQueue.clear();
    for (int d : { 1, -1 }) {
        const int r = (row + d + n) % n;
        if (r == row) continue;
        const QString key = m_fileList->item(r)->data(Qt::UserRole).toString();
        if (key.isEmpty() || key.startsWith(QLatin1String("mem://"))) continue;
        if (m_imgCache.contains(key) || key == m_prefetchInFlight) continue;
        if (!m_prefetchQueue.contains(key)) m_prefetchQueue << key;
    }
    startNextPrefetch();
}

void MainWindow::startNextPrefetch() {
    if (!m_prefetchInFlight.isEmpty() || m_prefetchQueue.isEmpty()) return;
    const QString key = m_prefetchQueue.takeFirst();
    int hduReq = -1;
    const QString base = splitHduKey(key, hduReq);
    const int mode = m_debayerByPath.value(key, 0);
    const int method = Preferences::get().debayerMethod;
    m_prefetchInFlight = key;
    if (!m_prefetchWatcher) {
        m_prefetchWatcher = new QFutureWatcher<PrefetchResult>(this);
        connect(m_prefetchWatcher, &QFutureWatcher<PrefetchResult>::finished, this, [this] {
            const PrefetchResult r = m_prefetchWatcher->result();
            m_prefetchInFlight.clear();
            // Insert only if the file is still exactly what was decoded — a
            // write that landed mid-decode must not be masked.
            const QFileInfo fi(r.base);
            if (r.ok && fi.exists() && fi.lastModified() == r.mtimeBefore &&
                fi.size() == r.sizeBefore && !m_imgCache.contains(r.key))
                m_imgCache.insert(r.key, r.base, ImageCache::Entry(r.entry));
            startNextPrefetch();
        });
    }
    m_prefetchWatcher->setFuture(QtConcurrent::run([key, base, hduReq, mode, method]() {
        PrefetchResult r;
        r.key = key;
        r.base = base;
        const QFileInfo fi(base);
        if (!fi.exists()) return r;
        r.mtimeBefore = fi.lastModified();
        r.sizeBefore = fi.size();
        io::LoadOptions lopts;
        lopts.fitsHdu = hduReq;
        io::LoadResult res = io::loadImage(base, lopts);
        if (!res.ok) return r;
        ImageData img = applyDebayerPure(std::move(res.image), res.header, mode, method);
        r.entry.stats = computeStats(img);
        r.entry.header = std::move(res.header);
        r.entry.image = std::make_shared<const ImageData>(std::move(img));
        r.ok = true;
        return r;
    }));
}

// ---- save dialogs with INLINE format options --------------------------------
// The format options (pixel depth for PNG/TIFF, quality for JPEG/WebP, XISF
// data-block compression) live IN the save dialog, enabled by the selected
// filter — no follow-up modal after picking a filename. Needs the Qt
// (non-native) dialog: the native one cannot host extra widgets. Values are
// remembered for the session, as the old follow-up dialogs were.
namespace {
int s_expDepth   = 0;      // 0 = 8-bit, 1 = 16-bit
int s_expQuality = 90;     // JPEG/WebP quality
int s_xisfComp   = 0;      // 0 = Zstd, 1 = Zlib, 2 = uncompressed

QString filterSuffix(const QString& filter) {          // "PNG (*.png)" -> "png"
    const int a = filter.indexOf(QLatin1String("(*."));
    if (a < 0) return {};
    const int b = filter.indexOf(QLatin1Char(')'), a);
    return filter.mid(a + 3, b - a - 3).section(QLatin1Char(' '), 0, 0);
}

// Append `row` (a container of option widgets) under the dialog's grid.
void addOptionRow(QFileDialog& dlg, QWidget* row) {
    if (auto* grid = qobject_cast<QGridLayout*>(dlg.layout()))
        grid->addWidget(row, grid->rowCount(), 0, 1, grid->columnCount());
}

// Native-dialog behaviour the Qt dialog lacks: clicking ANY existing image
// adopts its name (the extension then follows the chosen format). The Qt
// dialog greys out files that don't match the active filter — so keep the
// filter combo as the FORMAT selector, but re-widen the underlying model to
// every adoptable image type each time the filter changes.
const QStringList kAdoptableImages = {
    QStringLiteral("*.fits"), QStringLiteral("*.fit"),  QStringLiteral("*.fts"),
    QStringLiteral("*.fz"),   QStringLiteral("*.xisf"), QStringLiteral("*.png"),
    QStringLiteral("*.jpg"),  QStringLiteral("*.jpeg"), QStringLiteral("*.tiff"),
    QStringLiteral("*.tif"),  QStringLiteral("*.webp") };

void keepAllImagesClickable(QFileDialog& dlg) {
    if (auto* model = dlg.findChild<QFileSystemModel*>())
        model->setNameFilters(kAdoptableImages);
}

// Clicking a file adopts its BASE NAME only — the extension belongs to the
// selected format ("some_nebula.xisf" clicked in a PNG export prefills
// "some_nebula", saved as "some_nebula.png"; with matching format the
// original name reassembles exactly, for overwriting).
void adoptBasenameOnClick(QFileDialog& dlg) {
    QObject::connect(&dlg, &QFileDialog::currentChanged, &dlg,
                     [&dlg](const QString& p) {
        const QFileInfo fi(p);
        if (!p.isEmpty() && !fi.isDir() && !fi.completeBaseName().isEmpty())
            dlg.selectFile(fi.completeBaseName());
    });
}

// The typed/clicked name keeps its extension when this dialog can WRITE that
// format; any other extension is swapped for the selected filter's (clicking
// "M81.xisf" in the PNG export prefills "M81" and saves "M81.png").
QString normalizeSuffix(const QString& path, const QStringList& writable,
                        const QString& fallback) {
    const QFileInfo fi(path);
    if (writable.contains(fi.suffix().toLower())) return path;
    const QString base = fi.suffix().isEmpty()
        ? fi.filePath() : fi.filePath().left(fi.filePath().size() - fi.suffix().size() - 1);
    return base + QLatin1Char('.') + fallback;
}
} // namespace

QString MainWindow::exportImageDialogPath(const QString& title, bool offer16,
                                          bool* want16, int* quality) {
#ifdef Q_OS_MACOS
    // Native NSSavePanel with the same options as an accessory view; the Qt
    // dialog below stays as the non-cocoa (and offscreen) fallback.
    if (mac::savePanelAvailable()) {
        mac::SavePanelSpec spec;
        spec.title       = title;
        spec.directory   = openDialogDir();
        spec.formatLabel = tr("Format:");
        spec.formats = {
            { QStringLiteral("PNG"),  { QStringLiteral("png") },  offer16, false },
            { QStringLiteral("JPEG"), { QStringLiteral("jpg"),
                                        QStringLiteral("jpeg") }, false,   true  },
            { QStringLiteral("TIFF"), { QStringLiteral("tiff"),
                                        QStringLiteral("tif") },  offer16, false },
            { QStringLiteral("WebP"), { QStringLiteral("webp") }, false,   true  },
        };
        spec.formatIndex = 0;
        spec.popupLabel  = tr("Pixel depth:");
        spec.popupItems  = { tr("8-bit per channel"), tr("16-bit per channel") };
        spec.popupIndex  = s_expDepth;
        spec.sliderLabel = tr("Quality (1–100):");
        spec.sliderValue = s_expQuality;
        for (const QString& g : kAdoptableImages)
            spec.clickableSuffixes << g.mid(2);              // "*.fits" -> "fits"
        const mac::SavePanelResult r = mac::runSavePanel(spec);
        if (!r.accepted || r.path.isEmpty()) return {};
        static const QStringList kW{ "png", "jpg", "jpeg", "tiff", "tif", "webp" };
        const QString path = normalizeSuffix(r.path, kW,
            spec.formats[qBound(0, r.formatIndex, int(spec.formats.size()) - 1)]
                .suffixes.first());
        const QString ext = QFileInfo(path).suffix().toLower();
        s_expDepth   = r.popupIndex;
        s_expQuality = r.sliderValue;
        *want16 = offer16 && s_expDepth == 1 &&
                  (ext == QLatin1String("png") || ext == QLatin1String("tiff") ||
                   ext == QLatin1String("tif"));
        *quality = (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg") ||
                    ext == QLatin1String("webp")) ? s_expQuality : -1;
        return path;
    }
#endif
    QFileDialog dlg(this, title, openDialogDir(),
        tr("PNG (*.png);;JPEG (*.jpg);;TIFF (*.tiff);;WebP (*.webp)"));
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);

    auto* row = new QWidget(&dlg);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto* depthLabel = new QLabel(tr("Pixel depth:"), row);
    auto* depthCombo = new QComboBox(row);
    depthCombo->addItems({ tr("8-bit per channel"), tr("16-bit per channel") });
    depthCombo->setCurrentIndex(s_expDepth);
    auto* qualLabel = new QLabel(tr("Quality (1–100):"), row);
    auto* qualSpin = new QSpinBox(row);
    qualSpin->setRange(1, 100);
    qualSpin->setValue(s_expQuality);
    h->addWidget(depthLabel); h->addWidget(depthCombo);
    h->addSpacing(18);
    h->addWidget(qualLabel);  h->addWidget(qualSpin);
    h->addStretch();
    addOptionRow(dlg, row);

    auto sync = [&dlg, offer16, depthLabel, depthCombo, qualLabel, qualSpin](const QString& f) {
        const QString ext = filterSuffix(f);
        const bool hasQual  = ext == QLatin1String("jpg") || ext == QLatin1String("webp");
        const bool hasDepth = offer16 &&
            (ext == QLatin1String("png") || ext == QLatin1String("tiff"));
        depthLabel->setEnabled(hasDepth); depthCombo->setEnabled(hasDepth);
        qualLabel->setEnabled(hasQual);   qualSpin->setEnabled(hasQual);
        dlg.setDefaultSuffix(ext);
        keepAllImagesClickable(dlg);
    };
    QObject::connect(&dlg, &QFileDialog::filterSelected, &dlg, sync);
    sync(dlg.selectedNameFilter());
    adoptBasenameOnClick(dlg);

    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) return {};
    static const QStringList kWritable{ "png", "jpg", "jpeg", "tiff", "tif", "webp" };
    const QString path = normalizeSuffix(dlg.selectedFiles().first(),
                                         kWritable, dlg.defaultSuffix());
    // Options follow the FINAL suffix (a clicked .jpg under the PNG filter is
    // honoured as JPEG), the enable-state above being only a UI hint.
    const QString ext = QFileInfo(path).suffix().toLower();
    s_expDepth = depthCombo->currentIndex();
    s_expQuality = qualSpin->value();
    *want16 = offer16 && s_expDepth == 1 &&
              (ext == QLatin1String("png") || ext == QLatin1String("tiff") ||
               ext == QLatin1String("tif"));
    *quality = (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg") ||
                ext == QLatin1String("webp")) ? s_expQuality : -1;
    return path;
}

QString MainWindow::dataSaveDialogPath(const QString& title, io::SaveOptions& opts) {
    auto tlh = [](const char* s) {
        return QCoreApplication::translate("astro::MainWindowHelpers", s);
    };
#ifdef Q_OS_MACOS
    if (mac::savePanelAvailable()) {
        mac::SavePanelSpec spec;
        spec.title       = title;
        spec.directory   = openDialogDir();
        spec.formatLabel = tr("Format:");
        spec.formats = {
            { QStringLiteral("FITS"), { QStringLiteral("fits"), QStringLiteral("fit"),
                                        QStringLiteral("fts") },  false, false },
            { QStringLiteral("XISF"), { QStringLiteral("xisf") }, true,  false },
            { tr("TIFF 16-bit"),      { QStringLiteral("tiff"),
                                        QStringLiteral("tif") },  false, false },
        };
        spec.formatIndex = 0;
        spec.popupLabel  = tlh("Data-block compression:");
        spec.popupItems  = { tlh("Zstd (smallest)"), tlh("Zlib (widest compatibility)"),
                             tlh("Uncompressed") };
        spec.popupIndex  = s_xisfComp;
        for (const QString& g : kAdoptableImages)
            spec.clickableSuffixes << g.mid(2);
        const mac::SavePanelResult r = mac::runSavePanel(spec);
        if (!r.accepted || r.path.isEmpty()) return {};
        static const QStringList kW{ "fits", "fit", "fts", "xisf", "tiff", "tif" };
        const QString path = normalizeSuffix(r.path, kW,
            spec.formats[qBound(0, r.formatIndex, int(spec.formats.size()) - 1)]
                .suffixes.first());
        s_xisfComp = r.popupIndex;
        using C = io::SaveOptions::Compression;
        opts.xisfCompression = s_xisfComp == 0 ? C::Zstd
                             : s_xisfComp == 1 ? C::Zlib : C::None;
        return path;
    }
#endif
    QFileDialog dlg(this, title, openDialogDir(),
        tr("FITS (*.fits *.fit *.fts);;XISF (*.xisf);;TIFF 16-bit (*.tiff *.tif)"));
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);

    auto tl = [](const char* s) {
        return QCoreApplication::translate("astro::MainWindowHelpers", s);
    };
    auto* row = new QWidget(&dlg);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto* compLabel = new QLabel(tl("Data-block compression:"), row);
    auto* compCombo = new QComboBox(row);
    compCombo->addItems({ tl("Zstd (smallest)"), tl("Zlib (widest compatibility)"),
                          tl("Uncompressed") });
    compCombo->setCurrentIndex(s_xisfComp);
    h->addWidget(compLabel); h->addWidget(compCombo);
    h->addStretch();
    addOptionRow(dlg, row);

    auto sync = [&dlg, compLabel, compCombo](const QString& f) {
        const QString ext = filterSuffix(f);
        const bool isXisf = ext == QLatin1String("xisf");
        compLabel->setEnabled(isXisf); compCombo->setEnabled(isXisf);
        dlg.setDefaultSuffix(ext);
        keepAllImagesClickable(dlg);
    };
    QObject::connect(&dlg, &QFileDialog::filterSelected, &dlg, sync);
    sync(dlg.selectedNameFilter());
    adoptBasenameOnClick(dlg);

    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) return {};
    static const QStringList kWritable{ "fits", "fit", "fts", "xisf", "tiff", "tif" };
    const QString path = normalizeSuffix(dlg.selectedFiles().first(),
                                         kWritable, dlg.defaultSuffix());
    s_xisfComp = compCombo->currentIndex();
    using C = io::SaveOptions::Compression;
    opts.xisfCompression = s_xisfComp == 0 ? C::Zstd
                         : s_xisfComp == 1 ? C::Zlib : C::None;
    return path;
}

// Save the CURRENT VIEW's non-linear edit as data: the stretch (window +
// transfer + colormap for mono) is baked into Float32 [0,1] pixels at full
// precision — unlike Export View As…, which quantises to 8-bit for pictures.
void MainWindow::saveStretched() {
    if (!m_image.isValid()) return;
    io::SaveOptions opts;
    const QString path = dataSaveDialogPath(tr("Save stretched image"), opts);
    if (path.isEmpty()) return;
    ImageData baked = DisplayRenderer::renderFloat(m_image, m_model);
    if (!baked.isValid()) { QMessageBox::warning(this, tr("Save failed"), tr("Could not bake the stretch.")); return; }
    ImageHeader hdr = m_header;
    hdr.cards.push_back({ QStringLiteral("HISTORY"),
                          QStringLiteral("NebulaScope: baked display stretch"), QString() });
    io::SaveResult sr = io::saveImage(path, baked, hdr, opts);
    if (!sr.ok) { QMessageBox::warning(this, tr("Save failed"), sr.error); return; }
    statusBar()->showMessage(tr("Saved stretched %1").arg(QFileInfo(path).fileName()), 3000);
    // For an in-memory result (combine/crop) this file is its only disk
    // identity — the list entry takes the saved name, as with Save Data As.
    rebrandSyntheticAfterSave(path);
}

void MainWindow::saveFile() {
    if (!m_image.isValid()) return;
    io::SaveOptions opts;
    const QString path = dataSaveDialogPath(tr("Save image"), opts);
    if (path.isEmpty()) return;
    io::SaveResult sr = io::saveImage(path, m_image, m_header, opts);
    if (!sr.ok) { QMessageBox::warning(this, tr("Save failed"), sr.error); return; }
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 3000);
    rebrandSyntheticAfterSave(path);
}

// A synthetic (in-memory) image that was just written to disk becomes that
// file: rebrand its list row and migrate per-image state to the new key, so
// the entry's identifier is the saved name from here on. Shared by Save Data
// As, Save Stretched As, and the script `save` command; no-op for images
// that already live on disk.
void MainWindow::rebrandSyntheticAfterSave(const QString& savedPath) {
    if (!m_currentPath.startsWith(QLatin1String("mem://"))) return;
    const QString path = QFileInfo(savedPath).absoluteFilePath();  // keys are absolute
    const QString oldKey = m_currentPath;
    for (int i = 0; i < m_fileList->count(); ++i) {
        QListWidgetItem* it = m_fileList->item(i);
        if (it->data(Qt::UserRole).toString() != oldKey) continue;
        it->setData(Qt::UserRole, path);
        it->setText(QFileInfo(path).fileName());
        it->setToolTip(path);
        break;
    }
    if (m_stfByPath.contains(oldKey))      m_stfByPath.insert(path, m_stfByPath.take(oldKey));
    if (m_annByPath.contains(oldKey))      m_annByPath.insert(path, m_annByPath.take(oldKey));
    if (m_annDirty.remove(oldKey))         m_annDirty.insert(path);
    if (m_xformByPath.contains(oldKey))    m_xformByPath.insert(path, m_xformByPath.take(oldKey));
    if (m_diskSizeByPath.contains(oldKey)) m_diskSizeByPath.insert(path, m_diskSizeByPath.take(oldKey));
    m_synthetic.remove(oldKey);            // future loads come from the file
    m_currentPath = path;
    syncFileWatcher();                     // auto-reload watches the new file
    rememberRecent(QStringLiteral("recentImages"), path, Preferences::get().recentImagesMax);
}

// Float [0,1] render -> 16-bit-per-channel QImage (for 16-bit PNG/TIFF export).
static QImage floatToRgb64(const ImageData& f) {
    if (!f.isValid()) return QImage();
    const int w = f.width(), h = f.height();
    QImage out(w, h, QImage::Format_RGBX64);
    const float* p0 = f.plane<float>(0);
    const float* p1 = f.channels() >= 3 ? f.plane<float>(1) : p0;
    const float* p2 = f.channels() >= 3 ? f.plane<float>(2) : p0;
    auto q16 = [](float v) -> quint16 {
        const float c = v < 0 ? 0.0f : (v > 1 ? 1.0f : v);
        return quint16(c * 65535.0f + 0.5f);
    };
    for (int y = 0; y < h; ++y) {
        quint16* row = reinterpret_cast<quint16*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + x;
            row[x * 4 + 0] = q16(p0[i]);
            row[x * 4 + 1] = q16(p1[i]);
            row[x * 4 + 2] = q16(p2[i]);
            row[x * 4 + 3] = 65535;
        }
    }
    return out;
}

void MainWindow::exportView() {
    if (!m_image.isValid()) return;
    // The exact 8-bit RGB image currently on screen — stretch, colormap and all.
    saveRenderedImage(DisplayRenderer::render(m_image, m_model), tr("Export view (full frame)"),
        [this] {
            QImage f16 = floatToRgb64(DisplayRenderer::renderFloat(m_image, m_model));
            if (m_hasIcc) f16.applyColorTransform(m_iccToSrgb);
            if (m_hasIcc) f16.applyColorTransform(m_iccToSrgb);
            return f16;
        });
}

// ---- Tools > Measure PSF (Stars) -------------------------------------------
// The PSF study's stellar instrument (docs/PSF-STUDY.md), in-app: elliptical
// Moffat fits over the frame's isolated stars, per channel, reported with a
// 3x4 field map. A uniform elongation axis across the map is one-axis drift
// (guiding/flexure); an axis rotating toward the corners is optics. Runs in a
// worker; the report dialog can drop ellipse annotations on a sample of the
// fitted stars so the numbers can be seen on the image.

static QString psfReportText(const std::vector<PsfChannelReport>& reps,
                             double asecPerPx) {
    QString out;
    const char* chName[3] = { "ch0", "ch1", "ch2" };
    for (std::size_t c = 0; c < reps.size(); ++c) {
        const PsfChannelReport& r = reps[c];
        out += QStringLiteral("[%1] ").arg(QLatin1String(chName[std::min<std::size_t>(c, 2)]));
        if (r.nFitted < 5) {
            out += QObject::tr("only %1 usable stars — no measurement\n").arg(r.nFitted);
            continue;
        }
        auto px = [&](double v) {
            return asecPerPx > 0
                ? QStringLiteral("%1 px = %2\"").arg(v, 0, 'f', 2).arg(v * asecPerPx, 0, 'f', 2)
                : QStringLiteral("%1 px").arg(v, 0, 'f', 2);
        };
        out += QObject::tr("%1 stars | FWHM maj %2, min %3 (geo %4) | ecc %5 @ PA %6° | beta %7\n")
                   .arg(r.nFitted)
                   .arg(px(r.fwhmMaj), px(r.fwhmMin), px(r.fwhmGeo))
                   .arg(r.ecc, 0, 'f', 2)
                   .arg(r.paDeg, 0, 'f', 0)
                   .arg(r.beta, 0, 'f', 2);
        out += QObject::tr("    field map (FWHM px / ecc / PA):\n");
        for (int zr = 0; zr < 3; ++zr) {
            out += QStringLiteral("    ");
            for (int zc = 0; zc < 4; ++zc) {
                const PsfZone& z = r.zone[zr][zc];
                out += z.nStars >= 5
                    ? QStringLiteral("%1/%2/%3  ")
                          .arg(z.fwhmGeo, 5, 'f', 2).arg(z.ecc, 4, 'f', 2).arg(z.paDeg, 4, 'f', 0)
                    : QStringLiteral("     --        ");
            }
            out += QLatin1Char('\n');
        }
    }
    return out;
}

bool MainWindow::psfCacheValid() const {
    const auto hit = m_psfCache.constFind(m_currentPath);
    if (hit == m_psfCache.constEnd()) return false;
    const QFileInfo fi(splitHduBase(m_currentPath));
    return hit->mtime == fi.lastModified() && hit->fsize == fi.size() &&
           hit->xformOps == m_xformByPath.value(m_currentPath) &&
           hit->debayerMode == m_debayerByPath.value(m_currentPath, 0);
}

void MainWindow::measurePsfAction() {
    if (!m_image.isValid()) return;
    // Result cache: the 2 700-fit computation is redone only when the image
    // actually changed — on disk, by rotation, or by debayer mode. Otherwise
    // the menu action simply reopens the report.
    auto show = [this] {
        if (m_scriptDriving) {
            fprintf(stderr, "%s", psfReportText(m_lastPsf,
                    m_wcs.valid() ? m_wcs.pixelScaleArcsec() : 0.0).toUtf8().constData());
            statusBar()->showMessage(tr("PSF measured — %1 channel(s)").arg(m_lastPsf.size()), 4000);
        } else {
            showPsfReport();
        }
    };
    if (psfCacheValid()) {
        m_lastPsf = m_psfCache.value(m_currentPath).reports;
        m_lastPsfPath = m_currentPath;
        show();
        return;
    }
    runPsfMeasurement(show);
}

void MainWindow::runPsfMeasurement(std::function<void()> whenDone) {
    const QString path = m_currentPath;
    const QFileInfo fi(splitHduBase(path));
    const QStringList ops = m_xformByPath.value(path);
    const int dmode = m_debayerByPath.value(path, 0);
    const ImageData img = m_image;                 // deep copy for the worker
    const int nch = std::min(3, img.channels());
    auto store = [this, path, fi, ops, dmode](std::vector<PsfChannelReport> reps) {
        m_lastPsf = std::move(reps);
        m_lastPsfPath = path;
        PsfCacheEntry ce;
        ce.reports = m_lastPsf;
        ce.mtime = fi.lastModified();
        ce.fsize = fi.size();
        ce.xformOps = ops;
        ce.debayerMode = dmode;
        m_psfCache.insert(path, std::move(ce));
    };
    if (m_scriptDriving) {                          // synchronous
        std::vector<PsfChannelReport> reps;
        for (int c = 0; c < nch; ++c) reps.push_back(measurePsf(img, c));
        store(std::move(reps));
        if (whenDone) whenDone();
        return;
    }
    // Live progress: the worker ticks shared atomics (stars fitted / total,
    // cumulative over channels); a timer paints them into a status-bar
    // progress bar. Indeterminate while detection runs (total still 0).
    // The bar spans the WHOLE job: each channel owns an equal slice, filled
    // by that channel's own fit count — (channel + done/total) / nch is
    // monotone by construction (a per-run cumulative total made the bar
    // complete and rewind once per channel, since a channel's total is only
    // known after its detection).
    auto done = std::make_shared<std::atomic<int>>(0);
    auto total = std::make_shared<std::atomic<int>>(0);
    auto chanIx = std::make_shared<std::atomic<int>>(0);
    auto computeP = [img, nch, done, total, chanIx]() {
        std::vector<PsfChannelReport> reps;
        for (int c = 0; c < nch; ++c) {
            chanIx->store(c);
            total->store(0);
            done->store(0);
            reps.push_back(measurePsf(img, c, done.get(), total.get()));
        }
        return reps;
    };
    statusBar()->showMessage(tr("Measuring PSF — channel 1/%1: detecting stars…").arg(nch));
    auto* bar = new QProgressBar();
    bar->setMaximumWidth(220);
    bar->setRange(0, 1000);
    bar->setValue(0);
    statusBar()->addPermanentWidget(bar);
    auto* tick = new QTimer(this);
    connect(tick, &QTimer::timeout, this, [this, bar, done, total, chanIx, nch] {
        const int ci = chanIx->load();
        const int t = total->load(), d = done->load();
        const double frac = (ci + (t > 0 ? double(d) / t : 0.0)) / nch;
        bar->setValue(int(frac * 1000));
        statusBar()->showMessage(t > 0
            ? tr("Measuring PSF — channel %1/%2: %3 / %4 stars").arg(ci + 1).arg(nch).arg(d).arg(t)
            : tr("Measuring PSF — channel %1/%2: detecting stars…").arg(ci + 1).arg(nch));
    });
    tick->start(150);
    auto* watcher = new QFutureWatcher<std::vector<PsfChannelReport>>(this);
    connect(watcher, &QFutureWatcher<std::vector<PsfChannelReport>>::finished, this,
            [this, watcher, store, bar, tick, whenDone] {
                tick->stop();
                tick->deleteLater();
                statusBar()->removeWidget(bar);
                bar->deleteLater();
                watcher->deleteLater();
                store(watcher->result());
                statusBar()->clearMessage();
                if (whenDone) whenDone();
            });
    watcher->setFuture(QtConcurrent::run(computeP));
}

void MainWindow::showPsfReport() {
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("PSF measurement"));
    auto* lay = new QVBoxLayout(dlg);
    auto* text = new QTextEdit();
    text->setReadOnly(true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(mono.pointSizeF() - 1);
    text->setFont(mono);
    const double asec = m_wcs.valid() ? m_wcs.pixelScaleArcsec() : 0.0;
    text->setPlainText(psfReportText(m_lastPsf, asec)
        + (asec > 0 ? tr("\nplate scale %1\"/px (from the plate solution)").arg(asec, 0, 'f', 4)
                    : tr("\nno plate solution — pixels only")));
    text->setMinimumSize(560, 300);
    lay->addWidget(text);
    auto* row = new QHBoxLayout();
    QComboBox* chan = nullptr;
    if (m_lastPsf.size() > 1) {
        chan = new QComboBox();
        for (std::size_t c = 0; c < m_lastPsf.size(); ++c)
            chan->addItem(tr("channel %1").arg(c));
        row->addWidget(chan);
    }
    auto* count = new QSpinBox();
    count->setRange(5, 500);
    count->setValue(60);
    row->addWidget(new QLabel(tr("stars:")));
    row->addWidget(count);
    auto* labelMode = new QComboBox();
    labelMode->addItem(tr("no label"));
    labelMode->addItem(tr("FWHM"));
    labelMode->addItem(tr("eccentricity"));
    labelMode->addItem(tr("FWHM + ecc"));
    row->addWidget(labelMode);
    auto* annBtn = new QPushButton(tr("Annotate stars"));
    annBtn->setToolTip(tr("Drop rotated-ellipse annotations on the brightest fitted stars\n"
                          "(axes proportional to the fitted FWHM, angle = the fitted PA) —\n"
                          "the elongation pattern becomes visible on the image, optionally\n"
                          "with each star's numbers. One undo step."));
    connect(annBtn, &QPushButton::clicked, this, [this, chan, count, labelMode] {
        annotatePsfStars(chan ? chan->currentIndex() : 0, count->value(),
                         labelMode->currentIndex());
    });
    row->addWidget(annBtn);
    row->addStretch();
    auto* closeBtn = new QPushButton(tr("Close"));
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);
    row->addWidget(closeBtn);
    lay->addLayout(row);
    dlg->show();
}

void MainWindow::annotatePsfStars(int channel, int countN, int labelMode) {
    if (m_lastPsfPath != m_currentPath || m_lastPsf.empty()) {
        statusBar()->showMessage(tr("PSF results belong to another image — measure again"), 5000);
        return;
    }
    channel = std::min<int>(channel, int(m_lastPsf.size()) - 1);
    const auto& stars = m_lastPsf[channel].stars;
    if (stars.empty()) return;
    std::vector<Annotation> before = m_annByPath.value(m_currentPath);
    const int n = std::min<int>(countN, int(stars.size()));
    for (int i = 0; i < n; ++i) {
        const PsfStar& st = stars[i];
        Annotation an;
        an.type = Annotation::Type::Ellipse;
        an.x = st.x; an.y = st.y;
        an.a = 2.5 * st.fwhmMaj;               // semi-axes scaled for visibility;
        an.b = 2.5 * st.fwhmMin;               // the maj:min RATIO is preserved
        an.angleDeg = st.paDeg;
        an.color = QColor(255, 209, 102);      // warm gold — not a channel colour
        // Optional per-star numbers: FWHM (geometric mean, arcsec when a
        // plate solution exists) and/or eccentricity.
        if (labelMode > 0) {
            const double geo = std::sqrt(st.fwhmMaj * st.fwhmMin);
            const double asec = m_wcs.valid() ? m_wcs.pixelScaleArcsec() : 0.0;
            const QString fw = asec > 0
                ? QStringLiteral("%1\u2033").arg(geo * asec, 0, 'f', 2)
                : QStringLiteral("%1 px").arg(geo, 0, 'f', 2);
            const QString ec = QStringLiteral("e%1").arg(st.ecc, 0, 'f', 2);
            an.label = labelMode == 1 ? fw
                     : labelMode == 2 ? ec
                     : fw + QStringLiteral(" \u00b7 ") + ec;
            an.textSize = 9;
        }
        m_annByPath[m_currentPath].push_back(an);
    }
    m_annDirty.insert(m_currentPath);
    refreshAnnotations();
    pushAnnotationEdit(tr("annotate PSF stars"), m_currentPath, std::move(before));
    statusBar()->showMessage(tr("%1 fitted stars annotated (ellipse = fitted shape ×2.5)").arg(n), 5000);
}

// ---- deconvolution to a declared target PSF ---------------------------------
//
// The in-app port of the PSF study's full_deconv instrument (docs/PSF-STUDY.md,
// core/Deconvolve.cpp): measured Moffat kernel in, declared circular Gaussian
// out, via the MCS one-filter transform. Chains onto Measure PSF for the
// kernel; verifies its own delivery by re-fitting stars on the result.

void MainWindow::deconvolveAction() {
    if (!m_image.isValid()) return;
    if (!psfCacheValid()) {
        // The kernel IS the measurement — run it first, then come back here.
        statusBar()->showMessage(tr("Measuring the PSF first — it is the deconvolution kernel…"));
        runPsfMeasurement([this] { deconvolveAction(); });
        return;
    }
    m_lastPsf = m_psfCache.value(m_currentPath).reports;
    m_lastPsfPath = m_currentPath;
    const int nch = std::min<int>(int(m_lastPsf.size()), m_image.channels());
    if (nch < 1 || m_lastPsf[0].nFitted < 10) {
        statusBar()->showMessage(tr("Too few fitted stars to define a kernel"), 5000);
        return;
    }
    const double asec = m_wcs.valid() ? m_wcs.pixelScaleArcsec() : 0.0;
    double minGeoPx = 1e30;
    QString measured;
    for (int c = 0; c < nch; ++c) {
        const PsfChannelReport& r = m_lastPsf[c];
        if (r.nFitted >= 10) minGeoPx = std::min(minGeoPx, r.fwhmGeo);
        measured += tr("channel %1: FWHM %2 × %3, PA %4°, β %5  (%6 stars)\n")
            .arg(c)
            .arg(asec > 0 ? QStringLiteral("%1″").arg(r.fwhmMaj * asec, 0, 'f', 2)
                          : QStringLiteral("%1 px").arg(r.fwhmMaj, 0, 'f', 2))
            .arg(asec > 0 ? QStringLiteral("%1″").arg(r.fwhmMin * asec, 0, 'f', 2)
                          : QStringLiteral("%1 px").arg(r.fwhmMin, 0, 'f', 2))
            .arg(r.paDeg, 0, 'f', 0).arg(r.beta, 0, 'f', 1).arg(r.nFitted);
    }
    if (minGeoPx > 1e29) return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Deconvolve to target PSF"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* info = new QLabel(tr("Measured stellar PSF (the kernel):\n%1\n"
        "The result is a NEW list entry — the linear data, deconvolved by the\n"
        "measured elliptical Moffat and reconvolved to a round Gaussian of the\n"
        "declared width (MCS single-filter transform). Meaningful on LINEAR\n"
        "data. The delivered PSF is verified by re-fitting the result's stars.")
        .arg(measured));
    lay->addWidget(info);
    auto* form = new QFormLayout();
    auto* target = new QDoubleSpinBox();
    target->setDecimals(2);
    if (asec > 0) {
        target->setRange(0.10, 30.0);
        target->setSuffix(QStringLiteral("″"));
        target->setValue(0.75 * minGeoPx * asec);
    } else {
        target->setRange(0.5, 50.0);
        target->setSuffix(tr(" px"));
        target->setValue(0.75 * minGeoPx);
    }
    target->setToolTip(tr("The declared FWHM of the result's round Gaussian PSF.\n"
                          "About 25% below the measured width is reliably reachable;\n"
                          "more aggressive targets need the regularization to keep up\n"
                          "(watch the delivered figure)."));
    form->addRow(tr("Target FWHM:"), target);
    auto* lam = new QComboBox();
    lam->addItem(tr("automatic (largest honouring the target ±5%)"), 0.0);
    for (double v : { 3e-3, 1e-3, 3e-4, 1e-4, 3e-5 })
        lam->addItem(QStringLiteral("%1").arg(v, 0, 'e', 0), v);
    lam->setToolTip(tr("MCS regularization λ. Automatic walks a descending ladder and\n"
                       "keeps the LARGEST λ whose delivered FWHM (measured on a central\n"
                       "crop) honours the declaration — contract-first, per channel."));
    form->addRow(tr("Regularization:"), lam);
    auto* protect = new QCheckBox(tr("Protect saturated cores (keep input pixels, feathered)"));
    protect->setChecked(true);
    protect->setToolTip(tr("Clipped stellar cores are nonlinear — no longer truth convolved\n"
                           "with the PSF — so deconvolving them rings. The brightest 0.005%\n"
                           "of pixels keep their input values."));
    lay->addLayout(form);
    lay->addWidget(protect);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    const double targetPx = asec > 0 ? target->value() / asec : target->value();
    runDeconvolution(targetPx, lam->currentData().toDouble(), protect->isChecked());
}

void MainWindow::runDeconvolution(double targetFwhmPx, double lambda, bool protectCores) {
    const QString path = m_currentPath;
    if (m_lastPsfPath != path || m_lastPsf.empty()) return;
    const ImageData img = m_image;                     // deep copy for the worker
    const int nch = std::min<int>(int(m_lastPsf.size()), img.channels());
    std::vector<DeconvChannelPsf> psfs;
    for (int c = 0; c < nch; ++c) {
        const PsfChannelReport& r = m_lastPsf[c];
        DeconvChannelPsf p;
        p.fwhmMajPx = r.fwhmMaj; p.fwhmMinPx = r.fwhmMin;
        p.paDeg = r.paDeg; p.beta = r.beta;
        psfs.push_back(p);
    }

    struct DeconvRun {
        ImageData out;
        std::vector<double> lambdas, deliveredGeo;     // per channel; 0 = unmeasurable
    };
    auto steps = std::make_shared<std::atomic<int>>(0);   // 4 per channel (filter pass)
    auto chanIx = std::make_shared<std::atomic<int>>(0);
    auto stage = std::make_shared<std::atomic<int>>(0);   // 0 calibrating λ, 1 filtering
    auto compute = [img, psfs, nch, targetFwhmPx, lambda, protectCores,
                    steps, chanIx, stage]() {
        DeconvRun res;
        res.out = img;
        // Delivered-PSF verification on a centred crop of the result — the
        // same audit the study ran on the full frame.
        auto deliveredOn = [&](int c) {
            const int cw = std::min(1536, img.width()), chh = std::min(1536, img.height());
            const int x0 = (img.width() - cw) / 2, y0 = (img.height() - chh) / 2;
            ImageData crop(cw, chh, 1, SampleFormat::Float32, ColorSpace::Gray);
            const float* src = res.out.plane<float>(c);
            float* dst = crop.plane<float>(0);
            for (int y = 0; y < chh; ++y)
                std::copy(src + std::size_t(y0 + y) * img.width() + x0,
                          src + std::size_t(y0 + y) * img.width() + x0 + cw,
                          dst + std::size_t(y) * cw);
            const PsfChannelReport rep = measurePsf(crop, 0);
            return rep.nFitted >= 10 ? rep.fwhmGeo : 0.0;
        };
        for (int c = 0; c < nch; ++c) {
            chanIx->store(c);
            DeconvOptions opt;
            opt.targetFwhmPx = targetFwhmPx;
            opt.protectCores = protectCores;
            stage->store(0);
            opt.lambda = lambda > 0.0
                ? lambda
                : selectLambda(img, c, psfs[std::size_t(c)], targetFwhmPx);
            stage->store(1);
            std::vector<float> plane = deconvolveChannel(
                img.plane<float>(c), img.width(), img.height(),
                psfs[std::size_t(c)], opt, steps.get());
            std::copy(plane.begin(), plane.end(), res.out.plane<float>(c));
            res.lambdas.push_back(opt.lambda);
            res.deliveredGeo.push_back(deliveredOn(c));
        }
        return res;
    };

    // Everything the completion needs is captured NOW: by the time the worker
    // finishes, the user may have moved to another image and m_header /
    // m_lastPsf / the stretch state would belong to it.
    const double asec = m_wcs.valid() ? m_wcs.pixelScaleArcsec() : 0.0;
    const ImageHeader srcHdr = m_header;
    const std::vector<PsfChannelReport> reports = m_lastPsf;
    const StretchModel::State st = m_model.state();
    const std::vector<Annotation> srcAnns = m_annByPath.value(path);
    auto finish = [this, path, targetFwhmPx, protectCores, asec, nch,
                   srcHdr, reports, st, srcAnns](DeconvRun res) {
        auto fw = [asec](double px) {
            return asec > 0 ? QStringLiteral("%1″").arg(px * asec, 0, 'f', 2)
                            : QStringLiteral("%1 px").arg(px, 0, 'f', 2);
        };
        // Header: geometry (and the plate solution with it) is unchanged; the
        // model goes on record in the structure lines — every output pixel a
        // stated linear functional of the input.
        ImageHeader hdr = srcHdr;
        hdr.container = "In-memory";
        QStringList lines;
        lines << tr("Deconvolved to target PSF %1 (round Gaussian) · MCS single-filter transform%2")
                     .arg(fw(targetFwhmPx))
                     .arg(protectCores ? tr(" · saturated cores protected") : QString());
        QString delivered;
        for (int c = 0; c < nch; ++c) {
            const PsfChannelReport& r = reports[std::size_t(c)];
            lines << tr("channel %1: kernel Moffat %2 × %3 @ PA %4°, β %5 · λ %6 · delivered %7")
                         .arg(c).arg(fw(r.fwhmMaj), fw(r.fwhmMin))
                         .arg(r.paDeg, 0, 'f', 0).arg(r.beta, 0, 'f', 1)
                         .arg(res.lambdas[std::size_t(c)], 0, 'e', 0)
                         .arg(res.deliveredGeo[std::size_t(c)] > 0
                                  ? fw(res.deliveredGeo[std::size_t(c)]) : tr("(too few stars)"));
            if (res.deliveredGeo[std::size_t(c)] > 0)
                delivered += (delivered.isEmpty() ? QString() : QStringLiteral(" / "))
                           + fw(res.deliveredGeo[std::size_t(c)]);
        }
        hdr.structure = lines;
        const QString key = addSyntheticImage(
            QFileInfo(path).completeBaseName() + QStringLiteral("_deconv"),
            std::move(res.out));
        m_syntheticHeaders.insert(key, hdr);
        if (!srcAnns.empty()) m_annByPath.insert(key, srcAnns);   // geometry unchanged
        m_stfByPath.insert(key, st);
        displayPath(key);
        const QString msg = tr("Deconvolved to %1 — delivered %2 — Save Data As… keeps it")
            .arg(fw(targetFwhmPx), delivered.isEmpty() ? tr("(unverified)") : delivered);
        if (m_scriptDriving)
            fprintf(stderr, "%s\n", msg.toUtf8().constData());
        statusBar()->showMessage(msg, 8000);
    };

    if (m_scriptDriving) {                              // synchronous
        finish(compute());
        return;
    }
    statusBar()->showMessage(tr("Deconvolving — channel 1/%1: calibrating regularization…").arg(nch));
    auto* bar = new QProgressBar();
    bar->setMaximumWidth(220);
    bar->setRange(0, nch * 4);
    bar->setValue(0);
    statusBar()->addPermanentWidget(bar);
    auto* tick = new QTimer(this);
    connect(tick, &QTimer::timeout, this, [this, bar, steps, chanIx, stage, nch] {
        bar->setValue(steps->load());
        const int ci = chanIx->load();
        statusBar()->showMessage(stage->load() == 0
            ? tr("Deconvolving — channel %1/%2: calibrating regularization…").arg(ci + 1).arg(nch)
            : tr("Deconvolving — channel %1/%2: filtering…").arg(ci + 1).arg(nch));
    });
    tick->start(150);
    auto* watcher = new QFutureWatcher<DeconvRun>(this);
    connect(watcher, &QFutureWatcher<DeconvRun>::finished, this,
            [this, watcher, finish, bar, tick] {
                tick->stop();
                tick->deleteLater();
                statusBar()->removeWidget(bar);
                bar->deleteLater();
                watcher->deleteLater();
                finish(watcher->result());
            });
    watcher->setFuture(QtConcurrent::run(compute));
}

void MainWindow::scriptDeconvolve(double fwhmPx, double lambda) {
    if (!m_image.isValid()) return;
    if (!psfCacheValid()) {
        runPsfMeasurement({});                          // synchronous when scripted
    } else {
        m_lastPsf = m_psfCache.value(m_currentPath).reports;
        m_lastPsfPath = m_currentPath;
    }
    if (m_lastPsf.empty() || m_lastPsf[0].nFitted < 10) {
        fprintf(stderr, "deconv: too few fitted stars to define a kernel\n");
        return;
    }
    runDeconvolution(fwhmPx, lambda, true);
}

void MainWindow::exportRegion() {

    if (!m_image.isValid()) return;
    const QRect roi = m_view->visibleImageRect();
    if (roi.isEmpty()) {
        QMessageBox::information(this, tr("Export region"), tr("Nothing is visible to export."));
        return;
    }
    // WYSIWYG under a rotated navigation (a calibrated Match puts rotation in
    // the viewport): resample the display through the view transform at ~1
    // image px per output px, so what is exported IS what is on screen. An
    // axis-aligned crop of the frame would silently export the unrotated
    // bounding box instead.
    if (m_view->navigationRotated()) {
        const QImage full = DisplayRenderer::render(m_image, m_model);
        saveRenderedImage(m_view->renderVisible(full), tr("Export zoomed region"),
            [this] {
                return m_view->renderVisible(
                    floatToRgb64(DisplayRenderer::renderFloat(m_image, m_model)));
            });
        return;
    }
    // Render the whole frame, then crop to the currently visible image pixels.
    const QImage full = DisplayRenderer::render(m_image, m_model);
    saveRenderedImage(full.copy(roi.intersected(full.rect())), tr("Export zoomed region"),
        [this, roi] {
            QImage f16 = floatToRgb64(DisplayRenderer::renderFloat(m_image, m_model));
            return f16.copy(roi.intersected(f16.rect()));
        });
}

void MainWindow::saveRenderedImage(const QImage& img, const QString& title,
                                   const std::function<QImage()>& make16) {
    if (img.isNull()) return;
    bool want16 = false;
    int quality = -1;                                  // -1 = format default
    const QString path = exportImageDialogPath(title, bool(make16), &want16, &quality);
    if (path.isEmpty()) return;

    QImage toSave = img;
    if (want16) {
        toSave = make16();
        if (toSave.isNull()) { QMessageBox::warning(this, tr("Export failed"), tr("16-bit render failed.")); return; }
    }

    if (!toSave.save(path, nullptr, quality)) {
        QMessageBox::warning(this, tr("Export failed"),
                             tr("Could not write %1").arg(QFileInfo(path).fileName()));
        return;
    }
    statusBar()->showMessage(tr("Exported %1 (%2\u00d7%3%4)")
        .arg(QFileInfo(path).fileName()).arg(toSave.width()).arg(toSave.height())
        .arg(toSave.format() == QImage::Format_RGBX64 ? tr(" \u00b7 16-bit") : QString()), 3000);
}

// Colour management: when the current image embeds an ICC profile (XISF from
// PixInsight), render through profile→sRGB so colours match the producing
// application's colour-managed screen. LUT-based profiles QColorSpace cannot
// represent fall back silently to direct RGB.
// Drive a local Stellarium via its Remote Control plugin (F2 ▸ Plugins ▸
// Remote Control ▸ "Server enabled", default port 8090): point the
// planetarium at the J2000 direction of the clicked sky position, then match
// the field of view. Unlike the Aladin/SIMBAD web lookups this answers
// "where is this in TONIGHT'S sky from my site" — altitude, transit,
// horizon. Fire-and-forget; unreachable server = one status-bar hint.
void MainWindow::pointStellarium(double raDeg, double decDeg, double fovDeg) {
    if (!m_net) m_net = new QNetworkAccessManager(this);
    const double ra = raDeg * M_PI / 180.0, dec = decDeg * M_PI / 180.0;
    const QString j2000 = QStringLiteral("[%1,%2,%3]")
        .arg(std::cos(dec) * std::cos(ra), 0, 'f', 9)
        .arg(std::cos(dec) * std::sin(ra), 0, 'f', 9)
        .arg(std::sin(dec), 0, 'f', 9);
    const QString base = QStringLiteral("http://127.0.0.1:8090/api/main/");
    QNetworkRequest reqView{ QUrl(base + QStringLiteral("view")) };
    reqView.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    reqView.setTransferTimeout(2000);
    QNetworkReply* r = m_net->post(reqView, QByteArray("j2000=") + j2000.toLatin1());
    connect(r, &QNetworkReply::finished, this, [this, r, fovDeg, base] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            statusBar()->showMessage(
                tr("Stellarium not reachable — enable its Remote Control plugin (port 8090)"), 6000);
            return;
        }
        QNetworkRequest reqFov{ QUrl(base + QStringLiteral("fov")) };
        reqFov.setHeader(QNetworkRequest::ContentTypeHeader,
                         QStringLiteral("application/x-www-form-urlencoded"));
        reqFov.setTransferTimeout(2000);
        QNetworkReply* r2 = m_net->post(reqFov,
            QByteArray("fov=") + QByteArray::number(fovDeg, 'f', 2));
        connect(r2, &QNetworkReply::finished, r2, &QNetworkReply::deleteLater);
        statusBar()->showMessage(tr("Stellarium pointed at the target"), 4000);
    });
}

void MainWindow::updateIccTransform() {
    m_hasIcc = false;
    m_iccToSrgb = QColorTransform();
    if (m_header.iccProfile.isEmpty()) return;
    const QColorSpace cs = QColorSpace::fromIccProfile(m_header.iccProfile);
    if (!cs.isValid()) return;
    if (cs == QColorSpace::SRgb) return;             // identity — skip the cost
    m_iccToSrgb = cs.transformationToColorSpace(QColorSpace::SRgb);
    m_hasIcc = true;
}

QImage MainWindow::renderDisplayImage(const ImageData& img, const StretchModel& m) const {
    // The transform runs INSIDE the renderer (16-bit, before the 8-bit
    // dither) — converting after quantisation would re-band the gradients
    // the dither exists to protect.
    return DisplayRenderer::render(img, m, m_hasIcc ? &m_iccToSrgb : nullptr);
}

// While a histogram grip or adjustment slider is held down, image renders
// are deferred entirely — the histogram's OUTPUT curve gives the live
// feedback for free, and the (multi-second on large frames) render runs
// once, on release, from the final state.
void MainWindow::holdRenders(bool on) {
    if (on) { ++m_renderHold; return; }
    if (m_renderHold > 0 && --m_renderHold == 0 && m_renderPending) {
        m_renderPending = false;
        updateDisplay();
    }
}

void MainWindow::updateDisplay() {
    if (!m_image.isValid()) return;
    if (m_renderHold > 0) { m_renderPending = true; return; }
    // Coalescing async render: the GUI thread never blocks on a frame. If a
    // render is in flight, just note that a newer state exists — when the
    // worker returns, the LATEST model state is rendered next (intermediate
    // slider positions are skipped, which is exactly what a drag wants).
    if (m_renderWatcher->isRunning()) { m_renderPending = true; return; }
    m_renderPending = false;
    // Record the identity of the LIVE buffer first (ImageData copies are DEEP —
    // a pointer taken from the copy would never match m_image again), then give
    // the worker its own private copy, safe against mid-render rotations/switches.
    m_renderSrc = m_image.plane<float>(0);
    m_renderSize = QSize(m_image.width(), m_image.height());
    const ImageData img = m_image;
    const StretchModel::State st = m_model.state();
    const bool hasIcc = m_hasIcc;                // colour-manage in the worker,
    const QColorTransform icc = m_iccToSrgb;     // off the GUI thread
    m_renderWatcher->setFuture(QtConcurrent::run([img, st, hasIcc, icc]() -> QImage {
        StretchModel local;                      // plain value copy for the worker
        local.setState(st);
        return DisplayRenderer::render(img, local, hasIcc ? &icc : nullptr);
    }));
}

void MainWindow::onRenderDone() {
    const QImage frame = m_renderWatcher->result();
    if (!m_image.isValid()) return;
    // Identity check: only show the frame if it was rendered from the pixels
    // the window is STILL displaying — a frame from a pre-switch or pre-rotate
    // image (wrong content, maybe wrong size) is silently dropped and the
    // current state rendered instead.
    const bool current = (m_image.plane<float>(0) == m_renderSrc) &&
                         (QSize(m_image.width(), m_image.height()) == m_renderSize);
    if (current && !frame.isNull()) m_view->setDisplayImage(frame);
    if (m_renderPending || !current) updateDisplay();
}

void MainWindow::toggleImageOnly() {
    m_imageOnly = !m_imageOnly;
    if (m_imageOnly) {
        if (m_overlay) {
            // Overlay mode: remember which boxes were up, hide them all.
            m_savedLeft  = m_ovList && m_ovList->isVisible();
            m_savedInfo  = m_ovInfo && m_ovInfo->isVisible();
            m_savedRight = m_ovHist && m_ovHist->isVisible();
            if (m_ovList) m_ovList->hide();
            if (m_ovInfo) m_ovInfo->hide();
            if (m_ovHist) m_ovHist->hide();
        } else {
            m_savedLeft = m_leftDock->isVisible();
            m_savedRight = m_rightDock->isVisible();
            m_savedInfo = m_infoDock->isVisible();
            m_leftDock->hide();
            m_rightDock->hide();
            m_infoDock->hide();
        }
        menuBar()->hide();
        statusBar()->hide();
        for (QToolBar* tb : findChildren<QToolBar*>()) tb->hide();
    } else {
        if (m_overlay) {
            if (m_ovList) m_ovList->setVisible(m_savedLeft);
            if (m_ovInfo) m_ovInfo->setVisible(m_savedInfo);
            if (m_ovHist) m_ovHist->setVisible(m_savedRight);
            layoutOverlayPanels();
        } else {
            m_leftDock->setVisible(m_savedLeft);
            m_rightDock->setVisible(m_savedRight);
            m_infoDock->setVisible(m_savedInfo);
        }
        menuBar()->show();
        statusBar()->show();
        for (QToolBar* tb : findChildren<QToolBar*>()) tb->show();
    }
}

void MainWindow::ensureAnnotationsVisible() {
    if (!m_annotations || m_annotations->annotationsVisible()) return;
    m_annotations->setAnnotationsVisible(true);
    if (m_annVisAct) m_annVisAct->setChecked(true);   // keep the menu in sync
}

void MainWindow::refreshAnnotations() {
    if (!m_annotations) return;
    static const std::vector<Annotation> kNone;
    const auto it = m_annByPath.constFind(m_currentPath);
    m_annotations->rebuild(m_image.isValid() ? m_image.width() : 0,
                           m_image.isValid() ? m_image.height() : 0,
                           m_wcs, it != m_annByPath.constEnd() ? it.value() : kNone);
}

QString MainWindow::xformName(Xform x) {
    switch (x) {
        case Xform::RotCW:  return QStringLiteral("rotCW");
        case Xform::RotCCW: return QStringLiteral("rotCCW");
        case Xform::FlipH:  return QStringLiteral("flipH");
        default:            return QStringLiteral("flipV");
    }
}

bool MainWindow::xformFromName(const QString& n, Xform& out) {
    if (n == QLatin1String("rotCW"))       out = Xform::RotCW;
    else if (n == QLatin1String("rotCCW")) out = Xform::RotCCW;
    else if (n == QLatin1String("flipH"))  out = Xform::FlipH;
    else if (n == QLatin1String("flipV"))  out = Xform::FlipV;
    else return false;
    return true;
}

// The image reloads from disk in its stored orientation; catch the pixels up
// with any rotate/flip history recorded for this path (annotations in
// m_annByPath are already in the transformed coordinates).
// Imported annotations (SExtractor catalogs, plain JSON without an orientation
// record) are in the disk pixel frame; replay this image's orientation history
// over them — same ops, same order, same dimension tracking as the pixels.
QTransform MainWindow::diskToViewTransform(const QStringList& ops, const QSize& diskSize) const {
    QTransform T;
    int w = diskSize.width(), h = diskSize.height();
    for (const QString& n : ops) {
        if (n.startsWith(QLatin1String("rot:"))) {
            const double a = n.mid(4).toDouble();
            const double th = a * M_PI / 180.0;
            const double c = std::cos(th), s = std::sin(th);
            const int nw = std::max(1, int(std::ceil(w * std::fabs(c) + h * std::fabs(s))));
            const int nh = std::max(1, int(std::ceil(w * std::fabs(s) + h * std::fabs(c))));
            T = T * rotForwardTransform(a, w, h, nw, nh);
            w = nw; h = nh;
        } else {
            Xform x;
            if (!xformFromName(n, x)) continue;
            T = T * xformForwardTransform(x, w, h);
            if (x == Xform::RotCW || x == Xform::RotCCW) std::swap(w, h);
        }
    }
    return T;
}

void MainWindow::mapAnnotationsFromDiskFrame(std::vector<Annotation>& anns) {
    const QStringList ops = m_xformByPath.value(m_currentPath);
    if (ops.isEmpty() || anns.empty()) return;
    const QSize d = m_diskSizeByPath.value(m_currentPath,
                                           QSize(m_image.width(), m_image.height()));
    int w = d.width(), h = d.height();
    for (const QString& n : ops) {
        if (n.startsWith(QLatin1String("rot:"))) {
            const double a = n.mid(4).toDouble();
            const double th = a * M_PI / 180.0;
            const double c = std::cos(th), s = std::sin(th);
            // Same expanded-canvas formula as rotateArbitrary().
            const int nw = std::max(1, int(std::ceil(w * std::fabs(c) + h * std::fabs(s))));
            const int nh = std::max(1, int(std::ceil(w * std::fabs(s) + h * std::fabs(c))));
            rotateAnnotationsBy(anns, a, w, h, nw, nh);
            w = nw; h = nh;
        } else {
            Xform x;
            if (!xformFromName(n, x)) continue;
            transformAnnotations(anns, x, w, h);
            if (x == Xform::RotCW || x == Xform::RotCCW) std::swap(w, h);
        }
    }
}

// Walk `ops` backwards over `anns` with exact inverse maps, taking annotations
// from the frame the ops describe back to the disk frame.
void MainWindow::unmapAnnotationsToDiskFrame(std::vector<Annotation>& anns, const QStringList& ops) {
    if (ops.isEmpty() || anns.empty()) return;
    const QSize d = m_diskSizeByPath.value(m_currentPath,
                                           QSize(m_image.width(), m_image.height()));
    // Dimensions before/after each op, forward from the disk size.
    std::vector<QSize> dims;
    dims.push_back(d);
    for (const QString& n : ops) {
        int w = dims.back().width(), h = dims.back().height();
        if (n.startsWith(QLatin1String("rot:"))) {
            const double th = n.mid(4).toDouble() * M_PI / 180.0;
            const double c = std::cos(th), s = std::sin(th);
            dims.push_back(QSize(std::max(1, int(std::ceil(w * std::fabs(c) + h * std::fabs(s)))),
                                 std::max(1, int(std::ceil(w * std::fabs(s) + h * std::fabs(c))))));
        } else {
            Xform x;
            if (xformFromName(n, x) && (x == Xform::RotCW || x == Xform::RotCCW)) std::swap(w, h);
            dims.push_back(QSize(w, h));
        }
    }
    for (int i = ops.size() - 1; i >= 0; --i) {
        const QSize& from = dims[std::size_t(i) + 1];   // frame the annotations are in
        const QSize& to   = dims[std::size_t(i)];       // frame before this op
        const QString& n = ops[i];
        if (n.startsWith(QLatin1String("rot:"))) {
            rotateAnnotationsBy(anns, -n.mid(4).toDouble(),
                                from.width(), from.height(), to.width(), to.height());
        } else {
            Xform x;
            if (!xformFromName(n, x)) continue;
            transformAnnotations(anns, inverseXform(x), from.width(), from.height());
        }
    }
}

// Collapse an orientation history into an equivalent minimal one: adjacent
// arbitrary rotations merge into their sum, whole-turn rotations vanish, and
// adjacent inverse 90°/flip pairs cancel. A literal history is exact for the
// live pixels (each resample really expanded the canvas), but REPLAYING it
// from disk bakes those expansions in — e.g. rot:+a, rot:-a reloads as an
// upright image padded to (w + h·sin2a) × (h + w·sin2a). Replaying the
// canonical list reproduces the same geometry without the dead borders.
QStringList MainWindow::canonicalXforms(QStringList ops) {
    // Stage 1 — commute every arbitrary rotation to the tail and merge them
    // into ONE net rotation. Rotations commute with 90° rotations unchanged,
    // and with mirrors by negating the angle (M ∘ R(a) = R(-a) ∘ M). The
    // canonical form — lossless flips/90s first, at most one rot: last — means
    // replay expands the canvas at most ONCE, and net-zero rotations vanish
    // entirely (no accumulated black borders).
    {
        QStringList head;
        double tail = 0.0;
        int flipsAfter = 0;
        for (int i = ops.size() - 1; i >= 0; --i) {
            const QString& n = ops[i];
            if (n.startsWith(QLatin1String("rot:"))) {
                const double a = n.mid(4).toDouble();
                tail += (flipsAfter % 2) ? -a : a;
            } else {
                Xform x;
                if (xformFromName(n, x) && (x == Xform::FlipH || x == Xform::FlipV))
                    ++flipsAfter;
                head.prepend(n);
            }
        }
        ops = head;
        const double net = std::remainder(tail, 360.0);
        if (std::fabs(net) > 1e-4)
            ops << QStringLiteral("rot:%1").arg(net, 0, 'f', 4);
    }
    // Stage 2 — cancel adjacent inverse 90°/flip pairs (and drop whole-turn
    // rotations, defensively).
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < ops.size(); ) {          // whole-turn rotations are identity
            if (ops[i].startsWith(QLatin1String("rot:")) &&
                std::fabs(std::remainder(ops[i].mid(4).toDouble(), 360.0)) < 1e-4) {
                ops.removeAt(i); changed = true;
            } else ++i;
        }
        for (int i = 0; i + 1 < ops.size(); ) {
            const QString a = ops[i], b = ops[i + 1];
            const bool ra = a.startsWith(QLatin1String("rot:"));
            const bool rb = b.startsWith(QLatin1String("rot:"));
            if (ra && rb) {                          // merge adjacent rotations
                const double sum = a.mid(4).toDouble() + b.mid(4).toDouble();
                ops.removeAt(i + 1);
                ops[i] = QStringLiteral("rot:%1").arg(sum, 0, 'f', 4);
                changed = true; continue;
            }
            Xform xa, xb;
            if (!ra && !rb && xformFromName(a, xa) && xformFromName(b, xb) &&
                xb == inverseXform(xa)) {            // cancel inverse 90°/flip pairs
                ops.removeAt(i + 1); ops.removeAt(i);
                changed = true; continue;
            }
            ++i;
        }
    }
    return ops;
}

void MainWindow::normalizeOrientation() {
    auto it = m_xformByPath.find(m_currentPath);
    if (it == m_xformByPath.end()) return;
    const QStringList canon = canonicalXforms(it.value());
    if (canon == it.value()) return;               // already minimal — pixels are fine
    if (canon.isEmpty()) m_xformByPath.erase(it);
    else it.value() = canon;
    m_rotBasePath.clear();                         // pixels re-derived below — stale base
    m_rotBase = ImageData();
    bumpXformRev(m_currentPath);
    displayPath(m_currentPath);                    // one clean replay from source pixels
}

void MainWindow::reapplyStoredXforms() {
    // Canonicalize before replaying — a rotate/counter-rotate pair from a past
    // session must not bake dead black borders into the reloaded image.
    {
        auto it = m_xformByPath.find(m_currentPath);
        if (it != m_xformByPath.end()) {
            it.value() = canonicalXforms(it.value());
            if (it.value().isEmpty()) m_xformByPath.erase(it);
        }
    }
    const QStringList ops = m_xformByPath.value(m_currentPath);
    if (ops.isEmpty() || !m_image.isValid()) return;
    for (const QString& n : ops) {
        // Arbitrary rotations are stored as "rot:<deg>".
        if (n.startsWith(QLatin1String("rot:"))) {
            const double a = n.mid(4).toDouble();
            const int ow = m_image.width(), oh = m_image.height();
            m_image = rotateArbitrary(m_image, a);
            if (m_wcs.valid())
                m_wcs = m_wcs.rotated(a, ow, oh, m_image.width(), m_image.height());
            continue;
        }
        Xform x;
        if (!xformFromName(n, x)) continue;
        const int ow = m_image.width(), oh = m_image.height();
        switch (x) {
            case Xform::RotCW:  m_image = rotate90(m_image, true);  break;
            case Xform::RotCCW: m_image = rotate90(m_image, false); break;
            case Xform::FlipH:  m_image = flipHorizontal(m_image);  break;
            case Xform::FlipV:  m_image = flipVertical(m_image);    break;
        }
        if (m_wcs.valid()) {                     // solution follows each replayed op
            const Wcs::PixelXform px =
                x == Xform::RotCW  ? Wcs::PixelXform::RotCW  :
                x == Xform::RotCCW ? Wcs::PixelXform::RotCCW :
                x == Xform::FlipH  ? Wcs::PixelXform::FlipH  : Wcs::PixelXform::FlipV;
            m_wcs = m_wcs.transformed(px, ow, oh);
        }
    }
    m_view->setSource(&m_image);
    updateDisplay();
}

// ---- undo plumbing -----------------------------------------------------------

void MainWindow::setAnnotations(const QString& path, const std::vector<Annotation>& anns) {
    if (anns.empty()) m_annByPath.remove(path);
    else m_annByPath[path] = anns;
    m_annDirty.insert(path);                     // disk sidecar no longer matches
    if (path == m_currentPath) refreshAnnotations();
}

void MainWindow::pushAnnotationEdit(const QString& text, const QString& path,
                                    std::vector<Annotation> before) {
    m_undo->push(new AnnotationCmd(this, path, std::move(before),
                                   m_annByPath.value(path), text));
}

// Ctrl/Cmd+Shift+C: copy the selected annotation (the one showing handles).
void MainWindow::copySelectedAnnotation() {
    const auto& anns = m_annByPath.value(m_currentPath);
    const int idx = m_annotations->activeIndex();
    if (idx < 0 || idx >= int(anns.size())) {
        statusBar()->showMessage(tr("Click an annotation first to copy it"), 3000);
        return;
    }
    m_copiedAnn = anns[std::size_t(idx)];
    m_hasCopiedAnn = true;
    QApplication::clipboard()->setText(QString::fromUtf8(
        QJsonDocument(m_copiedAnn.toJson()).toJson(QJsonDocument::Compact)));
    statusBar()->showMessage(tr("Copied %1")
        .arg(m_copiedAnn.label.isEmpty() ? tr("annotation") : m_copiedAnn.label), 3000);
}

// Ctrl/Cmd+Shift+V: paste at the pointer's image position (image centre if the
// pointer is off the image).
void MainWindow::pasteAnnotationAtCursor() {
    if (!m_hasCopiedAnn || !m_image.isValid()) return;
    const double px = m_hoverValid ? m_hoverX : m_image.width() / 2.0;
    const double py = m_hoverValid ? m_hoverY : m_image.height() / 2.0;
    Annotation a = m_copiedAnn;
    const double dx = px - a.x, dy = py - a.y;
    a.x = px; a.y = py;
    if (a.type == Annotation::Type::Line) { a.x2 += dx; a.y2 += dy; }
    std::vector<Annotation> before = m_annByPath.value(m_currentPath);
    m_annByPath[m_currentPath].push_back(a);
    m_annDirty.insert(m_currentPath);
    refreshAnnotations();
    pushAnnotationEdit(tr("paste annotation"), m_currentPath, std::move(before));
}

// Delete key: remove the selected annotation (handles showing), or the most
// recently added one when nothing is selected.
void MainWindow::deleteActiveAnnotation() {
    auto it = m_annByPath.find(m_currentPath);
    if (it == m_annByPath.end() || it.value().empty()) return;
    int idx = m_annotations->activeIndex();
    if (idx < 0 || idx >= int(it.value().size()))
        idx = int(it.value().size()) - 1;            // latest
    std::vector<Annotation> before = m_annByPath.value(m_currentPath);
    it.value().erase(it.value().begin() + idx);
    m_annDirty.insert(m_currentPath);
    refreshAnnotations();
    pushAnnotationEdit(tr("delete annotation"), m_currentPath, std::move(before));
}

// Double-click editor: one small dialog for an annotation's text and colour.
void MainWindow::editAnnotationDialog(int annIdx) {
    auto it = m_annByPath.find(m_currentPath);
    if (it == m_annByPath.end() || annIdx < 0 || annIdx >= int(it.value().size())) return;
    Annotation& cur = it.value()[std::size_t(annIdx)];

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Edit annotation"));
    auto* form = new QVBoxLayout(&dlg);
    auto* edit = new QLineEdit(cur.label);
    QColor chosen = cur.color;
    auto* colorBtn = new QPushButton();
    auto setSwatch = [&](const QColor& c) {
        QPixmap pm(16, 16); pm.fill(c);
        colorBtn->setIcon(QIcon(pm));
        colorBtn->setText(c.name());
    };
    setSwatch(chosen);
    connect(colorBtn, &QPushButton::clicked, &dlg, [&] {
        const QColor c = QColorDialog::getColor(chosen, &dlg, tr("Annotation colour"));
        if (c.isValid()) { chosen = c; setSwatch(c); }
    });
    auto* row1 = new QHBoxLayout();
    row1->addWidget(new QLabel(tr("Text:")));
    row1->addWidget(edit, 1);
    auto* row2 = new QHBoxLayout();
    row2->addWidget(new QLabel(tr("Colour:")));
    row2->addWidget(colorBtn, 1);
    form->addLayout(row1);
    form->addLayout(row2);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addWidget(bb);
    edit->setFocus();

    if (dlg.exec() != QDialog::Accepted) return;
    const QString newLabel = edit->text().trimmed();
    if (newLabel == cur.label && chosen == cur.color) return;   // nothing changed
    std::vector<Annotation> before = m_annByPath.value(m_currentPath);
    cur.label = newLabel;
    cur.color = chosen;
    m_annDirty.insert(m_currentPath);
    refreshAnnotations();
    pushAnnotationEdit(tr("edit annotation"), m_currentPath, std::move(before));
}

// Fallback path for the Delete (Backspace) key: reaches here only when no
// focused widget consumed it and no shortcut matched — covers INI configs that
// bound the key elsewhere or shortcut-system quirks.
void MainWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Backspace && e->modifiers() == Qt::NoModifier) {
        deleteActiveAnnotation();
        e->accept();
        return;
    }
    QMainWindow::keyPressEvent(e);
}

// Tools ▸ Import SExtractor Catalog… — one ellipse annotation per detection.
// Needs X_IMAGE/Y_IMAGE; uses A/B/THETA_IMAGE for the shape when present.
void MainWindow::importSexCatalog() {
    if (!m_image.isValid()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import SExtractor catalog"), openDialogDir(),
        tr("SExtractor catalogs (*.cat *.txt);;All files (*)"));
    if (path.isEmpty()) return;

    QString err;
    const SexCatalog cat = SexCatalog::parse(path, &err);
    if (!cat.isValid()) { QMessageBox::warning(this, tr("Import failed"), err); return; }
    if (!cat.has("X_IMAGE") || !cat.has("Y_IMAGE")) {
        QMessageBox::warning(this, tr("Import failed"),
            tr("Catalog has no X_IMAGE/Y_IMAGE columns — add them to default.param."));
        return;
    }

    // Options dialog.
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Import SExtractor catalog"));
    auto* form = new QVBoxLayout(&dlg);
    form->addWidget(new QLabel(tr("%1 source(s), %2")
        .arg(cat.rowCount()).arg(QFileInfo(path).fileName())));
    auto* scaleRow = new QHBoxLayout();
    scaleRow->addWidget(new QLabel(tr("Ellipse scale × A/B_IMAGE:")));
    auto* scale = new QDoubleSpinBox();
    scale->setRange(0.5, 20.0); scale->setSingleStep(0.5); scale->setValue(3.0);
    scaleRow->addWidget(scale, 1);
    form->addLayout(scaleRow);
    auto* labelRow = new QHBoxLayout();
    labelRow->addWidget(new QLabel(tr("Label with:")));
    auto* labelBy = new QComboBox();
    labelBy->addItem(tr("None"));
    if (cat.has("NUMBER"))   labelBy->addItem("NUMBER");
    if (cat.has("MAG_AUTO")) labelBy->addItem("MAG_AUTO");
    labelRow->addWidget(labelBy, 1);
    form->addLayout(labelRow);
    auto* cleanOnly = new QCheckBox(tr("Skip flagged sources (FLAGS ≠ 0)"));
    cleanOnly->setEnabled(cat.has("FLAGS"));
    form->addWidget(cleanOnly);
    auto* classColor = new QCheckBox(tr("Colour stars gold (CLASS_STAR > 0.9)"));
    classColor->setEnabled(cat.has("CLASS_STAR"));
    classColor->setChecked(cat.has("CLASS_STAR"));
    form->addWidget(classColor);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    std::vector<Annotation> before = m_annByPath.value(m_currentPath);
    std::vector<Annotation> fresh;                 // catalog rows, in DISK coords
    const double k = scale->value();
    const QString lab = labelBy->currentText();
    int added = 0, skipped = 0;
    for (int r = 0; r < cat.rowCount(); ++r) {
        if (cleanOnly->isChecked() && cat.value(r, "FLAGS") != 0.0) { ++skipped; continue; }
        Annotation a;
        a.type = Annotation::Type::Ellipse;
        a.x = cat.value(r, "X_IMAGE") - 1.0;          // FITS 1-based -> 0-based
        a.y = cat.value(r, "Y_IMAGE") - 1.0;
        a.a = std::max(2.0, k * cat.value(r, "A_IMAGE", 2.0));
        a.b = std::max(2.0, k * cat.value(r, "B_IMAGE", 2.0));
        a.angleDeg = -cat.value(r, "THETA_IMAGE");    // CCW/x (y-up) -> y-down scene
        if (lab == QLatin1String("NUMBER"))
            a.label = QString::number(int(cat.value(r, "NUMBER")));
        else if (lab == QLatin1String("MAG_AUTO"))
            a.label = QString::number(cat.value(r, "MAG_AUTO"), 'f', 2);
        a.textSize = 8;
        a.color = (classColor->isChecked() && cat.value(r, "CLASS_STAR") > 0.9)
                      ? QColor("#ffd27f") : m_annColor;
        fresh.push_back(a);
        ++added;
    }
    // Catalog coordinates refer to the file on disk — carry the detections
    // through any rotation/flip applied to the view this session, then append.
    mapAnnotationsFromDiskFrame(fresh);
    auto& anns = m_annByPath[m_currentPath];
    anns.insert(anns.end(), fresh.begin(), fresh.end());
    m_annDirty.insert(m_currentPath);
    ensureAnnotationsVisible();                    // importing implies wanting to see them
    refreshAnnotations();
    pushAnnotationEdit(tr("import SExtractor catalog"), m_currentPath, std::move(before));
    statusBar()->showMessage(tr("Imported %1 source(s)%2")
        .arg(added).arg(skipped ? tr(", skipped %1 flagged").arg(skipped) : QString()), 4000);
}

// Warn when annotation edits would be lost on quit.
void MainWindow::closeEvent(QCloseEvent* e) {
    if (m_annDirty.isEmpty()) { e->accept(); return; }
    const auto btn = QMessageBox::warning(this, tr("Unsaved annotations"),
        tr("Annotations on %1 image(s) have not been saved.\n"
                       "Use Save Annotations\u2026 (right-click the image) to keep them.\n\n"
                       "Quit anyway?").arg(m_annDirty.size()),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (btn == QMessageBox::Discard) e->accept();
    else e->ignore();
}

// (De)serialize the display adjustments carried in annotation sidecars.
static QJsonObject adjustToJson(const AdjustParams& a) {
    QJsonObject o;
    o["brightness"] = a.brightness;   o["contrast"]   = a.contrast;
    o["gamma"]      = a.gamma;        o["shadows"]    = a.shadows;
    o["highlights"] = a.highlights;   o["blackpoint"] = a.blackpoint;
    o["whitepoint"] = a.whitepoint;   o["temperature"]= a.temperature;
    o["tint"]       = a.tint;         o["hue"]        = a.hue;
    o["saturation"] = a.saturation;   o["vibrance"]   = a.vibrance;
    return o;
}
static AdjustParams adjustFromJson(const QJsonObject& o) {
    AdjustParams a;
    a.brightness  = o.value("brightness").toDouble(0.0);
    a.contrast    = o.value("contrast").toDouble(0.0);
    a.gamma       = o.value("gamma").toDouble(1.0);
    a.shadows     = o.value("shadows").toDouble(0.0);
    a.highlights  = o.value("highlights").toDouble(0.0);
    a.blackpoint  = o.value("blackpoint").toDouble(0.0);
    a.whitepoint  = o.value("whitepoint").toDouble(1.0);
    a.temperature = o.value("temperature").toDouble(0.0);
    a.tint        = o.value("tint").toDouble(0.0);
    a.hue         = o.value("hue").toDouble(0.0);
    a.saturation  = o.value("saturation").toDouble(0.0);
    a.vibrance    = o.value("vibrance").toDouble(0.0);
    return a;
}

bool MainWindow::writeAnnotationsFileFor(const QString& key, const QString& path) {
    const auto& anns = m_annByPath.value(key);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QJsonDocument doc = AnnotationLayer::toJson(anns);
    QJsonObject root = doc.object();
    // Record the image orientation these annotations refer to, so a fresh
    // session can rotate/flip the reloaded image back into agreement.
    const QStringList ops = m_xformByPath.value(key);
    if (!ops.isEmpty()) {
        QJsonArray arr;
        for (const QString& o : ops) arr.append(o);
        root["orientation"] = arr;
    }
    // Display adjustments are per-image state too — carried in the sidecar and
    // restored on the next session's first visit. The current image's live in
    // the model; any other listed image's in its remembered stretch state.
    const AdjustParams adj = (key == m_currentPath) ? m_model.adjust()
                                                    : m_stfByPath.value(key).adj;
    if (!adj.identity())
        root["adjustments"] = adjustToJson(adj);
    // The FULL appearance — transfer function, per-channel windowing, GHS,
    // colormap, adjustments — so the sidecar reproduces what the screen
    // shows (a non-destructive transport fit included), not just the
    // adjustment layer on top of a fresh auto-stretch.
    const StretchModel::State display =
        (key == m_currentPath) ? m_model.state() : m_stfByPath.value(key);
    if (display.valid)
        root["display"] = StretchModel::stateToJson(display);
    doc.setObject(root);
    f.write(doc.toJson(QJsonDocument::Indented));
    m_annDirty.remove(key);
    return true;
}

bool MainWindow::writeAnnotationsFile(const QString& path) {
    if (!writeAnnotationsFileFor(m_currentPath, path)) {
        QMessageBox::warning(this, tr("Save failed"), tr("Could not write %1").arg(path));
        return false;
    }
    statusBar()->showMessage(tr("Saved %1 annotation(s)%2 to %3")
                                 .arg(m_annByPath.value(m_currentPath).size())
                                 .arg(m_model.adjust().identity() ? QString() : tr(" + adjustments"))
                                 .arg(QFileInfo(path).fileName()), 3000);
    return true;
}

// Silent save: overwrite the image's sidecar ("<image>_annotation.json") — the
// file displayPath() auto-loads. Falls back to the dialog for in-memory images.
void MainWindow::saveAnnotations() {
    // The sidecar carries the full display state too, so it is worth writing
    // even with no annotations drawn (e.g. to keep a transport fit).
    if (m_currentPath.isEmpty() || !m_image.isValid()) return;
    const QString sc = annotationSidecar(m_currentPath);
    if (sc.isEmpty()) { saveAnnotationsAs(); return; }
    writeAnnotationsFile(sc);
}

void MainWindow::saveAnnotationsAs() {
    if (m_annByPath.value(m_currentPath).empty() && m_model.adjust().identity()) return;
    const QString sc = annotationSidecar(m_currentPath);
    const QString suggest = sc.isEmpty() ? QStringLiteral("annotation.json") : sc;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save annotations as"), suggest, tr("Annotations (*_annotation.json *.json)"));
    if (path.isEmpty()) return;
    if (!sc.isEmpty() && QFileInfo(path).absoluteFilePath() == QFileInfo(sc).absoluteFilePath()) {
        QMessageBox::information(this, tr("Save annotations as"),
            tr("That is the image's default sidecar — plain Save Annotations writes it directly."));
    }
    writeAnnotationsFile(path);
}

void MainWindow::loadAnnotations() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load annotations"), openDialogDir(), tr("Annotations (*_annotation.json *.json)"));
    if (path.isEmpty()) return;
    loadAnnotationsFile(path);
}

void MainWindow::loadAnnotationsFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Load failed"), tr("Could not read %1").arg(path));
        return;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, tr("Load failed"), tr("JSON error: %1").arg(perr.errorString()));
        return;
    }
    QString err;
    std::vector<Annotation> anns = AnnotationLayer::fromJson(doc, &err);
    // Adjustments load even from an annotation-less sidecar.
    const bool hasAdj = doc.object().contains(QLatin1String("adjustments"));
    if (hasAdj)
        m_model.setAdjust(adjustFromJson(doc.object()["adjustments"].toObject()));
    if (anns.empty()) {
        if (hasAdj) {
            statusBar()->showMessage(tr("Loaded display adjustments (no annotations in file)"), 3000);
            return;
        }
        QMessageBox::warning(this, tr("Load failed"), err.isEmpty() ? tr("No annotations in file") : err);
        return;
    }
    std::vector<Annotation> before = m_annByPath.value(m_currentPath);
    // The file's coordinates live in the orientation it was saved in. That
    // recorded orientation is IGNORED as a view instruction: unmap the
    // annotations to the disk frame, then remap through the CURRENT view's
    // history — so they land on the sources regardless of either rotation.
    QStringList fileOps;
    for (const auto& v : doc.object()["orientation"].toArray()) fileOps << v.toString();
    if (!fileOps.isEmpty()) unmapAnnotationsToDiskFrame(anns, fileOps);
    mapAnnotationsFromDiskFrame(anns);             // no-op for an untransformed view
    m_annByPath[m_currentPath] = std::move(anns);
    m_annDirty.remove(m_currentPath);              // matches the file just read
    ensureAnnotationsVisible();                    // loading implies wanting to see them
    refreshAnnotations();
    pushAnnotationEdit(tr("load annotations"), m_currentPath, std::move(before));
    rememberRecent(QStringLiteral("recentJson"), path, Preferences::get().recentJsonMax);
    statusBar()->showMessage(tr("Loaded %1 annotation(s)%2")
        .arg(m_annByPath[m_currentPath].size())
        .arg(hasAdj ? tr(" + adjustments") : QString()), 3000);
}

// ---- recent-files history ----------------------------------------------------

void MainWindow::rememberRecent(const QString& settingsKey, const QString& path, int max) {
    if (path.isEmpty() || path.startsWith(QLatin1String("mem://")) || max <= 0) return;
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QStringLiteral("NebulaScope"), QStringLiteral("recent"));
    QStringList lst = s.value(settingsKey).toStringList();
    lst.removeAll(path);
    lst.prepend(path);
    while (lst.size() > max) lst.removeLast();
    s.setValue(settingsKey, lst);
    rebuildRecentMenus();
}

void MainWindow::rebuildRecentMenus() {
    if (!m_recentImagesMenu || !m_recentJsonMenu) return;
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QStringLiteral("NebulaScope"), QStringLiteral("recent"));
    auto fill = [this, &s](QMenu* menu, const QString& key, auto opener) {
        menu->clear();
        const QStringList lst = s.value(key).toStringList();
        for (const QString& p : lst) {
            // Show "name — dir" but act on the full path.
            QAction* a = menu->addAction(tr("%1 \u2014 %2")
                .arg(QFileInfo(p).fileName(), QFileInfo(p).absolutePath()));
            connect(a, &QAction::triggered, this, [opener, p] { opener(p); });
        }
        menu->setEnabled(!lst.isEmpty());
        if (!lst.isEmpty()) {
            menu->addSeparator();
            QAction* clr = menu->addAction(tr("Clear List"));
            connect(clr, &QAction::triggered, this, [this, key] {
                QSettings s2(QSettings::IniFormat, QSettings::UserScope,
                             QStringLiteral("NebulaScope"), QStringLiteral("recent"));
                s2.remove(key);
                rebuildRecentMenus();
            });
        }
    };
    fill(m_recentImagesMenu, QStringLiteral("recentImages"),
         [this](const QString& p) { openPaths({ p }); });
    fill(m_recentJsonMenu, QStringLiteral("recentJson"),
         [this](const QString& p) { loadAnnotationsFile(p); });
}

// Sky-patch background NEUTRALIZATION: the patch's per-channel medians are
// equalized to a COMMON output level — the mean of their current outputs —
// by solving each channel's black point numerically, with midtone and white
// held at their absolute positions. The background keeps its brightness (a
// barely visible dark grey, per the field call: true black hides faint
// nebulosity; grey lets it stand out) and loses only its colour cast; the
// movement per channel is the minimum needed for neutrality.
void MainWindow::onSkyPatchPicked(double x, double y, double w, double h) {
    if (!m_image.isValid()) return;
    const int x0 = std::max(0, int(std::floor(x)));
    const int y0 = std::max(0, int(std::floor(y)));
    const int x1 = std::min(m_image.width(),  int(std::ceil(x + w)));
    const int y1 = std::min(m_image.height(), int(std::ceil(y + h)));
    if (x1 - x0 < 2 || y1 - y0 < 2) return;
    const int nch = std::min(3, m_image.channels());
    // 1. Per-channel patch medians, in normalized [lo,hi] coordinates.
    std::vector<double> uMed(nch, std::nan(""));
    for (int c = 0; c < nch; ++c) {
        const float* p = m_image.plane<float>(c);
        std::vector<float> vals;
        vals.reserve(std::size_t(x1 - x0) * (y1 - y0));
        for (int yy = y0; yy < y1; ++yy)
            for (int xx = x0; xx < x1; ++xx) {
                const float v = p[std::size_t(yy) * m_image.width() + xx];
                if (std::isfinite(v)) vals.push_back(v);
            }
        if (vals.size() < 4) continue;
        std::nth_element(vals.begin(), vals.begin() + vals.size()/2, vals.end());
        const double range = std::max(1e-12, m_model.hi(c) - m_model.lo(c));
        uMed[c] = (double(vals[vals.size()/2]) - m_model.lo(c)) / range;
    }
    // 2. Current background output per channel (before the tone/colour
    //    adjustments, which are channel-identical for tone — equality
    //    survives them) and the common target: their mean.
    auto outAt = [&](int c, const ChannelStretch& cs) {
        return transferAt(uMed[c], m_model.fn(), cs, m_model.ghs());
    };
    double ySum = 0; int nOk = 0;
    for (int c = 0; c < nch; ++c) {
        if (std::isnan(uMed[c])) continue;
        ySum += outAt(c, m_model.channel(c));
        ++nOk;
    }
    if (nOk == 0) {
        statusBar()->showMessage(tr("Sky patch unusable — too small or not finite"), 5000);
        return;
    }
    const double yStar = ySum / nOk;
    // 3. Solve each channel's black point so its background hits yStar,
    //    midtone and white fixed. The output is not monotone in B in every
    //    mode (raising B darkens the coordinate but brightens the implied
    //    midtone ratio), so sample the feasible interval densely and refine.
    int nSet = 0;
    for (int c = 0; c < nch; ++c) {
        if (std::isnan(uMed[c])) continue;
        ChannelStretch cs = m_model.channel(c);
        if (uMed[c] >= cs.white - 0.02) continue;            // patch brighter than W
        const double bLo = -1.0;
        const double bHi = std::min(cs.mid - 0.02, uMed[c] - 1e-4);
        if (bHi <= bLo) continue;
        auto yAt = [&](double b) {
            ChannelStretch t = cs;
            t.black = b;
            return outAt(c, t);
        };
        double best = cs.black, bestErr = std::fabs(outAt(c, cs) - yStar);
        const int N1 = 256;
        for (int i = 0; i <= N1; ++i) {
            const double b = bLo + (bHi - bLo) * i / N1;
            const double e = std::fabs(yAt(b) - yStar);
            if (e < bestErr) { bestErr = e; best = b; }
        }
        const double step = (bHi - bLo) / N1;
        for (int i = 0; i <= 64; ++i) {                      // refine around the best
            const double b = std::max(bLo, std::min(bHi, best - step + 2.0 * step * i / 64));
            const double e = std::fabs(yAt(b) - yStar);
            if (e < bestErr) { bestErr = e; best = b; }
        }
        cs.black = best;
        m_model.setChannel(c, cs);
        ++nSet;
    }
    if (nSet == 0) {
        statusBar()->showMessage(tr("Sky patch unusable — brighter than the white point or too small"), 5000);
        return;
    }
    statusBar()->showMessage(tr("Background neutralized — %1 channel(s) equalized at output %2")
                                 .arg(nSet).arg(yStar, 0, 'f', 4), 6000);
}

void MainWindow::onEllipseDrawn(double cx, double cy, double a, double b) {
    if (!m_image.isValid()) return;
    bool ok = false;
    const QString label = QInputDialog::getText(this, tr("Ellipse annotation"),
        tr("Label (optional):"), QLineEdit::Normal, QString(), &ok);
    if (!ok) return;                              // cancelled — discard the shape
    Annotation an;
    an.type = Annotation::Type::Ellipse;
    an.x = cx; an.y = cy; an.a = a; an.b = b;
    an.label = label.trimmed();
    an.color = m_annColor;
    std::vector<Annotation> before = m_annByPath.value(m_currentPath);
    m_annByPath[m_currentPath].push_back(an);
    m_annDirty.insert(m_currentPath);
    refreshAnnotations();
    pushAnnotationEdit(tr("add ellipse"), m_currentPath, std::move(before));
}

void MainWindow::onLineDrawn(double x1, double y1, double x2, double y2) {
    if (!m_image.isValid()) return;
    bool ok = false;
    const QString label = QInputDialog::getText(this, tr("Line annotation"),
        tr("Label (optional):"), QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    Annotation an;
    an.type = Annotation::Type::Line;
    an.x = x1; an.y = y1; an.x2 = x2; an.y2 = y2;
    an.label = label.trimmed();
    an.color = m_annColor;
    std::vector<Annotation> before = m_annByPath.value(m_currentPath);
    m_annByPath[m_currentPath].push_back(an);
    m_annDirty.insert(m_currentPath);
    refreshAnnotations();
    pushAnnotationEdit(tr("add line"), m_currentPath, std::move(before));
}

void MainWindow::onTextPointPicked(double x, double y) {
    if (!m_image.isValid()) return;
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Text annotation"),
        tr("Text:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || text.trimmed().isEmpty()) return;
    Annotation an;
    an.type = Annotation::Type::Text;
    an.x = x; an.y = y;
    an.label = text.trimmed();
    an.textSize = Preferences::get().annTextSize;
    an.color = m_annColor;
    std::vector<Annotation> before = m_annByPath.value(m_currentPath);
    m_annByPath[m_currentPath].push_back(an);
    m_annDirty.insert(m_currentPath);
    refreshAnnotations();
    pushAnnotationEdit(tr("add text"), m_currentPath, std::move(before));
}

void MainWindow::onImageContextMenu(const QPoint& globalPos, int x, int y, bool onImage) {
    QMenu menu(this);

    double ra = 0, dec = 0;
    const bool sky = onImage && m_wcs.pixelToSky(x, y, ra, dec);
    const QString raS = sky ? Wcs::formatRa(ra) : QString();
    const QString decS = sky ? Wcs::formatDec(dec) : QString();
    QAction* aSky = menu.addAction(sky ? tr("Copy RA/Dec \u2014 %1 %2").arg(raS, decS)
                                       : tr("Copy RA/Dec (no astrometric solution)"));
    aSky->setEnabled(sky);

    QString pixText;
    if (onImage && m_image.isValid()) {
        const std::size_t i = std::size_t(y) * m_image.width() + x;
        if (m_image.channels() >= 3)
            pixText = QStringLiteral("(%1, %2)  R %3  G %4  B %5").arg(x).arg(y)
                          .arg(m_image.plane<float>(0)[i], 0, 'g', 6)
                          .arg(m_image.plane<float>(1)[i], 0, 'g', 6)
                          .arg(m_image.plane<float>(2)[i], 0, 'g', 6);
        else
            pixText = QStringLiteral("(%1, %2)  %3").arg(x).arg(y)
                          .arg(m_image.plane<float>(0)[i], 0, 'g', 6);
    }
    QAction* aPix = menu.addAction(tr("Copy Pixel Value"));
    aPix->setEnabled(!pixText.isEmpty());

    menu.addSeparator();
    // Editing actions for the annotation under the cursor, if any.
    const int annIdx = onImage ? m_annotations->hitTest(QPointF(x + 0.5, y + 0.5)) : -1;
    QAction* aEditText = nullptr; QAction* aEditColor = nullptr; QAction* aDelete = nullptr;
    QAction* aCopyAnn = nullptr;
    if (annIdx >= 0 && annIdx < int(m_annByPath[m_currentPath].size())) {
        const Annotation& cur = m_annByPath[m_currentPath][std::size_t(annIdx)];
        const QString what = cur.label.isEmpty() ? tr("annotation")
                                                 : tr("\u201c%1\u201d").arg(cur.label);
        aEditText  = menu.addAction(tr("Edit Text of %1\u2026").arg(what));
        aEditColor = menu.addAction(tr("Change Colour of %1\u2026").arg(what));
        aDelete    = menu.addAction(tr("Delete %1").arg(what));
        menu.addSeparator();
    }
    // Copy targets the SELECTED annotation (the one showing handles) when there
    // is one; otherwise whatever sits under the cursor. Labels can overhang
    // their neighbours, so the cursor hit alone was unreliable.
    const int copyIdx = (m_annotations->activeIndex() >= 0) ? m_annotations->activeIndex() : annIdx;
    if (copyIdx >= 0 && copyIdx < int(m_annByPath[m_currentPath].size())) {
        const Annotation& cc = m_annByPath[m_currentPath][std::size_t(copyIdx)];
        const QString ccName = cc.label.isEmpty() ? tr("annotation")
                                                  : tr("\u201c%1\u201d").arg(cc.label);
        aCopyAnn = menu.addAction(tr("Copy %1").arg(ccName));
    }
    QAction* aAnnotate = menu.addAction(tr("Annotate Here\u2026"));
    aAnnotate->setEnabled(onImage);
    QAction* aPasteAnn = menu.addAction(tr("Paste Annotation Here"));
    aPasteAnn->setEnabled(onImage && m_hasCopiedAnn);
    const bool hasAnn = !m_annByPath.value(m_currentPath).empty();
    // The sidecar persists more than shapes: a rotation/flip history or
    // non-identity adjustments alone are worth saving.
    const bool hasSidecarState = hasAnn ||
        !m_xformByPath.value(m_currentPath).isEmpty() ||
        !m_model.adjust().identity();
    QAction* aClearAnn = menu.addAction(tr("Clear Annotations"));
    aClearAnn->setEnabled(hasAnn);
    QAction* aSaveAnn = menu.addAction(tr("Save Annotations"));
    aSaveAnn->setEnabled(hasSidecarState);
    QAction* aSaveAnnAs = menu.addAction(tr("Save Annotations As\u2026"));
    aSaveAnnAs->setEnabled(hasSidecarState);
    QAction* aLoadAnn = menu.addAction(tr("Load Annotations\u2026"));
    QAction* aInvAnn = menu.addAction(tr("Invert Annotation Contrast"));
    aInvAnn->setCheckable(true);
    aInvAnn->setChecked(m_annotations->invertedContrast());

    // --- lookup section (needs an astrometric solution) ---
    menu.addSeparator();
    QAction* aAladin = nullptr;
    QAction* aSimbad = nullptr;
    QAction* aStellarium = nullptr;
    double alRa = 0, alDec = 0, alFovDeg = 0.25;
    if (m_wcs.valid() && onImage) {
        // Target the selected/hit annotation's centre, else the clicked pixel.
        double cx = x, cy = y;
        double radiusArcmin = 2.0;                     // SIMBAD cone-search radius
        if (copyIdx >= 0 && copyIdx < int(m_annByPath[m_currentPath].size())) {
            const Annotation& ta = m_annByPath[m_currentPath][std::size_t(copyIdx)];
            cx = ta.x; cy = ta.y;
            const double scaleDeg = m_wcs.pixelScaleArcsec() / 3600.0;
            // Aladin FOV ~10× the ellipse; SIMBAD radius ~2× (identify, not survey).
            alFovDeg = qBound(0.03, 10.0 * std::max(ta.a, ta.b) * scaleDeg, 5.0);
            radiusArcmin = qBound(0.2, 2.0 * std::max(ta.a, ta.b) * scaleDeg * 60.0, 30.0);
        }
        if (m_wcs.pixelToSky(cx, cy, alRa, alDec)) {
            const QString where = QStringLiteral("%1 %2").arg(Wcs::formatRa(alRa), Wcs::formatDec(alDec));
            aAladin = menu.addAction(tr("Look up in Aladin — %1").arg(where));
            aSimbad = menu.addAction(tr("Identify in SIMBAD — %1").arg(where));
            aSimbad->setData(radiusArcmin);
            aStellarium = menu.addAction(tr("Point Stellarium Here — %1").arg(where));
        }
    }

    menu.addSeparator();
    QAction* aFit = menu.addAction(tr("Zoom to Fit"));
    QAction* a11  = menu.addAction(tr("Zoom 1:1"));

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;
    if (chosen == aSky)      QApplication::clipboard()->setText(raS + QLatin1Char(' ') + decS);
    else if (chosen == aPix) QApplication::clipboard()->setText(pixText);
    else if (chosen == aAnnotate) {
        bool ok = false;
        const QString label = QInputDialog::getText(this, tr("Annotate"),
            sky ? tr("Label for %1 %2:").arg(raS, decS)
                : tr("Label for pixel (%1, %2):").arg(x).arg(y),
            QLineEdit::Normal, QString(), &ok);
        if (ok && !label.trimmed().isEmpty()) {
            Annotation a;
            a.label = label.trimmed();
            a.x = x; a.y = y;
            // Marker radius from Preferences (default ~1/40 of the frame).
            a.a = a.b = std::max(12.0, m_image.width() / Preferences::get().markerFrac);
            std::vector<Annotation> before = m_annByPath.value(m_currentPath);
            m_annByPath[m_currentPath].push_back(a);
            m_annDirty.insert(m_currentPath);
            refreshAnnotations();
            pushAnnotationEdit(tr("add annotation"), m_currentPath, std::move(before));
        }
    }
    else if (aEditText && chosen == aEditText) {
        Annotation& cur = m_annByPath[m_currentPath][std::size_t(annIdx)];
        bool ok = false;
        const QString t = QInputDialog::getText(this, tr("Edit annotation text"),
            tr("Text:"), QLineEdit::Normal, cur.label, &ok);
        if (ok) {
            std::vector<Annotation> before = m_annByPath.value(m_currentPath);
            cur.label = t.trimmed();
            m_annDirty.insert(m_currentPath);
            refreshAnnotations();
            pushAnnotationEdit(tr("edit annotation text"), m_currentPath, std::move(before));
        }
    }
    else if (aEditColor && chosen == aEditColor) {
        Annotation& cur = m_annByPath[m_currentPath][std::size_t(annIdx)];
        const QColor c = QColorDialog::getColor(cur.color, this, tr("Annotation colour"));
        if (c.isValid()) {
            std::vector<Annotation> before = m_annByPath.value(m_currentPath);
            cur.color = c;
            m_annDirty.insert(m_currentPath);
            refreshAnnotations();
            pushAnnotationEdit(tr("change annotation colour"), m_currentPath, std::move(before));
        }
    }
    else if (aDelete && chosen == aDelete) {
        auto& anns = m_annByPath[m_currentPath];
        std::vector<Annotation> before = m_annByPath.value(m_currentPath);
        anns.erase(anns.begin() + annIdx);
        m_annDirty.insert(m_currentPath);
        refreshAnnotations();
        pushAnnotationEdit(tr("delete annotation"), m_currentPath, std::move(before));
    }
    else if (aAladin && chosen == aAladin) {
        QUrl url(QStringLiteral("https://aladin.cds.unistra.fr/AladinLite/"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("target"), QStringLiteral("%1 %2")
            .arg(alRa, 0, 'f', 6).arg(alDec, 0, 'f', 6));
        q.addQueryItem(QStringLiteral("fov"), QString::number(alFovDeg, 'f', 3));
        q.addQueryItem(QStringLiteral("survey"), QStringLiteral("P/DSS2/color"));
        url.setQuery(q);
        QDesktopServices::openUrl(url);
    }
    else if (aStellarium && chosen == aStellarium) {
        // Sky-context framing: wider than Aladin's tight cutout.
        pointStellarium(alRa, alDec, qBound(0.5, alFovDeg * 4.0, 60.0));
    }
    else if (aSimbad && chosen == aSimbad) {
        // SIMBAD coordinate (cone) query around the target.
        QUrl url(QStringLiteral("https://simbad.cds.unistra.fr/simbad/sim-coo"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("Coord"), QStringLiteral("%1 %2")
            .arg(alRa, 0, 'f', 6).arg(alDec, 0, 'f', 6));
        q.addQueryItem(QStringLiteral("Radius"), QString::number(aSimbad->data().toDouble(), 'f', 2));
        q.addQueryItem(QStringLiteral("Radius.unit"), QStringLiteral("arcmin"));
        url.setQuery(q);
        QDesktopServices::openUrl(url);
    }
    else if (aCopyAnn && chosen == aCopyAnn) {
        m_copiedAnn = m_annByPath[m_currentPath][std::size_t(copyIdx)];
        m_hasCopiedAnn = true;
        // Also expose it as JSON on the system clipboard (handy for tooling).
        QApplication::clipboard()->setText(QString::fromUtf8(
            QJsonDocument(m_copiedAnn.toJson()).toJson(QJsonDocument::Compact)));
    }
    else if (chosen == aPasteAnn) {
        Annotation a = m_copiedAnn;
        const double dx = x - a.x, dy = y - a.y;   // anchor lands at the click point
        a.x = x; a.y = y;
        if (a.type == Annotation::Type::Line) { a.x2 += dx; a.y2 += dy; }
        std::vector<Annotation> before = m_annByPath.value(m_currentPath);
        m_annByPath[m_currentPath].push_back(a);
        m_annDirty.insert(m_currentPath);
        refreshAnnotations();
        pushAnnotationEdit(tr("paste annotation"), m_currentPath, std::move(before));
    }
    else if (chosen == aClearAnn) {
        std::vector<Annotation> before = m_annByPath.value(m_currentPath);
        m_annByPath.remove(m_currentPath);
        m_annDirty.insert(m_currentPath);
        refreshAnnotations();
        pushAnnotationEdit(tr("clear annotations"), m_currentPath, std::move(before));
    }
    else if (chosen == aSaveAnn) saveAnnotations();
    else if (chosen == aSaveAnnAs) saveAnnotationsAs();
    else if (chosen == aLoadAnn) loadAnnotations();
    else if (chosen == aInvAnn) {
        m_annotations->setInvertedContrast(aInvAnn->isChecked());
        refreshAnnotations();
    }
    else if (chosen == aFit) m_view->zoomToFit();
    else if (chosen == a11)  m_view->zoomActualSize();
}

void MainWindow::onPixelHovered(int x, int y, double r, double g, double b, bool valid) {
    m_hoverX = x; m_hoverY = y; m_hoverValid = valid;   // paste-at-cursor anchor
    if (m_valuesEverywhere) updateReadouts(x, y, valid);
    if (!valid) { m_pixelLabel->setText("—"); return; }
    QString txt;
    if (m_image.channels() >= 3)
        txt = tr("(%1, %2)   R %3  G %4  B %5")
            .arg(x).arg(y).arg(r, 0, 'g', 5).arg(g, 0, 'g', 5).arg(b, 0, 'g', 5);
    else
        txt = tr("(%1, %2)   %3").arg(x).arg(y).arg(r, 0, 'g', 5);
    double ra = 0, dec = 0;
    if (m_wcs.pixelToSky(x, y, ra, dec))
        txt += QStringLiteral("   ·   %1  %2").arg(Wcs::formatRa(ra), Wcs::formatDec(dec));
    m_pixelLabel->setText(txt);
}

// ---- Values Everywhere ------------------------------------------------------

namespace {
QString readoutText(const ImageData& img, int x, int y) {
    if (!img.isValid() || x < 0 || y < 0 || x >= img.width() || y >= img.height())
        return QStringLiteral("(%1, %2)  —").arg(x).arg(y);
    const std::size_t i = std::size_t(y) * img.width() + x;
    auto v = [&](int c) { return double(img.plane<float>(c)[i]); };
    if (img.channels() >= 3)
        return QStringLiteral("(%1, %2)  R %3  G %4  B %5").arg(x).arg(y)
            .arg(v(0), 0, 'g', 5).arg(v(1), 0, 'g', 5).arg(v(2), 0, 'g', 5);
    return QStringLiteral("(%1, %2)  %3").arg(x).arg(y).arg(v(0), 0, 'g', 5);
}
} // namespace

// The active cell's hovered pixel, shown in every cell against ITS data. The
// corresponding pixel in another cell is q = W_o^-1(W_c(p)) through the
// cells' calibrated-link "world" transforms — identity for same-size or
// unlinked views, i.e. the same coordinates, which is the comparison
// assumption; a calibrated pair maps through the alignment the user set.
void MainWindow::updateReadouts(int x, int y, bool valid) {
    ViewCell* act = m_grid->activeCell();
    if (!act) return;
    if (!valid) { clearReadouts(); return; }
    const QPointF world = act->world.map(QPointF(x + 0.5, y + 0.5));
    for (int i = 0; ViewCell* c = m_grid->cellAt(i); ++i) {
        const ImageData& img = (c == act) ? m_image : c->image;
        if (!img.isValid()) { c->setReadout(QString()); c->view()->setMarker(-1, -1); continue; }
        int qx = x, qy = y;
        if (c != act && c->calibrated && act->calibrated) {
            const QPointF q = c->world.inverted().map(world);
            qx = int(std::floor(q.x())); qy = int(std::floor(q.y()));
        }
        c->setReadout(readoutText(img, qx, qy));
        // Crosshair where the value was read — the visual counterpart of the
        // number, and the tell-tale when two views are NOT where you think.
        const bool inside = qx >= 0 && qy >= 0 && qx < img.width() && qy < img.height();
        c->view()->setMarker(inside ? qx : -1, inside ? qy : -1);
    }
}

// ---- Register (point-pair calibration) ---------------------------------------
//
// Geometry. Cells share a frame through their `world` transforms (image px ->
// shared frame); calibrated linking holds W_b(q) = W_a(p) for corresponding
// points. We solve the correspondence T: a-px -> b-px and hand it to the grid,
// which sets W_b = W_a * T^-1 (see ViewGrid::calibrateFromCorrespondence).
//
//  * One pair (p1 -> q1): the user already aligned scale/rotation by eye, so
//    keep the CURRENT linear part of the correspondence (from the two cells'
//    present worlds and viewports) and snap the translation so p1 maps exactly
//    onto q1. L = linear part of W_b^-1 * W_a (current); T(p) = L(p - p1) + q1.
//  * Two pairs (p1->q1, p2->q2): a similarity has 4 unknowns — translation
//    (2), scale, rotation — and two pairs give 4 equations: closed form.
//    Complex form: q = s·e^{iθ}·p + t, with s·e^{iθ} = (q2-q1)/(p2-p1).
//    No least squares, no iteration; exact on the two picked features.

// Least-squares affine fit q ≈ M p + t from point pairs (normal equations,
// 6 unknowns). Returns false if degenerate. rms = residual in B pixels.
static bool fitAffine(const std::vector<QPointF>& P, const std::vector<QPointF>& Q,
                      QTransform& out, double& rms) {
    const int n = int(P.size());
    if (n < 3 || Q.size() != P.size()) return false;
    // Solve two independent 3-unknown systems: qx = a·px + b·py + c, qy = d·px + e·py + f.
    double Sxx = 0, Sxy = 0, Sx = 0, Syy = 0, Sy = 0, S = n;
    double Sxqx = 0, Syqx = 0, Sqx = 0, Sxqy = 0, Syqy = 0, Sqy = 0;
    for (int i = 0; i < n; ++i) {
        const double x = P[i].x(), y = P[i].y(), u = Q[i].x(), v = Q[i].y();
        Sxx += x * x; Sxy += x * y; Sx += x; Syy += y * y; Sy += y;
        Sxqx += x * u; Syqx += y * u; Sqx += u;
        Sxqy += x * v; Syqy += y * v; Sqy += v;
    }
    // 3x3 symmetric normal matrix N = [[Sxx,Sxy,Sx],[Sxy,Syy,Sy],[Sx,Sy,S]]
    const double N[3][3] = { {Sxx, Sxy, Sx}, {Sxy, Syy, Sy}, {Sx, Sy, S} };
    const double det = N[0][0]*(N[1][1]*N[2][2]-N[1][2]*N[2][1])
                     - N[0][1]*(N[1][0]*N[2][2]-N[1][2]*N[2][0])
                     + N[0][2]*(N[1][0]*N[2][1]-N[1][1]*N[2][0]);
    if (std::abs(det) < 1e-12) return false;
    auto solve3 = [&](double r0, double r1, double r2, double& a, double& b, double& c) {
        // Cramer's rule
        auto d3 = [&](double m[3][3]) {
            return m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])
                 - m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
                 + m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
        };
        double M0[3][3], M1[3][3], M2[3][3];
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { M0[i][j] = N[i][j]; M1[i][j] = N[i][j]; M2[i][j] = N[i][j]; }
        M0[0][0] = r0; M0[1][0] = r1; M0[2][0] = r2;
        M1[0][1] = r0; M1[1][1] = r1; M1[2][1] = r2;
        M2[0][2] = r0; M2[1][2] = r1; M2[2][2] = r2;
        a = d3(M0) / det; b = d3(M1) / det; c = d3(M2) / det;
    };
    double a, b, c, d, e, f;
    solve3(Sxqx, Syqx, Sqx, a, b, c);
    solve3(Sxqy, Syqy, Sqy, d, e, f);
    // Qt row-vector convention: x' = m11·x + m21·y + dx, y' = m12·x + m22·y + dy.
    out = QTransform(a, d, b, e, c, f);
    double se = 0;
    for (int i = 0; i < n; ++i) {
        const QPointF q = out.map(P[i]);
        se += std::pow(q.x() - Q[i].x(), 2) + std::pow(q.y() - Q[i].y(), 2);
    }
    rms = std::sqrt(se / n);
    return true;
}

bool MainWindow::matchFromWcs(ViewCell* A, ViewCell* B) {
    const Wcs& wa = (A == m_grid->activeCell()) ? m_wcs : A->wcs;
    const Wcs& wb = (B == m_grid->activeCell()) ? m_wcs : B->wcs;
    if (!wa.valid() || !wb.valid()) return false;
    const ImageData& ia = (A == m_grid->activeCell()) ? m_image : A->image;
    const ImageData& ib = (B == m_grid->activeCell()) ? m_image : B->image;
    if (!ia.isValid() || !ib.isValid()) return false;
    // Sample a grid over A; keep the points whose sky position falls inside
    // B (the overlap). Affine-fit A px -> B px through those.
    std::vector<QPointF> P, Q;
    const int G = 12;
    for (int j = 0; j <= G; ++j)
        for (int i = 0; i <= G; ++i) {
            const double x = (ia.width() - 1) * double(i) / G;
            const double y = (ia.height() - 1) * double(j) / G;
            double ra, dec, bx, by;
            if (!wa.pixelToSky(x, y, ra, dec)) continue;
            if (!wb.skyToPixel(ra, dec, bx, by)) continue;
            if (bx < -0.5 || by < -0.5 || bx > ib.width() - 0.5 || by > ib.height() - 0.5) continue;
            P.push_back(QPointF(x, y)); Q.push_back(QPointF(bx, by));
        }
    if (P.size() < 6) {
        statusBar()->showMessage(tr("Match from WCS: the two fields barely overlap on the sky (%n common sample(s))",
                                    nullptr, int(P.size())), 6000);
        return false;
    }
    QTransform T; double rms = 0;
    if (!fitAffine(P, Q, T, rms)) return false;
    m_grid->calibrateFromCorrespondence(A, B, T);
    const double scale = std::sqrt(std::abs(T.determinant()));
    const double ang = std::atan2(T.m12(), T.m11()) * 180.0 / M_PI;
    statusBar()->showMessage(
        tr("Views matched from their plate solutions — scale ×%1, rotation %2°, %n overlap sample(s), affine residual %3 px rms",
           nullptr, int(P.size()))
            .arg(scale, 0, 'f', 4).arg(ang, 0, 'f', 2).arg(rms, 0, 'f', 3), 8000);
    return true;
}

void MainWindow::startRegister(bool secondPair) {
    // Need at least two occupied cells.
    int occ = 0;
    std::vector<ViewCell*> cells;
    for (int i = 0; ViewCell* c = m_grid->cellAt(i); ++i) if (c->occupied()) { ++occ; cells.push_back(c); }
    if (occ < 2) {
        statusBar()->showMessage(tr("Match needs two views with images — split the view first"), 4000);
        return;
    }
    // Both plate-solved? Then no picking: compute the correspondence from the
    // WCS. With exactly two occupied cells it is unambiguous; with more, the
    // active cell is the anchor and every other solved cell matches to it.
    if (!secondPair) {
        ViewCell* anchor = m_grid->activeCell();
        if (anchor && anchor->occupied()) {
            const Wcs& wa = m_wcs;
            if (wa.valid()) {
                int matched = 0;
                for (ViewCell* c : cells)
                    if (c != anchor && c->wcs.valid() && matchFromWcs(anchor, c)) ++matched;
                if (matched > 0) return;      // done — no picks needed
            }
        }
    }
    if (secondPair && !m_regFirst.a) {
        statusBar()->showMessage(tr("No first pair yet — M picks the first feature pair"), 4000);
        return;
    }
    if (!secondPair) m_regFirst = RegPair{};        // fresh registration
    m_regSecond = secondPair;
    m_regCur = RegPair{};
    m_regArmed = true;
    for (int i = 0; ViewCell* c = m_grid->cellAt(i); ++i)
        if (c->occupied()) c->view()->setDrawTool(ImageView::DrawTool::Register);
    statusBar()->showMessage(secondPair
        ? tr("Match (2nd pair): click a DIFFERENT feature in one view, then the same feature in the other — Esc cancels")
        : tr("Match: click a feature (star) in one view, then the same feature in the other — Esc cancels"), 0);
}

void MainWindow::cancelRegister() {
    m_regArmed = false;
    m_regCur = RegPair{};
    for (int i = 0; ViewCell* c = m_grid->cellAt(i); ++i) {
        c->view()->setDrawTool(ImageView::DrawTool::None);
        c->view()->setMarker(-1, -1);
    }
    statusBar()->showMessage(tr("Match cancelled"), 2500);
}

void MainWindow::onRegisterPointPicked(ImageView* v, double x, double y) {
    if (!m_regArmed) return;
    ViewCell* cell = nullptr;
    for (int i = 0; ViewCell* c = m_grid->cellAt(i); ++i) if (c->view() == v) cell = c;
    if (!cell) return;
    if (!m_regCur.a) {
        m_regCur.a = cell; m_regCur.pa = QPointF(x, y);
        // Visual cue on the first pick: the crosshair stays on that feature;
        // the other cells keep the cross cursor, this one is done.
        cell->view()->setMarker(int(std::floor(x)), int(std::floor(y)));
        cell->view()->setDrawTool(ImageView::DrawTool::None);
        statusBar()->showMessage(tr("First feature marked — now click the SAME feature in another view (Esc cancels)"), 0);
        return;
    }
    if (cell == m_regCur.a) {                         // re-pick in the same view: move the point
        m_regCur.pa = QPointF(x, y);
        cell->view()->setMarker(int(std::floor(x)), int(std::floor(y)));
        return;
    }
    m_regCur.b = cell; m_regCur.pb = QPointF(x, y);
    cell->view()->setMarker(int(std::floor(x)), int(std::floor(y)));
    finishRegisterPair();
}

void MainWindow::finishRegisterPair() {
    m_regArmed = false;
    for (int i = 0; ViewCell* c = m_grid->cellAt(i); ++i)
        c->view()->setDrawTool(ImageView::DrawTool::None);
    ViewCell* A = m_regCur.a; ViewCell* B = m_regCur.b;
    const QPointF p1 = m_regCur.pa, q1 = m_regCur.pb;
    QTransform T;                                     // A px -> B px
    QString how;
    if (m_regSecond && m_regFirst.a) {
        // Bring the stored first pair into (A,B) orientation.
        QPointF p0 = m_regFirst.pa, q0 = m_regFirst.pb;
        if (m_regFirst.a == B && m_regFirst.b == A) std::swap(p0, q0);
        else if (!(m_regFirst.a == A && m_regFirst.b == B)) {
            statusBar()->showMessage(tr("Second pair must use the same two views as the first — match restarted"), 5000);
            m_regFirst = RegPair{}; m_regSecond = false;
            return;
        }
        const double dpx = p1.x() - p0.x(), dpy = p1.y() - p0.y();
        const double dqx = q1.x() - q0.x(), dqy = q1.y() - q0.y();
        const double den = dpx * dpx + dpy * dpy;
        if (den < 1e-9) {
            statusBar()->showMessage(tr("Second feature is the same point as the first — pick a different one"), 5000);
            return;
        }
        // s·e^{iθ} = (q1-q0)/(p1-p0) as complex numbers.
        const double re = (dqx * dpx + dqy * dpy) / den;
        const double im = (dqy * dpx - dqx * dpy) / den;
        // q = M p + t,  M = [[re,-im],[im,re]],  t = q0 - M p0.
        const double tx = q0.x() - (re * p0.x() - im * p0.y());
        const double ty = q0.y() - (im * p0.x() + re * p0.y());
        T = QTransform(re, im, -im, re, tx, ty);       // Qt: (m11,m12,m21,m22,dx,dy), row-vector convention
        const double scale = std::hypot(re, im);
        const double ang = std::atan2(im, re) * 180.0 / M_PI;
        how = tr("scale ×%1, rotation %2°, translation snapped")
                  .arg(scale, 0, 'f', 4).arg(ang, 0, 'f', 2);
        m_regFirst = RegPair{};                       // consumed
    } else {
        // Keep the linear part the user aligned by eye; snap translation.
        // Current correspondence A->B: W_B^-1 ∘ W_A, but the two worlds only
        // define a correspondence if the cells are already calibrated. If not,
        // derive it from the two viewports: a world point lands at the same
        // screen position in both views, so A-px -> screen(A) -> B-px.
        QTransform cur;
        if (A->calibrated && B->calibrated)
            cur = A->world * B->world.inverted();
        else
            cur = A->view()->viewportTransform() * B->view()->viewportTransform().inverted();
        const QTransform L(cur.m11(), cur.m12(), cur.m21(), cur.m22(), 0, 0);
        const QPointF Lp1 = L.map(p1);
        T = L * QTransform::fromTranslate(q1.x() - Lp1.x(), q1.y() - Lp1.y());
        how = tr("translation snapped (scale/rotation as aligned by eye) — Shift+M adds a second pair for scale+rotation");
        m_regFirst = m_regCur;                        // available for a second pair
    }
    m_grid->calibrateFromCorrespondence(A, B, T);
    // Leave the crosshairs on the two features briefly as confirmation.
    QTimer::singleShot(2500, this, [this] {
        if (!m_regArmed && !m_valuesEverywhere)
            for (int i = 0; ViewCell* c = m_grid->cellAt(i); ++i) c->view()->setMarker(-1, -1);
    });
    statusBar()->showMessage(tr("Views matched — %1").arg(how), 8000);
}

void MainWindow::clearReadouts() {
    for (int i = 0; ViewCell* c = m_grid->cellAt(i); ++i) {
        c->setReadout(QString());
        c->view()->setMarker(-1, -1);
    }
}

} // namespace astro

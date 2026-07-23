#include "ui/MainWindow.h"
#include "ui/ImageView.h"
#include "ui/HistogramPanel.h"
#include "ui/RotateDialog.h"
#include "ui/ViewGrid.h"
#include "ui/InfoPanel.h"
#include "ui/CombineDialog.h"
#include "io/ImageReader.h"
#include "io/FitsReader.h"
#include "app/AppInfo.h"
#include "io/ImageWriter.h"
#include "core/ImageStats.h"
#include "core/ColorTransport.h"
#include "core/SexCatalog.h"
#include "core/Preferences.h"
#include "ui/PreferencesDialog.h"
#include "render/DisplayRenderer.h"
#include "core/Colormap.h"
#include "core/Transform.h"

#include <QDockWidget>
#include <QListWidget>
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QActionGroup>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
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
#include <QUrl>
#include <QUrlQuery>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QWidget>
#include <QHBoxLayout>

namespace astro {

MainWindow::MainWindow() {
    setWindowTitle("NebulaScope — Inspector");
    m_undo = new QUndoStack(this);
    m_annColor = Preferences::get().annColor;   // user default for new annotations
    buildUi();
    buildMenusAndToolbar();
    setAcceptDrops(true);          // drop FITS/XISF/images onto the window to open
    resize(1480, 940);
}

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
    m_view = m_grid->activeCell()->view();
    m_annotations = m_grid->activeCell()->layer();
    m_view->setSource(&m_image);

    // left dock: open images (with an append / remove / export button bar)
    m_leftDock = new QDockWidget("Open Images", this);
    m_leftDock->setObjectName("leftDock");
    auto* listHost = new QWidget(m_leftDock);
    auto* lv = new QVBoxLayout(listHost);
    lv->setContentsMargins(4, 4, 4, 4);
    lv->setSpacing(4);
    auto* bar = new QHBoxLayout();
    bar->setSpacing(4);
    auto* addBtn = new QToolButton(); addBtn->setText("+");  addBtn->setToolTip("Append files\u2026");
    auto* remBtn = new QToolButton(); remBtn->setText("\u2212"); remBtn->setToolTip("Remove selected (Del)");
    auto* expBtn = new QToolButton(); expBtn->setText("\u2913"); expBtn->setToolTip("Export list\u2026");
    bar->addWidget(addBtn);
    bar->addWidget(remBtn);
    bar->addStretch();
    bar->addWidget(expBtn);
    lv->addLayout(bar);

    m_fileList = new QListWidget(listHost);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setDragDropMode(QAbstractItemView::InternalMove);   // drag to reorder
    lv->addWidget(m_fileList, 1);
    m_leftDock->setWidget(listHost);
    addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);
    connect(m_fileList, &QListWidget::currentRowChanged, this, &MainWindow::showRow);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileList, &QListWidget::customContextMenuRequested, this, &MainWindow::onListContextMenu);
    connect(addBtn, &QToolButton::clicked, this, &MainWindow::appendToList);
    connect(remBtn, &QToolButton::clicked, this, &MainWindow::removeSelected);
    connect(expBtn, &QToolButton::clicked, this, &MainWindow::exportList);

    auto* del = new QShortcut(QKeySequence::Delete, m_fileList);
    del->setContext(Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, &MainWindow::removeSelected);

    // left dock (tabbed): image info / FITS structure / header
    m_infoDock = new QDockWidget("Info", this);
    m_infoDock->setObjectName("infoDock");
    m_info = new InfoPanel(&m_model, m_infoDock);
    m_infoDock->setWidget(m_info);
    addDockWidget(Qt::LeftDockWidgetArea, m_infoDock);
    tabifyDockWidget(m_leftDock, m_infoDock);
    m_leftDock->raise();

    // right dock: histogram
    m_rightDock = new QDockWidget("Histogram", this);
    m_rightDock->setObjectName("rightDock");
    m_hist = new HistogramPanel(&m_model, m_rightDock);
    m_hist->setSource(&m_image);
    m_rightDock->setWidget(m_hist);
    m_rightDock->setMinimumWidth(400);
    addDockWidget(Qt::RightDockWidgetArea, m_rightDock);

    m_pixelLabel = new QLabel("—");
    statusBar()->addPermanentWidget(m_pixelLabel);

    connect(&m_model, &StretchModel::changed, this, &MainWindow::updateDisplay);

    // Keep the active image's stretch memory current: every edit (drag, tab,
    // Auto/Reset) is snapshotted under its path, so revisiting restores it.
    connect(&m_model, &StretchModel::changed, this, [this] {
        if (!m_currentPath.isEmpty()) m_stfByPath.insert(m_currentPath, m_model.state());
    });
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
        setText(QStringLiteral("rotate to %1\u00b0").arg(nextDeg));
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
        setText(QStringLiteral("create %1").arg(m_name));
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
        : m_w(w), m_path(std::move(path)), m_x(x) { setText(QStringLiteral("transform image")); }
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
        QStringLiteral("Rotated %1\u00b0 — resampled onto %2\u00d7%3 (corners are blank)")
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
    QHash<QString, QString> defs, vals;
    for (auto it = acts.cbegin(); it != acts.cend(); ++it)
        defs[it.key()] = it.value()->shortcut().toString(QKeySequence::PortableText);
    for (auto it = keys.cbegin(); it != keys.cend(); ++it)
        defs[it.key()] = it.value()->key().toString(QKeySequence::PortableText);
    for (auto it = defs.cbegin(); it != defs.cend(); ++it) {
        if (!s.contains(it.key())) s.setValue(it.key(), it.value());
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
    QMessageBox::information(this, "Configure Shortcuts",
        QString("Shortcuts are read at startup from:<br><code>%1</code><br><br>"
                "Edit the <b>[shortcuts]</b> section using Qt key strings \u2014 e.g. "
                "<code>Ctrl+Shift+F</code>, <code>F11</code>, <code>Meta+Ctrl+F</code> "
                "(Meta = \u2318 on macOS). An empty value disables a shortcut. "
                "Restart NebulaScope to apply changes.").arg(m_shortcutFile));
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_shortcutFile).absolutePath()));
}

void MainWindow::showAbout() {
    QMessageBox box(this);
    box.setWindowTitle("About NebulaScope");
    box.setIconPixmap(windowIcon().pixmap(96, 96));
    box.setTextFormat(Qt::RichText);
    box.setText(QString(
        "<h2 style='margin:0'>NebulaScope</h2>"
        "<p style='color:#9fabb8;margin:2px 0 10px'>Astronomical image inspector — v%1</p>"
        "<p>Interactive FITS / XISF / JPEG / PNG / TIFF / WebP viewer with precise RGB"
        " histogram control, Generalised Hyperbolic Stretch, false-colour maps,"
        " and blink comparison.</p>"
        "<p style='color:#7e8b98;font-size:11px'>%2<br>Built with Qt, CFITSIO/CCfits and libXISF.</p>"
        "%3")
        .arg(QString::fromUtf8(appinfo::kVersion),
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
    statusBar()->showMessage("Copied stretch — right-click a list entry to paste", 2500);
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
    if (!m_copiedStretch.valid) { statusBar()->showMessage("No stretch copied yet", 2000); return; }
    auto sel = m_fileList->selectedItems();
    if (sel.isEmpty() && m_fileList->currentItem()) sel << m_fileList->currentItem();
    int n = 0;
    for (QListWidgetItem* it : sel) { applyCopiedStretch(it->data(Qt::UserRole).toString(), normalized); ++n; }
    statusBar()->showMessage(QStringLiteral("Pasted %1 stretch to %2 image(s)")
        .arg(normalized ? "normalized" : "absolute").arg(n), 3000);
}

void MainWindow::pasteStretchToAll(bool normalized) {
    if (!m_copiedStretch.valid) { statusBar()->showMessage("No stretch copied yet", 2000); return; }
    for (int i = 0; i < m_fileList->count(); ++i)
        applyCopiedStretch(m_fileList->item(i)->data(Qt::UserRole).toString(), normalized);
    statusBar()->showMessage(QStringLiteral("Pasted %1 stretch to all %2 image(s)")
        .arg(normalized ? "normalized" : "absolute").arg(m_fileList->count()), 3000);
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
    QAction* aCopy = menu.addAction("Copy Stretch");
    aCopy->setEnabled(!m_currentPath.isEmpty() && m_image.isValid());
    menu.addSeparator();
    QAction* aPasteN = menu.addAction(QStringLiteral("Paste Stretch — Normalized (%1)").arg(nSel));
    QAction* aPasteA = menu.addAction(QStringLiteral("Paste Stretch — Absolute (%1)").arg(nSel));
    QAction* aAllN = menu.addAction("Paste Stretch to All — Normalized");
    const bool canPaste = m_copiedStretch.valid && nSel > 0;
    aPasteN->setEnabled(canPaste);
    aPasteA->setEnabled(canPaste);
    aAllN->setEnabled(m_copiedStretch.valid && m_fileList->count() > 0);
    menu.addSeparator();
    QAction* aRemove = menu.addAction("Remove from List");
    aRemove->setEnabled(nSel > 0);

    QAction* chosen = menu.exec(m_fileList->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == aCopy) copyStretch();
    else if (chosen == aPasteN) pasteStretchToSelected(true);
    else if (chosen == aPasteA) pasteStretchToSelected(false);
    else if (chosen == aAllN) pasteStretchToAll(true);
    else if (chosen == aRemove) removeSelected();
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
    connect(v, &ImageView::ellipseDrawn, this, &MainWindow::onEllipseDrawn);
    connect(v, &ImageView::lineDrawn, this, &MainWindow::onLineDrawn);
    connect(v, &ImageView::textPointPicked, this, &MainWindow::onTextPointPicked);
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
            pushAnnotationEdit(QStringLiteral("move/resize annotation"), m_currentPath, std::move(before));
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
void MainWindow::onCellSwap(ViewCell* oldC, ViewCell* newC) {
    if (!newC || oldC == newC) return;
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
                m_fileList->setCurrentRow(i);
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
    QMenu* file = menuBar()->addMenu("&File");
    acts["open"] = file->addAction("&Open…", QKeySequence::Open, this, &MainWindow::openFile);
    m_recentImagesMenu = file->addMenu("Open &Recent");
    m_recentJsonMenu = file->addMenu("Recent A&nnotations");
    rebuildRecentMenus();
    acts["save_data_as"] = file->addAction("&Save Data As…", QKeySequence::SaveAs, this, &MainWindow::saveFile);
    acts["save_stretched_as"] = file->addAction("Save Stretc&hed As…", this, &MainWindow::saveStretched);
    acts["export_view"] = file->addAction("&Export View As…", QKeySequence("Ctrl+E"), this, &MainWindow::exportView);
    acts["export_region"] = file->addAction("Export &Zoomed Region As…", QKeySequence("Ctrl+Shift+E"), this, &MainWindow::exportRegion);
    file->addSeparator();
    acts["export_list"] = file->addAction("Export Image &List…", this, &MainWindow::exportList);
    acts["import_list"] = file->addAction("&Import Image List…", this, &MainWindow::importList);
    file->addSeparator();
    file->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);

    // Edit — undo/redo for annotation edits and image transforms.
    QMenu* editMenu = menuBar()->addMenu("&Edit");
    QAction* aUndo = m_undo->createUndoAction(this, "&Undo");
    aUndo->setShortcut(QKeySequence::Undo);
    QAction* aRedo = m_undo->createRedoAction(this, "&Redo");
    aRedo->setShortcut(QKeySequence::Redo);
    editMenu->addAction(aUndo);
    editMenu->addAction(aRedo);

    // View
    QMenu* view = menuBar()->addMenu("&View");
    QAction* aLeft = m_leftDock->toggleViewAction();
    aLeft->setShortcuts({ QKeySequence("F2"), QKeySequence("L") });
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
    acts["close_image"] = view->addAction("&Close Current Image", QKeySequence("C"), this, [this] {
        // Remove the displayed image from the list, reusing removeSelected()'s
        // cleanup (stretch memory, annotations, HDU children, next-row pick).
        QListWidgetItem* it = m_fileList->currentItem();
        if (!it) return;
        m_fileList->clearSelection();
        it->setSelected(true);
        removeSelected();
    });
    view->addSeparator();
    acts["zoom_to_fit"] = view->addAction("Zoom to &Fit", QKeySequence("F"), m_view, &ImageView::zoomToFit);
    acts["zoom_actual_size"] = view->addAction("Zoom &1:1", QKeySequence("1"), m_view, &ImageView::zoomActualSize);
    // QKeySequence::FullScreen is the platform-correct binding (⌃⌘F on macOS —
    // F11 there is taken by the system — and F11 on Windows/Linux).
    acts["fullscreen"] = view->addAction("&Fullscreen", QKeySequence::FullScreen, this, [this] {
        isFullScreen() ? showNormal() : showFullScreen();
    });
    acts["image_only"] = view->addAction("&Image Only", QKeySequence("Tab"), this, &MainWindow::toggleImageOnly);
    view->addSeparator();
    QAction* aGrid = view->addAction("Coordinate &Grid", QKeySequence("G"), this, [this](bool) {
        m_annotations->setGridVisible(!m_annotations->gridVisible());
        refreshAnnotations();
    });
    aGrid->setCheckable(true);
    acts["toggle_grid"] = aGrid;
    QAction* aAnnVis = view->addAction("Show &Annotations", QKeySequence("A"), this, [this] {
        m_annotations->setAnnotationsVisible(!m_annotations->annotationsVisible());
        refreshAnnotations();
    });
    aAnnVis->setCheckable(true);
    aAnnVis->setChecked(true);
    m_annVisAct = aAnnVis;
    acts["toggle_annotations"] = aAnnVis;
    // Hide the scrollbars ("elevators") for a clean canvas — pans still work
    // (right-drag / Shift-drag / middle-drag). Applies to every split cell.
    QAction* aScroll = view->addAction("Hide Scroll&bars", QKeySequence("H"), this, [this] {
        m_grid->setScrollBarsVisible(!m_grid->scrollBarsVisible());
    });
    aScroll->setCheckable(true);
    aScroll->setChecked(false);
    acts["toggle_scrollbars"] = aScroll;
    QAction* aOverlay = view->addAction("&Overlay Panels", QKeySequence("O"), this, [this] {
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
    QMenu* split = view->addMenu("Split &View");
    auto addPreset = [&](const QString& label, int r, int c) {
        split->addAction(label, this, [this, r, c] { m_grid->setGrid(r, c); });
    };
    addPreset(QStringLiteral("Single"), 1, 1);
    addPreset(QStringLiteral("1 \u00d7 2 (side by side)"), 1, 2);
    addPreset(QStringLiteral("2 \u00d7 1 (stacked)"), 2, 1);
    addPreset(QStringLiteral("2 \u00d7 2"), 2, 2);
    split->addSeparator();
    split->addAction(QStringLiteral("Custom\u2026"), this, [this] {
        // One dialog, two spinboxes (little up/down arrows), 1-5 each.
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Split view"));
        auto* form = new QFormLayout(&dlg);
        auto* rowsSpin = new QSpinBox(); rowsSpin->setRange(1, 5); rowsSpin->setValue(m_grid->rows());
        auto* colsSpin = new QSpinBox(); colsSpin->setRange(1, 5); colsSpin->setValue(m_grid->cols());
        form->addRow(QStringLiteral("Rows:"), rowsSpin);
        form->addRow(QStringLiteral("Columns:"), colsSpin);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(bb);
        if (dlg.exec() == QDialog::Accepted)
            m_grid->setGrid(rowsSpin->value(), colsSpin->value());
    });

    auto* esc = new QShortcut(QKeySequence("Esc"), this);
    connect(esc, &QShortcut::activated, this, [this] { if (m_imageOnly) toggleImageOnly(); });

    // Image — lossless 90° rotations and flips (applied to the pixel data).
    QMenu* image = menuBar()->addMenu("&Image");
    acts["rotate_cw"]  = image->addAction("Rotate 90\u00b0 CW",  QKeySequence("]"),       this, [this]{ applyTransform(Xform::RotCW); });
    acts["rotate_ccw"] = image->addAction("Rotate 90\u00b0 CCW", QKeySequence("["),       this, [this]{ applyTransform(Xform::RotCCW); });
    acts["rotate_by_angle"] = image->addAction("Rotate by &Angle\u2026", QKeySequence("Ctrl+R"), this, [this]{
        if (!m_image.isValid()) return;
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
        RotateDialog dlg(thumb, currentRotationAngle(), northUp, this);
        connect(&dlg, &RotateDialog::applyRequested, this, [this](double a){ pushRotateTo(a); });
        if (dlg.exec() == QDialog::Accepted) pushRotateTo(dlg.angle());
    });
    image->addSeparator();
    acts["flip_horizontal"] = image->addAction("Flip &Horizontal", QKeySequence("Ctrl+H"), this, [this]{ applyTransform(Xform::FlipH); });
    acts["flip_vertical"]   = image->addAction("Flip &Vertical",   QKeySequence("Ctrl+J"), this, [this]{ applyTransform(Xform::FlipV); });
    image->addSeparator();
    acts["reset_orientation"] = image->addAction("Reset &Orientation", this, &MainWindow::resetOrientation);
    image->addSeparator();
    acts["apply_saved_orientation"] = image->addAction("Apply &Saved Orientation", this, &MainWindow::applySavedOrientation);

    // Stretch — transfer the current image's stretch to others in the list.
    QMenu* stretch = menuBar()->addMenu("&Stretch");
    acts["copy_stretch"] = stretch->addAction("&Copy Stretch", QKeySequence("Ctrl+Alt+C"), this, &MainWindow::copyStretch);
    acts["paste_stretch_normalized"] = stretch->addAction("&Paste Stretch (Normalized)", QKeySequence("Ctrl+Alt+V"), this, [this]{ pasteStretchToSelected(true); });
    acts["paste_stretch_absolute"] = stretch->addAction("Paste Stretch (&Absolute)", QKeySequence("Ctrl+Alt+Shift+V"), this, [this]{ pasteStretchToSelected(false); });
    stretch->addSeparator();
    acts["paste_stretch_all"] = stretch->addAction("Paste Stretch to &All", this, [this]{ pasteStretchToAll(true); });

    // Tools — pixel-math utilities.
    QMenu* tools = menuBar()->addMenu("&Tools");
    acts["combine_channels"] = tools->addAction("&Combine Channels…", this, &MainWindow::combineChannels);
    acts["transport_colors"] = tools->addAction("&Transport Colors from Reference…", this, &MainWindow::transportColorsFromRef);
    acts["import_sextractor"] = tools->addAction("Import &SExtractor Catalog…", this, &MainWindow::importSexCatalog);

    // Help — the About action carries AboutRole, so on macOS Qt moves it into
    // the application menu (“NebulaScope ▸ About NebulaScope”) automatically.
    QMenu* help = menuBar()->addMenu("&Help");
    help->addAction("Configure &Shortcuts…", this, &MainWindow::showShortcutSettings);
    QAction* prefsAct = help->addAction("&Preferences…", this, [this] {
        PreferencesDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            m_annColor = Preferences::get().annColor;   // new-annotation default
            refreshAnnotations();                       // grid density, stroke width
        }
    });
    prefsAct->setMenuRole(QAction::PreferencesRole);   // macOS: app menu ▸ Settings…
    QAction* about = help->addAction("&About NebulaScope", this, &MainWindow::showAbout);
    about->setMenuRole(QAction::AboutRole);
    QAction* aboutQt = help->addAction("About &Qt", qApp, &QApplication::aboutQt);
    aboutQt->setMenuRole(QAction::AboutQtRole);

    // Walk the loaded-image list: Space = next, Shift+Space = previous.
    auto* next = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(next, &QShortcut::activated, this, &MainWindow::nextImage);
    auto* prev = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Space), this);
    connect(prev, &QShortcut::activated, this, &MainWindow::prevImage);
    keys["next_image"] = next;
    keys["prev_image"] = prev;
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

    // Toolbar
    QToolBar* tb = addToolBar("Main");
    tb->setObjectName("mainToolbar");
    tb->setMovable(false);
    tb->addAction("Open", this, &MainWindow::openFile);
    tb->addAction("Save", this, &MainWindow::saveFile);
    tb->addAction("Export", this, &MainWindow::exportView);
    tb->addSeparator();
    tb->addAction("Fit", m_view, &ImageView::zoomToFit);
    tb->addAction("1:1", m_view, &ImageView::zoomActualSize);
    tb->addSeparator();
    tb->addAction("\u21bb", this, [this]{ applyTransform(Xform::RotCW); })->setToolTip("Rotate 90\u00b0 clockwise ( ] )");
    tb->addAction("\u21ba", this, [this]{ applyTransform(Xform::RotCCW); })->setToolTip("Rotate 90\u00b0 counter-clockwise ( [ )");
    tb->addAction("\u2194", this, [this]{ applyTransform(Xform::FlipH); })->setToolTip("Flip horizontal (Ctrl+H)");
    tb->addAction("\u2195", this, [this]{ applyTransform(Xform::FlipV); })->setToolTip("Flip vertical (Ctrl+J)");
    tb->addSeparator();

    // False-colour map for mono images.
    tb->addWidget(new QLabel(" Colormap "));
    m_cmapCombo = new QComboBox();
    for (int i = 0; i < kColormapCount; ++i)
        m_cmapCombo->addItem(colormapName(static_cast<Colormap>(i)));
    tb->addWidget(m_cmapCombo);
    connect(m_cmapCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        m_model.setColormap(static_cast<Colormap>(i));
    });

    // Modifiers that compose with any base map.
    m_invertCheck = new QCheckBox("Inv");
    m_invertCheck->setToolTip("Invert the colormap (reverse the ramp)");
    tb->addWidget(m_invertCheck);
    connect(m_invertCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_model.setCmapInvert(on);
    });

    m_splitCheck = new QCheckBox("Split");
    m_splitCheck->setToolTip("Fold the ramp at a threshold: inverted below, normal above");
    tb->addWidget(m_splitCheck);
    connect(m_splitCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_model.setCmapSplit(on);
        if (m_splitWidget) m_splitWidget->setVisible(on);
    });

    // Split break-point slider (visible only when Split is enabled).
    m_splitWidget = new QWidget();
    auto* sl = new QHBoxLayout(m_splitWidget);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->addWidget(new QLabel(" break "));
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
    m_toolEllipse = tb->addAction("\u25ef Ellipse");
    m_toolEllipse->setToolTip("Draw an ellipse annotation — drag outward from the centre");
    m_toolLine = tb->addAction("\u2571 Line");
    m_toolLine->setToolTip("Draw a line annotation — drag from start to end");
    m_toolText = tb->addAction("T Text");
    m_toolText->setToolTip("Place a text annotation — click the anchor point");
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

void MainWindow::openFile() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, "Open image(s)", QString(),
        "Astronomy & images (*.fits *.fit *.fts *.fz *.xisf *.jpg *.jpeg *.png *.tif *.tiff *.webp);;All files (*)");
    if (!paths.isEmpty()) addPaths(paths);
}

void MainWindow::openPaths(const QStringList& paths) {
    addPaths(paths);
}

// ---- multi-HDU list keys ----------------------------------------------------
// A list row can point at one HDU inside a FITS file. The row's UserRole then
// holds "<path>||hdu=<n>"; splitHduKey() recovers the file path and HDU index.
static QString makeHduKey(const QString& base, int hdu) {
    return base + QStringLiteral("||hdu=%1").arg(hdu);
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
    QListWidgetItem* firstAdded = nullptr;
    for (const QString& p : paths) {
        if (p.isEmpty()) continue;
        int hduReq = -1;
        const QString base = splitHduKey(p, hduReq);   // re-imported lists may carry ||hdu=
        auto* it = new QListWidgetItem(
            hduReq < 0 ? QFileInfo(base).fileName()
                       : QStringLiteral("%1 [HDU %2]").arg(QFileInfo(base).fileName()).arg(hduReq),
            m_fileList);
        it->setData(Qt::UserRole, p);
        it->setToolTip(p);
        if (!firstAdded) firstAdded = it;

        // Multi-extension FITS: show the file like a folder, one indented child
        // row per image HDU. The parent row loads the first image HDU.
        if (hduReq < 0) {
            const QString ext = QFileInfo(base).suffix().toLower();
            if (ext == "fits" || ext == "fit" || ext == "fts" || ext == "fz") {
                const QList<io::FitsHduEntry> hdus = io::listFitsImageHdus(base);
                if (hdus.size() > 1) {
                    it->setText(it->text() + QStringLiteral("  \u25be %1 HDUs").arg(hdus.size()));
                    for (const io::FitsHduEntry& e : hdus) {
                        auto* child = new QListWidgetItem(
                            QStringLiteral("    \u2937 HDU %1 \u00b7 %2").arg(e.hdu).arg(e.summary),
                            m_fileList);
                        child->setData(Qt::UserRole, makeHduKey(base, e.hdu));
                        child->setToolTip(QStringLiteral("%1 \u2014 HDU %2").arg(base).arg(e.hdu));
                        child->setForeground(QColor("#8fa3b8"));
                    }
                }
            }
        }
    }
    // Show the first newly added file: in the first empty view if one exists
    // (multi-view workflow), else the active view. Selecting the row fires
    // currentRowChanged -> showRow -> displayPath into the active cell.
    if (firstAdded) {
        if (ViewCell* empty = m_grid->firstEmptyVisible()) m_grid->activate(empty);
        if (m_fileList->currentItem() == firstAdded) {
            // Row already current (e.g. sole entry re-opened): the row-change
            // signal won't fire, so display explicitly into the new cell.
            displayPath(firstAdded->data(Qt::UserRole).toString());
        } else {
            m_fileList->setCurrentItem(firstAdded);
        }
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
    it->setToolTip(name + "  (in-memory combine — use Save Data As… to keep)");
    m_fileList->setCurrentItem(it);               // triggers showRow -> displayPath
    m_undo->push(new SyntheticImageCmd(this, key, name, m_synthetic.value(key)));
    return key;
}

void MainWindow::removeSyntheticEntry(const QString& key) {
    for (int i = 0; i < m_fileList->count(); ++i) {
        QListWidgetItem* it = m_fileList->item(i);
        if (it->data(Qt::UserRole).toString() != key) continue;
        m_fileList->clearSelection();
        it->setSelected(true);
        removeSelected();          // full cleanup: stretch memory, synthetic map, empty state
        return;
    }
}

void MainWindow::restoreSyntheticEntry(const QString& key, const QString& name,
                                       std::shared_ptr<ImageData> img) {
    m_synthetic.insert(key, std::move(img));
    auto* it = new QListWidgetItem(name, m_fileList);
    it->setData(Qt::UserRole, key);
    it->setToolTip(name + "  (in-memory combine — use Save Data As… to keep)");
    m_fileList->setCurrentItem(it);
}

// Tools ▸ Combine Channels: gather every MONO image in the list (loading those
// not yet decoded), run the dialog, and add the RGB result to the list.
void MainWindow::combineChannels() {
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
                viewMap = [lut, lo, hi, cs](float v) -> float {
                    if (!std::isfinite(v)) return 0.0f;
                    const double t = windowCoord(v, lo, hi, cs);
                    int idx = int(t * 4095.0 + 0.5);
                    idx = idx < 0 ? 0 : (idx > 4095 ? 4095 : idx);
                    return (*lut)[std::size_t(idx)];
                };
            }
            mono.push_back({ item->text(), img, std::move(viewMap) });
        }
    }
    if (mono.size() < 2) {
        QMessageBox::information(this, "Combine Channels",
            "Load at least two single-channel (mono) images into the list first.");
        return;
    }
    CombineDialog dlg(std::move(mono), this);
    if (dlg.exec() == QDialog::Accepted && dlg.hasResult()) {
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
}

void MainWindow::appendToList() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, "Append image(s)", QString(),
        "Astronomy & images (*.fits *.fit *.fts *.fz *.xisf *.jpg *.jpeg *.png *.tif *.tiff *.webp);;All files (*)");
    if (!paths.isEmpty()) addPaths(paths);
}

void MainWindow::removeSelected() {
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
    // Forget any per-image stretch memory for removed paths, then delete rows.
    for (QListWidgetItem* it : doomed) {
        const QString p = it->data(Qt::UserRole).toString();
        m_stfByPath.remove(p);
        m_synthetic.remove(p);                           // free any in-memory combine
        delete m_fileList->takeItem(m_fileList->row(it));
    }
    if (m_fileList->count() == 0) {
        // Last image closed: empty every view cell and the live state.
        m_currentPath.clear();
        m_image = ImageData();
        m_header = ImageHeader();
        m_wcs = Wcs();
        m_annotations->rebuild(0, 0, m_wcs, {});
        m_grid->clearAll();
        m_hist->setSource(nullptr);
        m_info->setData(nullptr, nullptr, {});
        m_pixelLabel->setText("\u2014");
        setWindowTitle("NebulaScope \u2014 Inspector");
    } else if (m_fileList->currentRow() < 0) {
        m_fileList->setCurrentRow(0);
    }
}

void MainWindow::exportList() {
    if (m_fileList->count() == 0) return;
    const QString path = QFileDialog::getSaveFileName(
        this, "Export image list", "images.txt", "Text file (*.txt);;All files (*)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export failed", "Could not write " + path);
        return;
    }
    QTextStream out(&f);
    for (int i = 0; i < m_fileList->count(); ++i)
        out << m_fileList->item(i)->data(Qt::UserRole).toString() << '\n';
    statusBar()->showMessage(QStringLiteral("Exported list of %1 file(s)").arg(m_fileList->count()), 3000);
}

void MainWindow::importList() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Import image list", QString(), "Text file (*.txt);;All files (*)");
    if (!path.isEmpty()) importListFile(path);
}

void MainWindow::applySplitLayout(int rows, int cols) {
    m_grid->setGrid(rows, cols);
    const int n = std::min(rows * cols, m_fileList->count());
    for (int i = 0; i < n; ++i) {                 // raster order: row-major cells
        m_grid->activate(m_grid->cellAt(i));      // swap state into cell i
        showRow(i);                               // decode list row i into it
    }
    if (n > 0) m_grid->activate(m_grid->cellAt(0));
}

void MainWindow::importListFile(const QString& listPath) {
    QFile f(listPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Import failed", "Could not read " + listPath);
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
        statusBar()->showMessage("List file had no entries", 3000);
        return;
    }
    addPaths(paths);
    statusBar()->showMessage(QStringLiteral("Imported %1 file(s)").arg(paths.size()), 3000);
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
    m_fileList->setCurrentRow((row + 1) % n);            // wrap to top after last
}

void MainWindow::prevImage() {
    const int n = m_fileList->count();
    if (n == 0) return;
    const int row = m_fileList->currentRow();
    m_fileList->setCurrentRow((row - 1 + n) % n);        // wrap to bottom before first
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
        QMessageBox::information(this, "Transport Colors",
            "Load a second image to use as the colour reference.");
        return;
    }
    bool ok = false;
    // Reference picker + transport strength in one small dialog.
    QDialog dlg(this);
    dlg.setWindowTitle("Transport Colors");
    auto* form = new QFormLayout(&dlg);
    auto* combo = new QComboBox();
    combo->addItems(names);
    form->addRow("Reference (colours to adopt):", combo);
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
    form->addRow("Strength:", strengthRow);
    auto* hint = new QLabel("100% = full palette adoption; lower values blend\n"
                            "the transported colours with the original.");
    hint->setStyleSheet("color:#7e8b98; font-size:11px;");
    form->addRow(QString(), hint);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    ok = dlg.exec() == QDialog::Accepted;
    if (!ok) return;
    s_lastStrength = strength->value();
    const QString pick = combo->currentText();
    const QString key = keys[combo->currentIndex()];

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
        if (!res.ok) { QMessageBox::warning(this, "Transport Colors", res.error); return; }
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
    TransportRoi srcRoi;
    if (!diskFrame) {
        srcRoi = toRoi(m_view->visibleImageRect());
    } else {
        const QTransform T = diskToViewTransform(srcOps, QSize(srcPix->width(), srcPix->height()));
        const QRect diskRect = T.inverted().mapRect(QRectF(m_view->visibleImageRect()))
                                   .toAlignedRect()
                                   .intersected(QRect(0, 0, srcPix->width(), srcPix->height()));
        srcRoi = toRoi(diskRect);
    }
    TransportRoi refRoi;                          // whole image unless shown in a cell
    for (int i = 0; i < m_grid->rows() * m_grid->cols(); ++i) {
        ViewCell* c = m_grid->cellAt(i);
        if (c && c != m_grid->activeCell() && c->path == key) {
            const QStringList refOps = m_xformByPath.value(key);
            QRect vis = c->view()->visibleImageRect();
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

    ColorTransportResult res = transportColors(srcDisp, refDisp, 15, 200000, srcRoi, refRoi);
    QApplication::restoreOverrideCursor();
    if (!res.ok) {
        QMessageBox::warning(this, "Transport Colors", QString::fromStdString(res.error));
        return;
    }
    // Partial transport: blend transported colours back toward the original.
    const double s = s_lastStrength / 100.0;
    if (s < 0.999) {
        for (int c = 0; c < res.image.channels(); ++c) {
            float* o = res.image.plane<float>(c);
            const float* orig = srcDisp.plane<float>(std::min(c, srcDisp.channels() - 1));
            const std::size_t np = res.image.samplesPerChannel();
            for (std::size_t i = 0; i < np; ++i)
                o[i] = float(orig[i] + s * (o[i] - orig[i]));
        }
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
    statusBar()->showMessage(QStringLiteral("Colours transported from %1").arg(pick), 4000);
}

void MainWindow::resetOrientation() {
    if (m_currentPath.isEmpty() || !m_image.isValid()) return;
    const QStringList ops = m_xformByPath.value(m_currentPath);
    if (ops.isEmpty()) { statusBar()->showMessage("No stored orientation for this image", 3000); return; }
    auto it = m_annByPath.find(m_currentPath);
    if (it != m_annByPath.end() && !it.value().empty()) {
        unmapAnnotationsToDiskFrame(it.value(), ops);
        m_annDirty.insert(m_currentPath);
    }
    m_xformByPath.remove(m_currentPath);
    bumpXformRev(m_currentPath);
    m_rotBasePath.clear();                    // stale rotation-dialog base
    m_rotBase = ImageData();
    m_undo->clear();
    displayPath(m_currentPath);               // fresh decode; replay is now a no-op
    statusBar()->showMessage(QStringLiteral("Orientation reset — showing disk pixels (%1\u00d7%2)")
                                 .arg(m_image.width()).arg(m_image.height()), 4000);
}

// Replay the orientation stashed from the annotation sidecar on the current
// image, through the SAME machinery as manual transforms (pixels, annotations,
// WCS, view links, and history recording all stay consistent — so a later
// annotation save re-records it).
void MainWindow::applySavedOrientation() {
    if (m_currentPath.isEmpty() || !m_image.isValid()) return;
    const QStringList ops = m_sidecarOrientByPath.value(m_currentPath);
    if (ops.isEmpty()) {
        statusBar()->showMessage("No saved orientation for this image", 3000);
        return;
    }
    for (const QString& n : ops) {
        if (n.startsWith(QLatin1String("rot:"))) doRotateArbitrary(n.mid(4).toDouble());
        else { Xform x; if (xformFromName(n, x)) doTransform(x); }
    }
    m_sidecarOrientByPath.remove(m_currentPath);   // now carried by the live history
    normalizeOrientation();
    statusBar()->showMessage(QStringLiteral("Saved orientation applied (%1×%2)")
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
            box->setCursor(onS ? Qt::SplitVCursor : (onW ? Qt::SizeHorCursor : Qt::ArrowCursor));
            break;
        }
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
    ImageData loaded; ImageHeader hdr;
    auto syn = m_synthetic.constFind(path);
    if (syn != m_synthetic.constEnd() && syn.value()) {
        loaded = *syn.value();                           // in-memory combine result (copy)
        hdr.container  = "In-memory";
        hdr.nativeType = "32-bit float (channel combine)";
        hdr.structure  = QStringList{ QString("RGB combine · %1×%2 · 3 channels")
                                        .arg(loaded.width()).arg(loaded.height()) };
    } else {
        int hduReq = -1;
        const QString base = splitHduKey(path, hduReq);
        io::LoadOptions lopts;
        lopts.fitsHdu = hduReq;                          // -1 = first image HDU
        io::LoadResult res = io::loadImage(base, lopts); // promoteToFloat = true
        if (!res.ok) {
            QMessageBox::warning(this, "Open failed", res.error);
            return;
        }
        loaded = std::move(res.image);
        hdr    = std::move(res.header);
    }
    m_image = std::move(loaded);
    m_header = std::move(hdr);

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
    const std::vector<ChannelStats> stats = computeStats(m_image);
    m_curStats = stats;                                  // cache for Copy/Paste Stretch anchors

    // Per-image STF memory: restore this file's last stretch, or auto-stretch on
    // first visit. Set m_currentPath first so the change handler saves correctly.
    m_currentPath = path;
    {   // Recent-images history (strip any ||hdu= suffix; skip in-memory results).
        int hduDummy = -1;
        rememberRecent(QStringLiteral("recentImages"), splitHduKey(path, hduDummy),
                       Preferences::get().recentImagesMax);
    }

    // Auto-load the sidecar annotation file on the first visit to this image.
    if (Preferences::get().autoLoadSidecar && !m_annByPath.contains(path)) {
        const QString sc = annotationSidecar(path);
        if (!sc.isEmpty() && QFile::exists(sc)) {
            QFile f(sc);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
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
                        QStringLiteral("Loaded %1 annotation(s) from %2%3")
                            .arg(m_annByPath[path].size()).arg(QFileInfo(sc).fileName(),
                                 canon.isEmpty() ? QString()
                                 : QStringLiteral(" — saved orientation available (Image ▸ Apply Saved Orientation)")), 6000);
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

    updateDisplay();
    // Preserve zoom/pan when stepping between images of identical geometry so a
    // zoomed-in region stays put for comparison; otherwise fit the new image.
    if (m_image.width() != m_lastW || m_image.height() != m_lastH)
        m_view->zoomToFit();
    m_lastW = m_image.width();
    m_lastH = m_image.height();

    const QString name = QFileInfo(path).fileName();
    setWindowTitle(QStringLiteral("NebulaScope \u2014 %1").arg(name));
    statusBar()->showMessage(QStringLiteral("%1   %2\u00d7%3   %4 ch   [%5/%6]")
        .arg(name).arg(m_image.width()).arg(m_image.height()).arg(m_image.channels())
        .arg(m_fileList->currentRow() + 1).arg(m_fileList->count()), 4000);
}

// Save the CURRENT VIEW's non-linear edit as data: the stretch (window +
// transfer + colormap for mono) is baked into Float32 [0,1] pixels at full
// precision — unlike Export View As…, which quantises to 8-bit for pictures.
void MainWindow::saveStretched() {
    if (!m_image.isValid()) return;
    const QString path = QFileDialog::getSaveFileName(
        this, "Save stretched image", QString(),
        "FITS (*.fits);;XISF (*.xisf);;TIFF 16-bit (*.tiff)");
    if (path.isEmpty()) return;
    ImageData baked = DisplayRenderer::renderFloat(m_image, m_model);
    if (!baked.isValid()) { QMessageBox::warning(this, "Save failed", "Could not bake the stretch."); return; }
    ImageHeader hdr = m_header;
    hdr.cards.push_back({ QStringLiteral("HISTORY"),
                          QStringLiteral("NebulaScope: baked display stretch"), QString() });
    io::SaveResult sr = io::saveImage(path, baked, hdr);
    if (!sr.ok) QMessageBox::warning(this, "Save failed", sr.error);
    else statusBar()->showMessage("Saved stretched " + QFileInfo(path).fileName(), 3000);
}

void MainWindow::saveFile() {
    if (!m_image.isValid()) return;
    const QString path = QFileDialog::getSaveFileName(
        this, "Save image", QString(), "FITS (*.fits);;XISF (*.xisf);;TIFF 16-bit (*.tiff)");
    if (path.isEmpty()) return;
    io::SaveResult sr = io::saveImage(path, m_image, m_header);
    if (!sr.ok) { QMessageBox::warning(this, "Save failed", sr.error); return; }
    statusBar()->showMessage("Saved " + QFileInfo(path).fileName(), 3000);

    // A synthetic (in-memory) image that was just written to disk becomes that
    // file: rebrand its list row and migrate per-image state to the new key, so
    // the entry's identifier is the saved name from here on.
    if (m_currentPath.startsWith(QLatin1String("mem://"))) {
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
        rememberRecent(QStringLiteral("recentImages"), path, Preferences::get().recentImagesMax);
    }
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
    saveRenderedImage(DisplayRenderer::render(m_image, m_model), "Export view (full frame)",
        [this] { return floatToRgb64(DisplayRenderer::renderFloat(m_image, m_model)); });
}

void MainWindow::exportRegion() {
    if (!m_image.isValid()) return;
    const QRect roi = m_view->visibleImageRect();
    if (roi.isEmpty()) {
        QMessageBox::information(this, "Export region", "Nothing is visible to export.");
        return;
    }
    // Render the whole frame, then crop to the currently visible image pixels.
    const QImage full = DisplayRenderer::render(m_image, m_model);
    saveRenderedImage(full.copy(roi.intersected(full.rect())), "Export zoomed region",
        [this, roi] {
            QImage f16 = floatToRgb64(DisplayRenderer::renderFloat(m_image, m_model));
            return f16.copy(roi.intersected(f16.rect()));
        });
}

void MainWindow::saveRenderedImage(const QImage& img, const QString& title,
                                   const std::function<QImage()>& make16) {
    if (img.isNull()) return;
    const QString path = QFileDialog::getSaveFileName(
        this, title, QString(), "PNG (*.png);;JPEG (*.jpg);;TIFF (*.tiff);;WebP (*.webp)");
    if (path.isEmpty()) return;
    const QString ext = QFileInfo(path).suffix().toLower();

    int quality = -1;                                  // -1 = format default
    QImage toSave = img;
    if (ext == "jpg" || ext == "jpeg") {
        static int lastQuality = 90;                   // remembered for the session
        bool ok = false;
        const int q = QInputDialog::getInt(this, "JPEG quality",
            "Quality (1\u2013100):", lastQuality, 1, 100, 1, &ok);
        if (!ok) return;
        lastQuality = quality = q;
    } else if ((ext == "png" || ext == "tiff" || ext == "tif") && make16) {
        static int lastDepth = 0;                      // 0 = 8-bit
        bool ok = false;
        const QStringList depths{ "8-bit per channel", "16-bit per channel" };
        const QString depth = QInputDialog::getItem(this, "Bit depth",
            "Pixel depth:", depths, lastDepth, false, &ok);
        if (!ok) return;
        lastDepth = depth.startsWith("16") ? 1 : 0;
        if (lastDepth == 1) {
            toSave = make16();
            if (toSave.isNull()) { QMessageBox::warning(this, "Export failed", "16-bit render failed."); return; }
        }
    }

    if (!toSave.save(path, nullptr, quality)) {
        QMessageBox::warning(this, "Export failed",
                             QStringLiteral("Could not write %1").arg(QFileInfo(path).fileName()));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Exported %1 (%2\u00d7%3%4)")
        .arg(QFileInfo(path).fileName()).arg(toSave.width()).arg(toSave.height())
        .arg(toSave.format() == QImage::Format_RGBX64 ? QStringLiteral(" \u00b7 16-bit") : QString()), 3000);
}

void MainWindow::updateDisplay() {
    if (!m_image.isValid()) return;
    m_view->setDisplayImage(DisplayRenderer::render(m_image, m_model));
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
        statusBar()->showMessage(QStringLiteral("Click an annotation first to copy it"), 3000);
        return;
    }
    m_copiedAnn = anns[std::size_t(idx)];
    m_hasCopiedAnn = true;
    QApplication::clipboard()->setText(QString::fromUtf8(
        QJsonDocument(m_copiedAnn.toJson()).toJson(QJsonDocument::Compact)));
    statusBar()->showMessage(QStringLiteral("Copied %1")
        .arg(m_copiedAnn.label.isEmpty() ? QStringLiteral("annotation") : m_copiedAnn.label), 3000);
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
    pushAnnotationEdit(QStringLiteral("paste annotation"), m_currentPath, std::move(before));
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
    pushAnnotationEdit(QStringLiteral("delete annotation"), m_currentPath, std::move(before));
}

// Double-click editor: one small dialog for an annotation's text and colour.
void MainWindow::editAnnotationDialog(int annIdx) {
    auto it = m_annByPath.find(m_currentPath);
    if (it == m_annByPath.end() || annIdx < 0 || annIdx >= int(it.value().size())) return;
    Annotation& cur = it.value()[std::size_t(annIdx)];

    QDialog dlg(this);
    dlg.setWindowTitle("Edit annotation");
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
        const QColor c = QColorDialog::getColor(chosen, &dlg, "Annotation colour");
        if (c.isValid()) { chosen = c; setSwatch(c); }
    });
    auto* row1 = new QHBoxLayout();
    row1->addWidget(new QLabel("Text:"));
    row1->addWidget(edit, 1);
    auto* row2 = new QHBoxLayout();
    row2->addWidget(new QLabel("Colour:"));
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
    pushAnnotationEdit(QStringLiteral("edit annotation"), m_currentPath, std::move(before));
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
        this, "Import SExtractor catalog", QString(),
        "SExtractor catalogs (*.cat *.txt);;All files (*)");
    if (path.isEmpty()) return;

    QString err;
    const SexCatalog cat = SexCatalog::parse(path, &err);
    if (!cat.isValid()) { QMessageBox::warning(this, "Import failed", err); return; }
    if (!cat.has("X_IMAGE") || !cat.has("Y_IMAGE")) {
        QMessageBox::warning(this, "Import failed",
            "Catalog has no X_IMAGE/Y_IMAGE columns — add them to default.param.");
        return;
    }

    // Options dialog.
    QDialog dlg(this);
    dlg.setWindowTitle("Import SExtractor catalog");
    auto* form = new QVBoxLayout(&dlg);
    form->addWidget(new QLabel(QStringLiteral("%1 source(s), %2")
        .arg(cat.rowCount()).arg(QFileInfo(path).fileName())));
    auto* scaleRow = new QHBoxLayout();
    scaleRow->addWidget(new QLabel("Ellipse scale × A/B_IMAGE:"));
    auto* scale = new QDoubleSpinBox();
    scale->setRange(0.5, 20.0); scale->setSingleStep(0.5); scale->setValue(3.0);
    scaleRow->addWidget(scale, 1);
    form->addLayout(scaleRow);
    auto* labelRow = new QHBoxLayout();
    labelRow->addWidget(new QLabel("Label with:"));
    auto* labelBy = new QComboBox();
    labelBy->addItem("None");
    if (cat.has("NUMBER"))   labelBy->addItem("NUMBER");
    if (cat.has("MAG_AUTO")) labelBy->addItem("MAG_AUTO");
    labelRow->addWidget(labelBy, 1);
    form->addLayout(labelRow);
    auto* cleanOnly = new QCheckBox("Skip flagged sources (FLAGS ≠ 0)");
    cleanOnly->setEnabled(cat.has("FLAGS"));
    form->addWidget(cleanOnly);
    auto* classColor = new QCheckBox("Colour stars gold (CLASS_STAR > 0.9)");
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
    pushAnnotationEdit(QStringLiteral("import SExtractor catalog"), m_currentPath, std::move(before));
    statusBar()->showMessage(QStringLiteral("Imported %1 source(s)%2")
        .arg(added).arg(skipped ? QStringLiteral(", skipped %1 flagged").arg(skipped) : QString()), 4000);
}

// Warn when annotation edits would be lost on quit.
void MainWindow::closeEvent(QCloseEvent* e) {
    if (m_annDirty.isEmpty()) { e->accept(); return; }
    const auto btn = QMessageBox::warning(this, "Unsaved annotations",
        QStringLiteral("Annotations on %1 image(s) have not been saved.\n"
                       "Use Save Annotations\u2026 (right-click the image) to keep them.\n\n"
                       "Quit anyway?").arg(m_annDirty.size()),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (btn == QMessageBox::Discard) e->accept();
    else e->ignore();
}

bool MainWindow::writeAnnotationsFile(const QString& path) {
    const auto& anns = m_annByPath.value(m_currentPath);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save failed", "Could not write " + path);
        return false;
    }
    QJsonDocument doc = AnnotationLayer::toJson(anns);
    // Record the image orientation these annotations refer to, so a fresh
    // session can rotate/flip the reloaded image back into agreement.
    const QStringList ops = m_xformByPath.value(m_currentPath);
    if (!ops.isEmpty()) {
        QJsonObject root = doc.object();
        QJsonArray arr;
        for (const QString& o : ops) arr.append(o);
        root["orientation"] = arr;
        doc.setObject(root);
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    m_annDirty.remove(m_currentPath);
    statusBar()->showMessage(QStringLiteral("Saved %1 annotation(s) to %2")
                                 .arg(anns.size()).arg(QFileInfo(path).fileName()), 3000);
    return true;
}

// Silent save: overwrite the image's sidecar ("<image>_annotation.json") — the
// file displayPath() auto-loads. Falls back to the dialog for in-memory images.
void MainWindow::saveAnnotations() {
    if (m_annByPath.value(m_currentPath).empty()) return;
    const QString sc = annotationSidecar(m_currentPath);
    if (sc.isEmpty()) { saveAnnotationsAs(); return; }
    writeAnnotationsFile(sc);
}

void MainWindow::saveAnnotationsAs() {
    if (m_annByPath.value(m_currentPath).empty()) return;
    const QString sc = annotationSidecar(m_currentPath);
    const QString suggest = sc.isEmpty() ? QStringLiteral("annotation.json") : sc;
    const QString path = QFileDialog::getSaveFileName(
        this, "Save annotations as", suggest, "Annotations (*_annotation.json *.json)");
    if (path.isEmpty()) return;
    if (!sc.isEmpty() && QFileInfo(path).absoluteFilePath() == QFileInfo(sc).absoluteFilePath()) {
        QMessageBox::information(this, "Save annotations as",
            "That is the image's default sidecar — plain Save Annotations writes it directly.");
    }
    writeAnnotationsFile(path);
}

void MainWindow::loadAnnotations() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Load annotations", QString(), "Annotations (*_annotation.json *.json)");
    if (path.isEmpty()) return;
    loadAnnotationsFile(path);
}

void MainWindow::loadAnnotationsFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Load failed", "Could not read " + path);
        return;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, "Load failed", "JSON error: " + perr.errorString());
        return;
    }
    QString err;
    std::vector<Annotation> anns = AnnotationLayer::fromJson(doc, &err);
    if (anns.empty()) {
        QMessageBox::warning(this, "Load failed", err.isEmpty() ? QStringLiteral("No annotations in file") : err);
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
    pushAnnotationEdit(QStringLiteral("load annotations"), m_currentPath, std::move(before));
    rememberRecent(QStringLiteral("recentJson"), path, Preferences::get().recentJsonMax);
    statusBar()->showMessage(QStringLiteral("Loaded %1 annotation(s)").arg(m_annByPath[m_currentPath].size()), 3000);
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
            QAction* a = menu->addAction(QStringLiteral("%1 \u2014 %2")
                .arg(QFileInfo(p).fileName(), QFileInfo(p).absolutePath()));
            connect(a, &QAction::triggered, this, [opener, p] { opener(p); });
        }
        menu->setEnabled(!lst.isEmpty());
        if (!lst.isEmpty()) {
            menu->addSeparator();
            QAction* clr = menu->addAction(QStringLiteral("Clear List"));
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

void MainWindow::onEllipseDrawn(double cx, double cy, double a, double b) {
    if (!m_image.isValid()) return;
    bool ok = false;
    const QString label = QInputDialog::getText(this, "Ellipse annotation",
        "Label (optional):", QLineEdit::Normal, QString(), &ok);
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
    pushAnnotationEdit(QStringLiteral("add ellipse"), m_currentPath, std::move(before));
}

void MainWindow::onLineDrawn(double x1, double y1, double x2, double y2) {
    if (!m_image.isValid()) return;
    bool ok = false;
    const QString label = QInputDialog::getText(this, "Line annotation",
        "Label (optional):", QLineEdit::Normal, QString(), &ok);
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
    pushAnnotationEdit(QStringLiteral("add line"), m_currentPath, std::move(before));
}

void MainWindow::onTextPointPicked(double x, double y) {
    if (!m_image.isValid()) return;
    bool ok = false;
    const QString text = QInputDialog::getText(this, "Text annotation",
        "Text:", QLineEdit::Normal, QString(), &ok);
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
    pushAnnotationEdit(QStringLiteral("add text"), m_currentPath, std::move(before));
}

void MainWindow::onImageContextMenu(const QPoint& globalPos, int x, int y, bool onImage) {
    QMenu menu(this);

    double ra = 0, dec = 0;
    const bool sky = onImage && m_wcs.pixelToSky(x, y, ra, dec);
    const QString raS = sky ? Wcs::formatRa(ra) : QString();
    const QString decS = sky ? Wcs::formatDec(dec) : QString();
    QAction* aSky = menu.addAction(sky ? QStringLiteral("Copy RA/Dec \u2014 %1 %2").arg(raS, decS)
                                       : QStringLiteral("Copy RA/Dec (no astrometric solution)"));
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
    QAction* aPix = menu.addAction(QStringLiteral("Copy Pixel Value"));
    aPix->setEnabled(!pixText.isEmpty());

    menu.addSeparator();
    // Editing actions for the annotation under the cursor, if any.
    const int annIdx = onImage ? m_annotations->hitTest(QPointF(x + 0.5, y + 0.5)) : -1;
    QAction* aEditText = nullptr; QAction* aEditColor = nullptr; QAction* aDelete = nullptr;
    QAction* aCopyAnn = nullptr;
    if (annIdx >= 0 && annIdx < int(m_annByPath[m_currentPath].size())) {
        const Annotation& cur = m_annByPath[m_currentPath][std::size_t(annIdx)];
        const QString what = cur.label.isEmpty() ? QStringLiteral("annotation")
                                                 : QStringLiteral("\u201c%1\u201d").arg(cur.label);
        aEditText  = menu.addAction(QStringLiteral("Edit Text of %1\u2026").arg(what));
        aEditColor = menu.addAction(QStringLiteral("Change Colour of %1\u2026").arg(what));
        aDelete    = menu.addAction(QStringLiteral("Delete %1").arg(what));
        menu.addSeparator();
    }
    // Copy targets the SELECTED annotation (the one showing handles) when there
    // is one; otherwise whatever sits under the cursor. Labels can overhang
    // their neighbours, so the cursor hit alone was unreliable.
    const int copyIdx = (m_annotations->activeIndex() >= 0) ? m_annotations->activeIndex() : annIdx;
    if (copyIdx >= 0 && copyIdx < int(m_annByPath[m_currentPath].size())) {
        const Annotation& cc = m_annByPath[m_currentPath][std::size_t(copyIdx)];
        const QString ccName = cc.label.isEmpty() ? QStringLiteral("annotation")
                                                  : QStringLiteral("\u201c%1\u201d").arg(cc.label);
        aCopyAnn = menu.addAction(QStringLiteral("Copy %1").arg(ccName));
    }
    QAction* aAnnotate = menu.addAction(QStringLiteral("Annotate Here\u2026"));
    aAnnotate->setEnabled(onImage);
    QAction* aPasteAnn = menu.addAction(QStringLiteral("Paste Annotation Here"));
    aPasteAnn->setEnabled(onImage && m_hasCopiedAnn);
    const bool hasAnn = !m_annByPath.value(m_currentPath).empty();
    QAction* aClearAnn = menu.addAction(QStringLiteral("Clear Annotations"));
    aClearAnn->setEnabled(hasAnn);
    QAction* aSaveAnn = menu.addAction(QStringLiteral("Save Annotations"));
    aSaveAnn->setEnabled(hasAnn);
    QAction* aSaveAnnAs = menu.addAction(QStringLiteral("Save Annotations As\u2026"));
    aSaveAnnAs->setEnabled(hasAnn);
    QAction* aLoadAnn = menu.addAction(QStringLiteral("Load Annotations\u2026"));
    QAction* aInvAnn = menu.addAction(QStringLiteral("Invert Annotation Contrast"));
    aInvAnn->setCheckable(true);
    aInvAnn->setChecked(m_annotations->invertedContrast());

    // --- lookup section (needs an astrometric solution) ---
    menu.addSeparator();
    QAction* aAladin = nullptr;
    QAction* aSimbad = nullptr;
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
            aAladin = menu.addAction(QStringLiteral("Look up in Aladin — %1").arg(where));
            aSimbad = menu.addAction(QStringLiteral("Identify in SIMBAD — %1").arg(where));
            aSimbad->setData(radiusArcmin);
        }
    }

    menu.addSeparator();
    QAction* aFit = menu.addAction(QStringLiteral("Zoom to Fit"));
    QAction* a11  = menu.addAction(QStringLiteral("Zoom 1:1"));

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;
    if (chosen == aSky)      QApplication::clipboard()->setText(raS + QLatin1Char(' ') + decS);
    else if (chosen == aPix) QApplication::clipboard()->setText(pixText);
    else if (chosen == aAnnotate) {
        bool ok = false;
        const QString label = QInputDialog::getText(this, "Annotate",
            sky ? QStringLiteral("Label for %1 %2:").arg(raS, decS)
                : QStringLiteral("Label for pixel (%1, %2):").arg(x).arg(y),
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
            pushAnnotationEdit(QStringLiteral("add annotation"), m_currentPath, std::move(before));
        }
    }
    else if (aEditText && chosen == aEditText) {
        Annotation& cur = m_annByPath[m_currentPath][std::size_t(annIdx)];
        bool ok = false;
        const QString t = QInputDialog::getText(this, "Edit annotation text",
            "Text:", QLineEdit::Normal, cur.label, &ok);
        if (ok) {
            std::vector<Annotation> before = m_annByPath.value(m_currentPath);
            cur.label = t.trimmed();
            m_annDirty.insert(m_currentPath);
            refreshAnnotations();
            pushAnnotationEdit(QStringLiteral("edit annotation text"), m_currentPath, std::move(before));
        }
    }
    else if (aEditColor && chosen == aEditColor) {
        Annotation& cur = m_annByPath[m_currentPath][std::size_t(annIdx)];
        const QColor c = QColorDialog::getColor(cur.color, this, "Annotation colour");
        if (c.isValid()) {
            std::vector<Annotation> before = m_annByPath.value(m_currentPath);
            cur.color = c;
            m_annDirty.insert(m_currentPath);
            refreshAnnotations();
            pushAnnotationEdit(QStringLiteral("change annotation colour"), m_currentPath, std::move(before));
        }
    }
    else if (aDelete && chosen == aDelete) {
        auto& anns = m_annByPath[m_currentPath];
        std::vector<Annotation> before = m_annByPath.value(m_currentPath);
        anns.erase(anns.begin() + annIdx);
        m_annDirty.insert(m_currentPath);
        refreshAnnotations();
        pushAnnotationEdit(QStringLiteral("delete annotation"), m_currentPath, std::move(before));
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
        pushAnnotationEdit(QStringLiteral("paste annotation"), m_currentPath, std::move(before));
    }
    else if (chosen == aClearAnn) {
        std::vector<Annotation> before = m_annByPath.value(m_currentPath);
        m_annByPath.remove(m_currentPath);
        m_annDirty.insert(m_currentPath);
        refreshAnnotations();
        pushAnnotationEdit(QStringLiteral("clear annotations"), m_currentPath, std::move(before));
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
    if (!valid) { m_pixelLabel->setText("—"); return; }
    QString txt;
    if (m_image.channels() >= 3)
        txt = QString("(%1, %2)   R %3  G %4  B %5")
            .arg(x).arg(y).arg(r, 0, 'g', 5).arg(g, 0, 'g', 5).arg(b, 0, 'g', 5);
    else
        txt = QString("(%1, %2)   %3").arg(x).arg(y).arg(r, 0, 'g', 5);
    double ra = 0, dec = 0;
    if (m_wcs.pixelToSky(x, y, ra, dec))
        txt += QStringLiteral("   ·   %1  %2").arg(Wcs::formatRa(ra), Wcs::formatDec(dec));
    m_pixelLabel->setText(txt);
}

} // namespace astro

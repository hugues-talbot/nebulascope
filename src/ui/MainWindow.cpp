#include "ui/MainWindow.h"
#include "ui/ImageView.h"
#include "ui/HistogramPanel.h"
#include "ui/InfoPanel.h"
#include "ui/CombineDialog.h"
#include "io/ImageReader.h"
#include "app/AppInfo.h"
#include "io/ImageWriter.h"
#include "core/ImageStats.h"
#include "render/DisplayRenderer.h"
#include "core/Colormap.h"
#include "core/Transform.h"

#include <QDockWidget>
#include <QListWidget>
#include <QApplication>
#include <QMenu>
#include <algorithm>
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
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QWidget>
#include <QHBoxLayout>

namespace astro {

MainWindow::MainWindow() {
    setWindowTitle("NebulaScope — Inspector");
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
    m_view = new ImageView(this);
    m_view->setSource(&m_image);
    setCentralWidget(m_view);
    connect(m_view, &ImageView::pixelHovered, this, &MainWindow::onPixelHovered);

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

void MainWindow::applyTransform(Xform x) {
    if (!m_image.isValid()) return;
    switch (x) {
        case Xform::RotCW:  m_image = rotate90(m_image, true);  break;
        case Xform::RotCCW: m_image = rotate90(m_image, false); break;
        case Xform::FlipH:  m_image = flipHorizontal(m_image);  break;
        case Xform::FlipV:  m_image = flipVertical(m_image);    break;
    }
    // Values are unchanged, so stretch/stats stay valid; only geometry differs.
    m_view->setSource(&m_image);
    updateDisplay();
    const bool rotated = (x == Xform::RotCW || x == Xform::RotCCW);
    if (rotated) { m_view->zoomToFit(); m_lastW = m_image.width(); m_lastH = m_image.height(); }
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
    for (auto it = acts.cbegin(); it != acts.cend(); ++it)
        if (!s.contains(it.key()))
            s.setValue(it.key(), it.value()->shortcut().toString(QKeySequence::PortableText));
    for (auto it = keys.cbegin(); it != keys.cend(); ++it)
        if (!s.contains(it.key()))
            s.setValue(it.key(), it.value()->key().toString(QKeySequence::PortableText));
    for (auto it = acts.cbegin(); it != acts.cend(); ++it)
        it.value()->setShortcut(QKeySequence::fromString(s.value(it.key()).toString(), QKeySequence::PortableText));
    for (auto it = keys.cbegin(); it != keys.cend(); ++it)
        it.value()->setKey(QKeySequence::fromString(s.value(it.key()).toString(), QKeySequence::PortableText));
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

void MainWindow::buildMenusAndToolbar() {
    QHash<QString, QAction*> acts;      // registry for user-configurable shortcuts
    QHash<QString, QShortcut*> keys;

    // File
    QMenu* file = menuBar()->addMenu("&File");
    acts["open"] = file->addAction("&Open…", QKeySequence::Open, this, &MainWindow::openFile);
    acts["save_data_as"] = file->addAction("&Save Data As…", QKeySequence::SaveAs, this, &MainWindow::saveFile);
    acts["export_view"] = file->addAction("&Export View As…", QKeySequence("Ctrl+E"), this, &MainWindow::exportView);
    acts["export_region"] = file->addAction("Export &Zoomed Region As…", QKeySequence("Ctrl+Shift+E"), this, &MainWindow::exportRegion);
    file->addSeparator();
    acts["export_list"] = file->addAction("Export Image &List…", this, &MainWindow::exportList);
    acts["import_list"] = file->addAction("&Import Image List…", this, &MainWindow::importList);
    file->addSeparator();
    file->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);

    // View
    QMenu* view = menuBar()->addMenu("&View");
    QAction* aLeft = m_leftDock->toggleViewAction();
    aLeft->setShortcut(QKeySequence("F2"));
    QAction* aRight = m_rightDock->toggleViewAction();
    aRight->setShortcut(QKeySequence("F3"));
    QAction* aInfo = m_infoDock->toggleViewAction();
    aInfo->setShortcut(QKeySequence("F4"));
    view->addAction(aLeft);
    view->addAction(aInfo);
    view->addAction(aRight);
    acts["toggle_image_list"] = aLeft;
    acts["toggle_info_panel"] = aInfo;
    acts["toggle_histogram"]  = aRight;
    view->addSeparator();
    acts["zoom_to_fit"] = view->addAction("Zoom to &Fit", QKeySequence("F"), m_view, &ImageView::zoomToFit);
    // QKeySequence::FullScreen is the platform-correct binding (⌃⌘F on macOS —
    // F11 there is taken by the system — and F11 on Windows/Linux).
    acts["fullscreen"] = view->addAction("&Fullscreen", QKeySequence::FullScreen, this, [this] {
        isFullScreen() ? showNormal() : showFullScreen();
    });
    acts["image_only"] = view->addAction("&Image Only", QKeySequence("Tab"), this, &MainWindow::toggleImageOnly);
    auto* esc = new QShortcut(QKeySequence("Esc"), this);
    connect(esc, &QShortcut::activated, this, [this] { if (m_imageOnly) toggleImageOnly(); });

    // Image — lossless 90° rotations and flips (applied to the pixel data).
    QMenu* image = menuBar()->addMenu("&Image");
    acts["rotate_cw"]  = image->addAction("Rotate 90\u00b0 CW",  QKeySequence("]"),       this, [this]{ applyTransform(Xform::RotCW); });
    acts["rotate_ccw"] = image->addAction("Rotate 90\u00b0 CCW", QKeySequence("["),       this, [this]{ applyTransform(Xform::RotCCW); });
    image->addSeparator();
    acts["flip_horizontal"] = image->addAction("Flip &Horizontal", QKeySequence("Ctrl+H"), this, [this]{ applyTransform(Xform::FlipH); });
    acts["flip_vertical"]   = image->addAction("Flip &Vertical",   QKeySequence("Ctrl+J"), this, [this]{ applyTransform(Xform::FlipV); });

    // Stretch — transfer the current image's stretch to others in the list.
    QMenu* stretch = menuBar()->addMenu("&Stretch");
    acts["copy_stretch"] = stretch->addAction("&Copy Stretch", QKeySequence("Ctrl+Shift+C"), this, &MainWindow::copyStretch);
    acts["paste_stretch_normalized"] = stretch->addAction("&Paste Stretch (Normalized)", QKeySequence("Ctrl+Shift+V"), this, [this]{ pasteStretchToSelected(true); });
    acts["paste_stretch_absolute"] = stretch->addAction("Paste Stretch (&Absolute)", QKeySequence("Ctrl+Alt+Shift+V"), this, [this]{ pasteStretchToSelected(false); });
    stretch->addSeparator();
    acts["paste_stretch_all"] = stretch->addAction("Paste Stretch to &All", this, [this]{ pasteStretchToAll(true); });

    // Tools — pixel-math utilities.
    QMenu* tools = menuBar()->addMenu("&Tools");
    acts["combine_channels"] = tools->addAction("&Combine Channels…", this, &MainWindow::combineChannels);

    // Help — the About action carries AboutRole, so on macOS Qt moves it into
    // the application menu (“NebulaScope ▸ About NebulaScope”) automatically.
    QMenu* help = menuBar()->addMenu("&Help");
    help->addAction("Configure &Shortcuts…", this, &MainWindow::showShortcutSettings);
    QAction* about = help->addAction("&About NebulaScope", this, &MainWindow::showAbout);
    about->setMenuRole(QAction::AboutRole);
    QAction* aboutQt = help->addAction("About &Qt", qApp, &QApplication::aboutQt);
    aboutQt->setMenuRole(QAction::AboutQtRole);

    // Walk the loaded-image list: Space = next, Backspace = previous.
    auto* next = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(next, &QShortcut::activated, this, &MainWindow::nextImage);
    auto* prev = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    connect(prev, &QShortcut::activated, this, &MainWindow::prevImage);
    keys["next_image"] = next;
    keys["prev_image"] = prev;

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
    tb->addAction(aImageOnly);
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

// Append entries to the list without decoding. Selecting one (here or via the
// keyboard) is what triggers the actual load in showRow().
void MainWindow::addPaths(const QStringList& paths) {
    QListWidgetItem* firstAdded = nullptr;
    for (const QString& p : paths) {
        if (p.isEmpty()) continue;
        auto* it = new QListWidgetItem(QFileInfo(p).fileName(), m_fileList);
        it->setData(Qt::UserRole, p);
        it->setToolTip(p);
        if (!firstAdded) firstAdded = it;
    }
    // If nothing is displayed yet, show the first newly added file.
    if (m_fileList->currentRow() < 0 && firstAdded)
        m_fileList->setCurrentItem(firstAdded);   // fires currentRowChanged -> showRow
}

// Register an in-memory image (e.g. a channel combine) and show it. It gets a
// synthetic "mem://" key so displayPath() serves it from m_synthetic instead of
// touching the disk; Save Data As… can later write it to a real file.
void MainWindow::addSyntheticImage(const QString& name, ImageData&& img) {
    static int counter = 0;
    const QString key = QStringLiteral("mem://%1#%2").arg(name).arg(++counter);
    m_synthetic.insert(key, std::make_shared<ImageData>(std::move(img)));
    auto* it = new QListWidgetItem(name, m_fileList);
    it->setData(Qt::UserRole, key);
    it->setToolTip(name + "  (in-memory combine — use Save Data As… to keep)");
    m_fileList->setCurrentItem(it);               // triggers showRow -> displayPath
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
            io::LoadResult res = io::loadImage(p);
            if (!res.ok) continue;
            img = std::make_shared<ImageData>(std::move(res.image));
        }
        if (img && img->channels() == 1) mono.push_back({ item->text(), img });
    }
    if (mono.size() < 2) {
        QMessageBox::information(this, "Combine Channels",
            "Load at least two single-channel (mono) images into the list first.");
        return;
    }
    CombineDialog dlg(std::move(mono), this);
    if (dlg.exec() == QDialog::Accepted && dlg.hasResult()) {
        ImageData out = dlg.result();                 // copy out of the dialog
        addSyntheticImage(dlg.resultName(), std::move(out));
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
    // Forget any per-image stretch memory for removed paths, then delete rows.
    for (QListWidgetItem* it : sel) {
        const QString p = it->data(Qt::UserRole).toString();
        m_stfByPath.remove(p);
        m_synthetic.remove(p);                           // free any in-memory combine
        delete m_fileList->takeItem(m_fileList->row(it));
    }
    if (m_fileList->count() == 0) {
        m_currentPath.clear();
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
        io::LoadResult res = io::loadImage(path);        // promoteToFloat = true
        if (!res.ok) {
            QMessageBox::warning(this, "Open failed", res.error);
            return;
        }
        loaded = std::move(res.image);
        hdr    = std::move(res.header);
    }
    m_image = std::move(loaded);
    m_header = std::move(hdr);

    m_model.setChannelCount(m_image.channels());
    const std::vector<ChannelStats> stats = computeStats(m_image);
    m_curStats = stats;                                  // cache for Copy/Paste Stretch anchors

    // Per-image STF memory: restore this file's last stretch, or auto-stretch on
    // first visit. Set m_currentPath first so the change handler saves correctly.
    m_currentPath = path;
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

void MainWindow::saveFile() {
    if (!m_image.isValid()) return;
    const QString path = QFileDialog::getSaveFileName(
        this, "Save image", QString(), "FITS (*.fits);;XISF (*.xisf);;TIFF 16-bit (*.tiff)");
    if (path.isEmpty()) return;
    io::SaveResult sr = io::saveImage(path, m_image, m_header);
    if (!sr.ok) QMessageBox::warning(this, "Save failed", sr.error);
    else statusBar()->showMessage("Saved " + QFileInfo(path).fileName(), 3000);
}

void MainWindow::exportView() {
    if (!m_image.isValid()) return;
    // The exact 8-bit RGB image currently on screen — stretch, colormap and all.
    saveRenderedImage(DisplayRenderer::render(m_image, m_model), "Export view (full frame)");
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
    saveRenderedImage(full.copy(roi.intersected(full.rect())), "Export zoomed region");
}

void MainWindow::saveRenderedImage(const QImage& img, const QString& title) {
    if (img.isNull()) return;
    const QString path = QFileDialog::getSaveFileName(
        this, title, QString(), "PNG (*.png);;JPEG (*.jpg);;TIFF (*.tiff);;WebP (*.webp)");
    if (path.isEmpty()) return;
    if (!img.save(path)) {
        QMessageBox::warning(this, "Export failed",
                             QStringLiteral("Could not write %1").arg(QFileInfo(path).fileName()));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Exported %1 (%2\u00d7%3)")
        .arg(QFileInfo(path).fileName()).arg(img.width()).arg(img.height()), 3000);
}

void MainWindow::updateDisplay() {
    if (!m_image.isValid()) return;
    m_view->setDisplayImage(DisplayRenderer::render(m_image, m_model));
}

void MainWindow::toggleImageOnly() {
    m_imageOnly = !m_imageOnly;
    if (m_imageOnly) {
        m_savedLeft = m_leftDock->isVisible();
        m_savedRight = m_rightDock->isVisible();
        m_savedInfo = m_infoDock->isVisible();
        m_leftDock->hide();
        m_rightDock->hide();
        m_infoDock->hide();
        menuBar()->hide();
        statusBar()->hide();
        for (QToolBar* tb : findChildren<QToolBar*>()) tb->hide();
    } else {
        m_leftDock->setVisible(m_savedLeft);
        m_rightDock->setVisible(m_savedRight);
        m_infoDock->setVisible(m_savedInfo);
        menuBar()->show();
        statusBar()->show();
        for (QToolBar* tb : findChildren<QToolBar*>()) tb->show();
    }
}

void MainWindow::onPixelHovered(int x, int y, double r, double g, double b, bool valid) {
    if (!valid) { m_pixelLabel->setText("—"); return; }
    if (m_image.channels() >= 3)
        m_pixelLabel->setText(QString("(%1, %2)   R %3  G %4  B %5")
            .arg(x).arg(y).arg(r, 0, 'g', 5).arg(g, 0, 'g', 5).arg(b, 0, 'g', 5));
    else
        m_pixelLabel->setText(QString("(%1, %2)   %3").arg(x).arg(y).arg(r, 0, 'g', 5));
}

} // namespace astro

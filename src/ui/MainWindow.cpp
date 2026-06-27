#include "ui/MainWindow.h"
#include "ui/ImageView.h"
#include "ui/HistogramPanel.h"
#include "ui/InfoPanel.h"
#include "io/ImageReader.h"
#include "io/ImageWriter.h"
#include "core/ImageStats.h"
#include "render/DisplayRenderer.h"
#include "core/Colormap.h"

#include <QDockWidget>
#include <QListWidget>
#include <QApplication>
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
#include <QComboBox>
#include <QSlider>
#include <QWidget>
#include <QHBoxLayout>

namespace astro {

MainWindow::MainWindow() {
    setWindowTitle("NebulaScope — Inspector");
    buildUi();
    buildMenusAndToolbar();
    resize(1480, 940);
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

void MainWindow::showAbout() {
    QMessageBox box(this);
    box.setWindowTitle("About NebulaScope");
    box.setIconPixmap(windowIcon().pixmap(96, 96));
    box.setTextFormat(Qt::RichText);
    box.setText(
        "<h2 style='margin:0'>NebulaScope</h2>"
        "<p style='color:#9fabb8;margin:2px 0 10px'>Astronomical image inspector</p>"
        "<p>Interactive FITS / XISF / JPEG / PNG / TIFF viewer with precise RGB"
        " histogram control, Generalised Hyperbolic Stretch, false-colour maps,"
        " and blink comparison.</p>"
        "<p style='color:#7e8b98;font-size:11px'>Built with Qt, CFITSIO/CCfits and libXISF.</p>");
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

void MainWindow::buildMenusAndToolbar() {
    // File
    QMenu* file = menuBar()->addMenu("&File");
    file->addAction("&Open…", QKeySequence::Open, this, &MainWindow::openFile);
    file->addAction("&Save Data As…", QKeySequence::SaveAs, this, &MainWindow::saveFile);
    file->addAction("&Export View As…", QKeySequence("Ctrl+E"), this, &MainWindow::exportView);
    file->addAction("Export &Zoomed Region As…", QKeySequence("Ctrl+Shift+E"), this, &MainWindow::exportRegion);
    file->addSeparator();
    file->addAction("Export Image &List…", this, &MainWindow::exportList);
    file->addAction("&Import Image List…", this, &MainWindow::importList);
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
    view->addSeparator();
    QAction* aFit = view->addAction("Zoom to &Fit", QKeySequence("F"), m_view, &ImageView::zoomToFit);
    Q_UNUSED(aFit);
    QAction* aFull = view->addAction("&Fullscreen", QKeySequence("F11"), this, [this] {
        isFullScreen() ? showNormal() : showFullScreen();
    });
    Q_UNUSED(aFull);
    QAction* aImageOnly = view->addAction("&Image Only", QKeySequence("Tab"), this, &MainWindow::toggleImageOnly);
    Q_UNUSED(aImageOnly);
    auto* esc = new QShortcut(QKeySequence("Esc"), this);
    connect(esc, &QShortcut::activated, this, [this] { if (m_imageOnly) toggleImageOnly(); });

    // Help — the About action carries AboutRole, so on macOS Qt moves it into
    // the application menu (“NebulaScope ▸ About NebulaScope”) automatically.
    QMenu* help = menuBar()->addMenu("&Help");
    QAction* about = help->addAction("&About NebulaScope", this, &MainWindow::showAbout);
    about->setMenuRole(QAction::AboutRole);
    QAction* aboutQt = help->addAction("About &Qt", qApp, &QApplication::aboutQt);
    aboutQt->setMenuRole(QAction::AboutQtRole);

    // Walk the loaded-image list: Space = next, Backspace = previous.
    auto* next = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(next, &QShortcut::activated, this, &MainWindow::nextImage);
    auto* prev = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    connect(prev, &QShortcut::activated, this, &MainWindow::prevImage);

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

    // False-colour map for mono images.
    tb->addWidget(new QLabel(" Colormap "));
    m_cmapCombo = new QComboBox();
    for (int i = 0; i < kColormapCount; ++i)
        m_cmapCombo->addItem(colormapName(static_cast<Colormap>(i)));
    tb->addWidget(m_cmapCombo);
    connect(m_cmapCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        m_model.setColormap(static_cast<Colormap>(i));
        if (m_splitWidget)
            m_splitWidget->setVisible(static_cast<Colormap>(i) == Colormap::Split);
    });

    // Split break-point slider (visible only when the Split map is active).
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
        "Astronomy & images (*.fits *.fit *.fts *.fz *.xisf *.jpg *.jpeg *.png *.tif *.tiff);;All files (*)");
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

void MainWindow::appendToList() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, "Append image(s)", QString(),
        "Astronomy & images (*.fits *.fit *.fts *.fz *.xisf *.jpg *.jpeg *.png *.tif *.tiff);;All files (*)");
    if (!paths.isEmpty()) addPaths(paths);
}

void MainWindow::removeSelected() {
    const auto sel = m_fileList->selectedItems();
    if (sel.isEmpty()) return;
    // Forget any per-image stretch memory for removed paths, then delete rows.
    for (QListWidgetItem* it : sel) {
        const QString p = it->data(Qt::UserRole).toString();
        m_stfByPath.remove(p);
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
    io::LoadResult res = io::loadImage(path);            // promoteToFloat = true
    if (!res.ok) {
        QMessageBox::warning(this, "Open failed", res.error);
        return;
    }
    m_image = std::move(res.image);
    m_header = std::move(res.header);

    m_model.setChannelCount(m_image.channels());
    const std::vector<ChannelStats> stats = computeStats(m_image);

    // Per-image STF memory: restore this file's last stretch, or auto-stretch on
    // first visit. Set m_currentPath first so the change handler saves correctly.
    m_currentPath = path;
    auto remembered = m_stfByPath.constFind(path);
    if (remembered != m_stfByPath.constEnd() && remembered.value().valid)
        m_model.setState(remembered.value());            // re-apply remembered STF
    else
        m_model.autoStretch(stats);                      // first visit: auto STF

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
        if (m_splitSlider) {
            QSignalBlocker bs(m_splitSlider);
            m_splitSlider->setValue(int(m_model.splitThreshold() * 100));
        }
        if (m_splitWidget) m_splitWidget->setVisible(mono && cm == Colormap::Split);
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
        this, title, QString(), "PNG (*.png);;JPEG (*.jpg);;TIFF (*.tiff)");
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

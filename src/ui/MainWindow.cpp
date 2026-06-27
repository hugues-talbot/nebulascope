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

    // left dock: open images
    m_leftDock = new QDockWidget("Open Images", this);
    m_leftDock->setObjectName("leftDock");
    m_fileList = new QListWidget(m_leftDock);
    m_leftDock->setWidget(m_fileList);
    addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);
    connect(m_fileList, &QListWidget::currentRowChanged, this, &MainWindow::showRow);

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

void MainWindow::buildMenusAndToolbar() {
    // File
    QMenu* file = menuBar()->addMenu("&File");
    file->addAction("&Open…", QKeySequence::Open, this, &MainWindow::openFile);
    file->addAction("&Save As…", QKeySequence::SaveAs, this, &MainWindow::saveFile);
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
        "Astronomy images (*.fits *.fit *.fts *.fz *.xisf);;All files (*)");
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
        this, "Save image", QString(), "FITS (*.fits);;XISF (*.xisf)");
    if (path.isEmpty()) return;
    io::SaveResult sr = io::saveImage(path, m_image, m_header);
    if (!sr.ok) QMessageBox::warning(this, "Save failed", sr.error);
    else statusBar()->showMessage("Saved " + QFileInfo(path).fileName(), 3000);
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

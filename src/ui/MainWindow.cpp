#include "ui/MainWindow.h"
#include "ui/ImageView.h"
#include "ui/HistogramPanel.h"
#include "ui/InfoPanel.h"
#include "io/ImageReader.h"
#include "io/ImageWriter.h"
#include "core/ImageStats.h"
#include "render/DisplayRenderer.h"

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

    // Toolbar
    QToolBar* tb = addToolBar("Main");
    tb->setObjectName("mainToolbar");
    tb->setMovable(false);
    tb->addAction("Open", this, &MainWindow::openFile);
    tb->addAction("Save", this, &MainWindow::saveFile);
    tb->addSeparator();
    tb->addAction("Fit", m_view, &ImageView::zoomToFit);
    tb->addSeparator();
    tb->addAction(aLeft);
    tb->addAction(aRight);
    tb->addAction(aImageOnly);
}

void MainWindow::openFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open image", QString(),
        "Astronomy images (*.fits *.fit *.fts *.xisf);;All files (*)");
    if (!path.isEmpty()) loadPath(path);
}

void MainWindow::loadPath(const QString& path) {
    io::LoadResult res = io::loadImage(path);            // promoteToFloat = true
    if (!res.ok) {
        QMessageBox::warning(this, "Open failed", res.error);
        return;
    }
    m_image = std::move(res.image);
    m_header = std::move(res.header);

    m_model.setChannelCount(m_image.channels());
    const std::vector<ChannelStats> stats = computeStats(m_image);
    m_model.autoStretch(stats);                          // sets ranges + STF, emits changed()
    m_view->setSource(&m_image);
    m_hist->setSource(&m_image);
    m_info->setData(&m_image, &m_header, stats);
    updateDisplay();
    m_view->zoomToFit();

    auto* it = new QListWidgetItem(QFileInfo(path).fileName(), m_fileList);
    it->setData(Qt::UserRole, path);
    m_fileList->setCurrentItem(it);
    statusBar()->showMessage(QString("%1   %2×%3   %4 ch")
        .arg(QFileInfo(path).fileName()).arg(m_image.width()).arg(m_image.height()).arg(m_image.channels()), 4000);
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

#pragma once
//
// MainWindow — assembles the inspector: image list (left dock), image view
// (centre), histogram panel (right dock), toolbar, and a View menu that hides
// panels / toggles fullscreen / enters image-only mode. Wires the StretchModel
// to the renderer so any histogram edit updates the display live.
//
#include <QMainWindow>
#include "core/ImageData.h"
#include "core/ImageHeader.h"
#include "render/StretchModel.h"

class QDockWidget;
class QListWidget;
class QLabel;

namespace astro {

class ImageView;
class HistogramPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

private slots:
    void openFile();
    void saveFile();
    void updateDisplay();
    void toggleImageOnly();
    void onPixelHovered(int x, int y, double r, double g, double b, bool valid);

private:
    void buildUi();
    void buildMenusAndToolbar();
    void loadPath(const QString& path);

    ImageData      m_image;
    ImageHeader    m_header;
    StretchModel   m_model;

    ImageView*      m_view = nullptr;
    HistogramPanel* m_hist = nullptr;
    QDockWidget*    m_leftDock = nullptr;
    QDockWidget*    m_rightDock = nullptr;
    QListWidget*    m_fileList = nullptr;
    QLabel*         m_pixelLabel = nullptr;

    bool m_imageOnly = false;
    bool m_savedLeft = true, m_savedRight = true;
};

} // namespace astro

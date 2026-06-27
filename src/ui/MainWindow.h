#pragma once
//
// MainWindow — assembles the inspector: image list (left dock), image view
// (centre), histogram panel (right dock), toolbar, and a View menu that hides
// panels / toggles fullscreen / enters image-only mode. Wires the StretchModel
// to the renderer so any histogram edit updates the display live.
//
#include <QMainWindow>
#include <QHash>
#include "core/ImageData.h"
#include "core/ImageHeader.h"
#include "render/StretchModel.h"

class QDockWidget;
class QListWidget;
class QLabel;
class QComboBox;
class QSlider;
class QWidget;

namespace astro {

class ImageView;
class HistogramPanel;
class InfoPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

    // Register files (e.g. from the command line) without decoding; the first
    // becomes the displayed image if nothing is shown yet.
    void openPaths(const QStringList& paths);

private slots:
    void openFile();
    void saveFile();
    void updateDisplay();
    void toggleImageOnly();
    void onPixelHovered(int x, int y, double r, double g, double b, bool valid);
    void showRow(int row);      // decode + display the list item at row
    void nextImage();           // Space
    void prevImage();           // Backspace

private:
    void buildUi();
    void buildMenusAndToolbar();
    void addPaths(const QStringList& paths);   // append list items, no decode
    void displayPath(const QString& path);     // decode one file into the view

    ImageData      m_image;
    ImageHeader    m_header;
    StretchModel   m_model;

    ImageView*      m_view = nullptr;
    HistogramPanel* m_hist = nullptr;
    InfoPanel*      m_info = nullptr;
    QDockWidget*    m_leftDock = nullptr;
    QDockWidget*    m_rightDock = nullptr;
    QDockWidget*    m_infoDock = nullptr;
    QListWidget*    m_fileList = nullptr;
    QLabel*         m_pixelLabel = nullptr;
    QComboBox*      m_cmapCombo = nullptr;
    QWidget*        m_splitWidget = nullptr;
    QSlider*        m_splitSlider = nullptr;

    bool m_imageOnly = false;
    bool m_savedLeft = true, m_savedRight = true, m_savedInfo = true;

    // Last displayed image dimensions; used to keep zoom/pan when the next image
    // has the same geometry (so small regions stay aligned for comparison).
    int m_lastW = -1, m_lastH = -1;

    // Per-image stretch memory: each file remembers the last STF applied to it,
    // re-applied on revisit; first visit auto-stretches. Keyed by file path.
    QHash<QString, StretchModel::State> m_stfByPath;
    QString m_currentPath;
};

} // namespace astro

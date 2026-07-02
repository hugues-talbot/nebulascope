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
    void exportView();
    void exportRegion();
    void updateDisplay();
    void toggleImageOnly();
    void onPixelHovered(int x, int y, double r, double g, double b, bool valid);
    void showRow(int row);      // decode + display the list item at row
    void nextImage();           // Space
    void prevImage();           // Backspace
    void appendToList();        // + : pick files, append
    void removeSelected();      // − / Del : drop selected entries
    void exportList();          // write the list of paths to a text file
    void importList();          // read a list of paths from a text file
    void showAbout();           // About dialog (App menu on macOS)
    void copyStretch();         // capture current image's stretch
    void pasteStretchToSelected(bool normalized);   // apply to selected list rows
    void pasteStretchToAll(bool normalized);        // apply to every list row
    void onListContextMenu(const QPoint& pos);      // right-click on the image list

public:
    // Load a list file (one path per line; blanks and #-comments ignored) and
    // append its entries. Used by File▸Import List and the --list CLI flag.
    void importListFile(const QString& listPath);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;   // accept file drops
    void dropEvent(QDropEvent* e) override;

private:
    void buildUi();
    void buildMenusAndToolbar();
    void addPaths(const QStringList& paths);   // append list items, no decode
    void displayPath(const QString& path);     // decode one file into the view
    enum class Xform { RotCW, RotCCW, FlipH, FlipV };
    void applyTransform(Xform x);              // lossless geometry on the current image
    void applyCopiedStretch(const QString& path, bool normalized);  // paste onto one file
    void saveRenderedImage(const QImage& img, const QString& title);  // shared export dialog

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
    StretchModel::State m_copiedStretch;   // clipboard for Copy/Paste Stretch
    std::vector<ChannelStats> m_curStats;  // stats of the currently displayed image
};

} // namespace astro

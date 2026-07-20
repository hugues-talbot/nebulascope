#pragma once
//
// MainWindow — assembles the inspector: image list (left dock), image view
// (centre), histogram panel (right dock), toolbar, and a View menu that hides
// panels / toggles fullscreen / enters image-only mode. Wires the StretchModel
// to the renderer so any histogram edit updates the display live.
//
#include <QMainWindow>
#include <QHash>
#include <QSet>
#include <memory>
#include "core/ImageData.h"
#include "core/ImageHeader.h"
#include "core/Wcs.h"
#include "ui/AnnotationLayer.h"
#include "render/StretchModel.h"

class QDockWidget;
class QUndoStack;
class QListWidget;
class QLabel;
class QComboBox;
class QCheckBox;
class QShortcut;
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
    void onImageContextMenu(const QPoint& globalPos, int x, int y, bool onImage);
    void onEllipseDrawn(double cx, double cy, double a, double b);
    void onLineDrawn(double x1, double y1, double x2, double y2);
    void onTextPointPicked(double x, double y);
    void showRow(int row);      // decode + display the list item at row
    void nextImage();           // Space
    void prevImage();           // Backspace
    void appendToList();        // + : pick files, append
    void removeSelected();      // − / Del : drop selected entries
    void exportList();          // write the list of paths to a text file
    void importList();          // read a list of paths from a text file
    void showAbout();
    void showShortcutSettings();           // About dialog (App menu on macOS)
    void copyStretch();         // capture current image's stretch
    void pasteStretchToSelected(bool normalized);   // apply to selected list rows
    void pasteStretchToAll(bool normalized);        // apply to every list row
    void onListContextMenu(const QPoint& pos);      // right-click on the image list
    void combineChannels();                         // Tools ▸ Combine Channels…

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
    // Read/write ~/.../NebulaScope/shortcuts.ini: defaults are written on first
    // run, user edits override the hardcoded shortcuts at startup.
    void applyUserShortcuts(const QHash<QString, QAction*>& acts,
                            const QHash<QString, QShortcut*>& keys);
    void addPaths(const QStringList& paths);   // append list items, no decode
    void displayPath(const QString& path);     // decode one file into the view
    void addSyntheticImage(const QString& name, ImageData&& img);  // in-memory result → list
public:
    enum class Xform { RotCW, RotCCW, FlipH, FlipV };
    // Undo plumbing (used by the QUndoCommand classes in MainWindow.cpp):
    void doTransform(Xform x);                 // apply rotate/flip without pushing undo
    void setAnnotations(const QString& path, const std::vector<Annotation>& anns);
    const QString& currentPath() const { return m_currentPath; }
private:
    void applyTransform(Xform x);              // lossless geometry on the current image
    void applyCopiedStretch(const QString& path, bool normalized);  // paste onto one file
    void saveRenderedImage(const QImage& img, const QString& title);  // shared export dialog

    ImageData      m_image;
    ImageHeader    m_header;
    Wcs            m_wcs;                 // astrometric solution of the shown image
    StretchModel   m_model;

    AnnotationLayer* m_annotations = nullptr;
    QHash<QString, std::vector<Annotation>> m_annByPath;   // per-image annotations
    QSet<QString> m_annDirty;                              // edited since last save/load
    QUndoStack* m_undo = nullptr;
    QColor m_annColor = QColor("#8fc0f5");                 // colour for new annotations
    QAction* m_toolEllipse = nullptr;
    QAction* m_toolLine = nullptr;
    QAction* m_toolText = nullptr;
    void refreshAnnotations();            // rebuild the overlay for the shown image
    // Push an undo entry for an annotation edit already applied to m_annByPath;
    // `before` is the list as it was prior to the edit.
    void pushAnnotationEdit(const QString& text, const QString& path,
                            std::vector<Annotation> before);
    void saveAnnotations();               // write current image's annotations to JSON
    void loadAnnotations();               // read annotations from a JSON file

protected:
    void closeEvent(QCloseEvent* e) override;   // warn about unsaved annotations

    ImageView*      m_view = nullptr;
    HistogramPanel* m_hist = nullptr;
    InfoPanel*      m_info = nullptr;
    QDockWidget*    m_leftDock = nullptr;
    QDockWidget*    m_rightDock = nullptr;
    QDockWidget*    m_infoDock = nullptr;
    QListWidget*    m_fileList = nullptr;
    QLabel*         m_pixelLabel = nullptr;
    QComboBox*      m_cmapCombo = nullptr;
    QCheckBox*      m_invertCheck = nullptr;
    QCheckBox*      m_splitCheck = nullptr;
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
    QString m_shortcutFile;               // path of the user shortcuts INI
    StretchModel::State m_copiedStretch;   // clipboard for Copy/Paste Stretch
    std::vector<ChannelStats> m_curStats;  // stats of the currently displayed image

    // In-memory images produced in-app (channel combines) that have no file on
    // disk. Keyed by a synthetic "mem://name#n" path stored in the list item;
    // displayPath() serves these instead of hitting io::loadImage.
    QHash<QString, std::shared_ptr<ImageData>> m_synthetic;
};

} // namespace astro

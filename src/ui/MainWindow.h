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
#include <functional>
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
class ViewGrid;
class ViewCell;
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
    void saveStretched();   // bake the current stretch into Float32 data, save
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
    // Split the main view and assign the first rows*cols list entries to the
    // cells in raster order. Used by the --split CLI flag.
    void applySplitLayout(int rows, int cols);

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
    void doRotateArbitrary(double angleDeg);   // resampling rotation, no undo push
    void resetOrientation();                   // drop stored rotate/flip history, re-decode
    void transportColorsFromRef();             // sliced-OT colour transfer from a reference
    void applySavedOrientation();              // replay the sidecar's orientation on demand
    // Absolute rotation: restores the stashed pre-rotation base, then applies
    // ONE resample. Hunting for the right angle never degrades the image, and
    // undo/redo is exact (rotate back to the previous total from the same base).
    void rotateToAngle(double totalDeg);
    double currentRotationAngle() const;       // the "rot:" total in the history
    // Exact restore for RotateCmd::undo — an inverse rotation would re-resample
    // AND re-grow the canvas, so the command snapshots the state instead.
    void restoreImageState(const QString& path, const ImageData& img,
                           const std::vector<Annotation>& anns,
                           const Wcs& wcs, const QStringList& xformHist);
    void setAnnotations(const QString& path, const std::vector<Annotation>& anns);
    const QString& currentPath() const { return m_currentPath; }
private:
    void applyTransform(Xform x);              // lossless geometry on the current image
    void pushRotateTo(double totalDeg);        // rotateToAngle + undo command
    // Map annotations given in the DISK (as-loaded) pixel frame through this
    // image's orientation history, so imports line up with a rotated view.
    void mapAnnotationsFromDiskFrame(std::vector<Annotation>& anns);
    // Inverse: take annotations expressed in the frame described by `ops` back
    // to the disk frame (walk the op chain backwards with exact inverses).
    void unmapAnnotationsToDiskFrame(std::vector<Annotation>& anns, const QStringList& ops);
    QHash<QString, QSize> m_diskSizeByPath;    // as-decoded dims, pre-orientation
    // Pre-rotation base for rotateToAngle — captured lazily on the first
    // arbitrary rotation of an image, dropped on 90°/flip or image switch.
    ImageData m_rotBase;
    Wcs m_rotBaseWcs;
    QStringList m_rotBaseHist;
    QString m_rotBasePath;
    double m_rotBaseAngle = 0.0;
    void applyCopiedStretch(const QString& path, bool normalized);  // paste onto one file
    // Shared export dialog. make16, when given, supplies a 16-bit variant for
    // PNG/TIFF export (built from the float render, no 8-bit quantisation).
    void saveRenderedImage(const QImage& img, const QString& title,
                           const std::function<QImage()>& make16 = {});

    ImageData      m_image;
    ImageHeader    m_header;
    Wcs            m_wcs;                 // astrometric solution of the shown image
    StretchModel   m_model;

    AnnotationLayer* m_annotations = nullptr;
    QHash<QString, std::vector<Annotation>> m_annByPath;   // per-image annotations
    QHash<QString, QStringList> m_xformByPath;             // per-image rotate/flip history
    QHash<QString, QStringList> m_sidecarOrientByPath;     // sidecar orientation, NOT auto-applied
    QSet<QString> m_annDirty;                              // edited since last save/load
    QUndoStack* m_undo = nullptr;
    QColor m_annColor = QColor("#8fc0f5");                 // colour for new annotations
    Annotation m_copiedAnn;                                // clipboard for copy/paste
    bool m_hasCopiedAnn = false;
    int m_hoverX = 0, m_hoverY = 0;                        // last hovered image pixel
    bool m_hoverValid = false;
    QAction* m_toolEllipse = nullptr;
    QAction* m_toolLine = nullptr;
    QAction* m_toolText = nullptr;
    void refreshAnnotations();            // rebuild the overlay for the shown image
    void connectViewSignals(ImageView* v);           // per-view wiring (grid cells)
    void onCellSwap(ViewCell* oldC, ViewCell* newC); // active-cell state exchange
    ViewGrid* m_grid = nullptr;
    void ensureAnnotationsVisible();      // force the overlay on (load/import)
    QAction* m_annVisAct = nullptr;       // View ▸ Show Annotations (kept in sync)
    // Rotate/flip history per image: re-applied when the image reloads from
    // disk (blink-back or a fresh session via the annotation sidecar), so the
    // pixels always match annotations made in a transformed orientation.
    static QString xformName(Xform x);
    static bool xformFromName(const QString& n, Xform& out);
    // Collapse an orientation history into a minimal equivalent (merge adjacent
    // rotations, drop whole turns, cancel inverse pairs) — replaying a literal
    // rotate/counter-rotate pair from disk would bake in dead black borders.
    static QStringList canonicalXforms(QStringList ops);
    void reapplyStoredXforms();
    // Push an undo entry for an annotation edit already applied to m_annByPath;
    // `before` is the list as it was prior to the edit.
    void pushAnnotationEdit(const QString& text, const QString& path,
                            std::vector<Annotation> before);
    void saveAnnotations();               // silent save to the image's sidecar
    void saveAnnotationsAs();             // dialog for an explicit file name
    bool writeAnnotationsFile(const QString& path);   // shared writer
    void loadAnnotations();               // dialog, then loadAnnotationsFile
    void loadAnnotationsFile(const QString& path);   // read annotations from a JSON file
    // Recent-files history (persisted via QSettings): last 10 images, 5 JSONs.
    void rememberRecent(const QString& settingsKey, const QString& path, int max);
    void rebuildRecentMenus();
    QMenu* m_recentImagesMenu = nullptr;
    QMenu* m_recentJsonMenu = nullptr;
    void importSexCatalog();              // SExtractor catalog -> ellipse annotations
    void editAnnotationDialog(int annIdx);   // double-click: text + colour dialog
    void deleteActiveAnnotation();           // Delete key: selected (or latest) annotation
    void copySelectedAnnotation();           // Ctrl/Cmd+Shift+C
    void pasteAnnotationAtCursor();          // Ctrl/Cmd+Shift+V — at hover position

protected:
    void closeEvent(QCloseEvent* e) override;   // warn about unsaved annotations
    void keyPressEvent(QKeyEvent* e) override;  // fallback for the Delete key
    void resizeEvent(QResizeEvent* e) override; // reposition overlay panels
    bool eventFilter(QObject* o, QEvent* e) override;   // overlay edge-drag resize

    ImageView*      m_view = nullptr;
    HistogramPanel* m_hist = nullptr;
    InfoPanel*      m_info = nullptr;
    // Overlay-panel mode: the dock contents float translucently over the image.
    void setOverlayPanels(bool on);
    void layoutOverlayPanels();
    QWidget* makeOverlayBox(QWidget* content);
    bool m_overlay = false;
    QWidget* m_ovList = nullptr;
    QWidget* m_ovInfo = nullptr;
    QWidget* m_ovHist = nullptr;
    QWidget* m_listContent = nullptr;   // the left dock's content widget (list + buttons)
    // Overlay geometry (user-resizable by edge drag; persisted per session)
    int m_ovLeftW = 0;                  // 0 = auto
    int m_ovHistW = 0;
    double m_ovSplit = 0.55;            // list share of the left column
    int m_ovDrag = 0;                   // 0 none, 1 left width, 2 hist width, 3 split
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

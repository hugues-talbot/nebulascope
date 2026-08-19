#pragma once
//
// MainWindow — assembles the inspector: image list (left dock), image view
// (centre), histogram panel (right dock), toolbar, and a View menu that hides
// panels / toggles fullscreen / enters image-only mode. Wires the StretchModel
// to the renderer so any histogram edit updates the display live.
//
#include <QMainWindow>
#include <QColorTransform>
#include <QFutureWatcher>
#include <QImage>
#include <QHash>
#include <QSet>
#include <climits>
#include <QFileSystemWatcher>
#include <QTimer>
#include <functional>
#include <memory>
#include "core/ImageData.h"
#include "core/ImageHeader.h"
#include "core/Wcs.h"
#include "ui/AnnotationLayer.h"
#include "render/StretchModel.h"

class QDockWidget;
class QUndoStack;
class QNetworkAccessManager;
class QListWidget;
class QLabel;
class QComboBox;
class QCheckBox;
class QShortcut;
class QSlider;
class QWidget;

namespace astro {

namespace io { struct SaveOptions; }

class ImageView;
class HistogramPanel;
class ViewGrid;
class ViewCell;
class InfoPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
    friend class ScriptRunner;   // --run scripts drive the window directly
    friend struct StretchSquelch; // RAII: marks programmatic stretch changes
public:
    MainWindow();

    // Register files (e.g. from the command line) without decoding; the first
    // becomes the displayed image if nothing is shown yet.
    void openPaths(const QStringList& paths);

private slots:
    void openFile();
    void clearImageList();
    void reloadOriginal();
    void flushStretchUndo();
    void pushStretchUndo();
    void resyncStretchUndoBase();
    QString openDialogDir() const;
    void rememberOpenDialogDir(const QString& firstPath);
    void saveFile();
    void saveStretched();   // bake the current stretch into Float32 data, save
    void exportView();
    void exportRegion();
    void updateDisplay();
    void onRenderDone();                  // async render finished — show + maybe rerun
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
    void removeSelected();      // − / Del : close + drop selected entries
    void removeSelectedRows(bool promptForAnnotations);  // the guts; prompt=false for undo/scripts
    void exportList();          // write the list of paths to a text file
    void importList();          // read a list of paths from a text file
    void showAbout();
    void showShortcutSettings();           // About dialog (App menu on macOS)
    void copyStretch();         // capture current image's stretch
    void pasteStretchToSelected(bool normalized);   // apply to selected list rows
    void pasteStretchToAll(bool normalized);        // apply to every list row
    void onListContextMenu(const QPoint& pos);      // right-click on the image list
    void combineChannels();
    // Dialog builders shared by the menu slots (exec) and ScriptRunner
    // (show, for scripted captures). Null when preconditions fail.
    class RotateDialog*  makeRotateDialog();
    class CombineDialog* makeCombineDialog(QString* whyNot = nullptr);
    void adoptCombineResult(class CombineDialog& dlg);
    bool runColorTransport(const QString& refKey, int strengthPct,
                           QString* errOut = nullptr, bool asStretch = false,
                           bool fitColourAdj = false);
    void combineStars();                  // screen-blend starless + stars-only                         // Tools ▸ Combine Channels…

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
    // ICC colour management: rebuild m_iccToSrgb from m_header's embedded
    // profile; renderDisplayImage() = stretch render + that transform.
    void updateIccTransform();
    void pointStellarium(double raDeg, double decDeg, double fovDeg);
    QImage renderDisplayImage(const ImageData& img, const StretchModel& m) const;
    // Save dialogs with inline format options (depth/quality/XISF compression).
    QString exportImageDialogPath(const QString& title, bool offer16,
                                  bool* want16, int* quality);
    QString dataSaveDialogPath(const QString& title, io::SaveOptions& opts);
    void rebrandSyntheticAfterSave(const QString& savedPath);
    void displayPath(const QString& path);     // decode one file into the view
    QString addSyntheticImage(const QString& name, ImageData&& img);  // in-memory result → list; returns its key
public:
    // Undo/redo backing for synthetic entries (combine, colour transport).
    void removeSyntheticEntry(const QString& key);
    void restoreSyntheticEntry(const QString& key, const QString& name,
                               std::shared_ptr<ImageData> img);
public:
    enum class Xform { RotCW, RotCCW, FlipH, FlipV };
    // Undo plumbing (used by the QUndoCommand classes in MainWindow.cpp):
    // Debayer changes: apply without pushing / apply + push one undo entry.
    // kKeep leaves that half unchanged (mode is per-image, method global).
    static constexpr int kKeepDebayer = INT_MIN;
    void applyStretchState(const StretchModel::State& st);   // undo plumbing
    void applyDebayerChange(const QString& path, int mode, int method);
    void applyDebayerToAll();                 // copy the shown image's mode to every row
    QSet<QString> m_cfaHinted;                // mosaic sniff shown (once per path)
    void requestDebayerChange(int newMode, int newMethod);
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
    // Forward map (disk frame → current view frame) of an orientation history,
    // as one QTransform — used to carry ROIs/points across the rotation.
    QTransform diskToViewTransform(const QStringList& ops, const QSize& diskSize) const;
    QHash<QString, QSize> m_diskSizeByPath;    // as-decoded dims, pre-orientation
    // Orientation revision per path: bumped on every rotate/flip/reset. Cells
    // stash PIXELS; a stash made under an older revision no longer matches the
    // path's history and must be re-derived on activation (stale-view guard).
    QHash<QString, int> m_xformRev;
    void bumpXformRev(const QString& path) { ++m_xformRev[path]; }
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
    // Values Everywhere (V): the coordinates/values under the pointer shown as
    // an overlay in EVERY cell, each reading its own data at the corresponding
    // pixel (through calibrated-link transforms when present).
    bool m_valuesEverywhere = false;
    // Register (R / Shift+R): point-pair calibration between two cells.
    // Pick a feature in one cell, the same feature in another: one pair
    // snaps the translation (scale/rotation as aligned by eye); a second
    // pair (Shift+R) solves the full similarity (scale+rotation+translation)
    // from both pairs. Esc cancels an armed pick.
    struct RegPair { ViewCell* a = nullptr; QPointF pa; ViewCell* b = nullptr; QPointF pb; };
    bool m_regArmed = false;          // waiting for a pick
    bool m_regSecond = false;         // this arming adds a second pair
    RegPair m_regCur;                 // the pair being collected
    RegPair m_regFirst;               // completed first pair (for the similarity solve)
    void startRegister(bool secondPair);
    void onRegisterPointPicked(ImageView* v, double x, double y);
    void cancelRegister();
    void finishRegisterPair();
    void updateReadouts(int x, int y, bool valid);   // fan the active hover out
    void clearReadouts();
    QAction* m_toolEllipse = nullptr;
    QAction* m_toolLine = nullptr;
    QAction* m_toolText = nullptr;
    void refreshAnnotations();            // rebuild the overlay for the shown image
    void connectViewSignals(ImageView* v);           // per-view wiring (grid cells)
    void onCellSwap(ViewCell* oldC, ViewCell* newC); // active-cell state exchange
    ViewGrid* m_grid = nullptr;
    void ensureAnnotationsVisible();      // force the overlay on (load/import)
    QAction* m_annVisAct = nullptr;       // View ▸ Show Annotations (kept in sync)

    // Auto-reload: watch every on-disk list image; re-decode when an external
    // tool (PixInsight, Siril, GraXpert, ...) overwrites one.
    // Debayer: per-image mode (-1 off, 0 auto-detect, 1..4 forced pattern in
    // BayerPattern enum order); algorithm is the global preference.
    QHash<QString, int> m_debayerByPath;
    QAction* m_debayerModeActs[6] = {};   // auto, RGGB, BGGR, GRBG, GBRG, off
    QAction* m_debayerMethodActs[3] = {}; // superpixel, bilinear, RCD
    ImageData applyDebayer(ImageData&& img, ImageHeader& hdr, const QString& key);
    void syncDebayerMenu();

    // Multi-HDU FITS probing runs AFTER the rows appear (one file per event-
    // loop tick): adding 20 files stays instant, child rows fill in behind.
    QStringList m_hduProbeQueue;
    void scheduleHduProbe();
    // Write the current stretch (+adjustments) into every list image's
    // memory — each applies as that image loads. Refreshes visible cells.
    void applyStretchToAllList();

    // Crop: the visible region (menu) or an explicit rect (script) becomes a
    // new in-memory entry — pixels copied at full depth, WCS rebased (pure
    // CRPIX shift), annotations translated, header carried for saving.
    void cropCurrentToRect(QRect r);
    QHash<QString, ImageHeader> m_syntheticHeaders;   // header per mem:// entry

    // Blink culling (checked = keep):
    void toggleCurrentTag();                    // B — group-toggle the selection's checks
    void setSelectedTags(bool checked);         // check/uncheck the list selection
    bool m_tagPropagating = false;              // guards checkbox→selection fan-out
    bool m_scriptDriving = false;               // --run active: never block on modals
    void sortListByTag();                       // checked rows first, order stable
    void removeTaggedFromList(bool checked);    // drop all (un)checked rows
    void moveTaggedFiles(bool checked,          // move (un)checked files + sidecars;
                         const QString& destDir = QString());  // empty → ask (GUI)
    void migratePathState(const QString& oldKey, const QString& newKey);

public:
    // --shared-stf: auto-stretch the first image and share it with the list.
    void sharedStfStartup();
private:

    QFileSystemWatcher* m_fileWatcher = nullptr;
    QTimer*        m_reloadTimer = nullptr;    // debounce: writes arrive in bursts
    QSet<QString>  m_reloadPending;            // base file paths awaiting reload
    QAction*       m_autoReloadAct = nullptr;
    void syncFileWatcher();
    void onWatchedFileChanged(const QString& path);
    void reloadChangedFiles();
    // Rotate/flip history per image: re-applied when the image reloads from
    // disk (blink-back or a fresh session via the annotation sidecar), so the
    // pixels always match annotations made in a transformed orientation.
    static QString xformName(Xform x);
    static bool xformFromName(const QString& n, Xform& out);
    // Collapse an orientation history into a minimal equivalent (merge adjacent
    // rotations, drop whole turns, cancel inverse pairs) — replaying a literal
    // rotate/counter-rotate pair from disk would bake in dead black borders.
    static QStringList canonicalXforms(QStringList ops);
    // Re-canonicalize the current image's history after a mutation; when it
    // shortens (e.g. rot → flip → rot-back), re-derive the pixels with one
    // clean replay so expansion borders never accumulate.
    void normalizeOrientation();
    void reapplyStoredXforms();
    // Push an undo entry for an annotation edit already applied to m_annByPath;
    // `before` is the list as it was prior to the edit.
    void pushAnnotationEdit(const QString& text, const QString& path,
                            std::vector<Annotation> before);
    void saveAnnotations();               // silent save to the image's sidecar
    void saveAnnotationsAs();             // dialog for an explicit file name
    bool writeAnnotationsFile(const QString& path);   // shared writer (current image)
    bool writeAnnotationsFileFor(const QString& key, const QString& path);  // any listed image
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
    // Async display rendering: updateDisplay() snapshots state and kicks a
    // worker; slider drags stay fluid while frames render off-thread, and
    // intermediate states are coalesced (only the latest is rendered).
    QFutureWatcher<QImage>* m_renderWatcher = nullptr;
    bool        m_renderPending = false;  // a newer state arrived mid-render
    QNetworkAccessManager* m_net = nullptr;   // Stellarium remote control
    QColorTransform m_iccToSrgb;          // embedded-ICC → sRGB (see updateIccTransform)
    bool        m_hasIcc = false;         // m_iccToSrgb is meaningful
    // Stretch undo history: user gestures coalesce (timer) into one
    // StretchStateCmd each, so ⌘Z walks stretch edits back to the as-loaded
    // state. m_squelchStretch marks programmatic changes (image switches,
    // undo/redo itself), which must not record.
    StretchModel::State m_undoBase;        // state at the last recorded boundary
    StretchModel::State m_undoPendingNext; // state at the latest user change
    QString     m_undoBasePath;
    QTimer*     m_stretchUndoTimer = nullptr;
    int         m_squelchStretch = 0;
    const void* m_renderSrc = nullptr;    // identity of the pixels being rendered
    QSize       m_renderSize;
    QComboBox*      m_cmapCombo = nullptr;
    QCheckBox*      m_invertCheck = nullptr;
    QCheckBox*      m_splitCheck = nullptr;
    QWidget*        m_splitWidget = nullptr;
    QSlider*        m_splitSlider = nullptr;

    bool m_imageOnly = false;
    QHash<QString, QAction*> m_actionRegistry;   // shortcut names → actions (ScriptRunner)
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

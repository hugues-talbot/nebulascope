#include "ui/MainWindow.h"
#include "ui/ScriptRunner.h"
#include "app/AppInfo.h"
#include "app/BuildInfo.h"
#include "app/MacWindowColor.h"
#include "core/Preferences.h"
#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QPalette>
#include <QStyleFactory>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QIcon>
#include <QFileOpenEvent>
#include <QPointer>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

// QApplication subclass that handles macOS "open document" requests — double-
// clicking a file in Finder or using "Open With" sends a QFileOpenEvent rather
// than passing the path in argv. We forward it to the main window. Events that
// arrive before the window exists are queued and replayed once it's set.
class NebulaApp : public QApplication {
public:
    NebulaApp(int& argc, char** argv) : QApplication(argc, argv) {}
    void setWindow(astro::MainWindow* w) {
        m_win = w;
        if (w && !m_pending.isEmpty()) { w->openPaths(m_pending); m_pending.clear(); raiseWindow(); }
    }
protected:
    bool event(QEvent* e) override {
        if (e->type() == QEvent::FileOpen) {
            const auto* fo = static_cast<QFileOpenEvent*>(e);
            QString f = fo->file();
            if (f.isEmpty()) f = fo->url().toLocalFile();  // some senders pass a URL
            // Breadcrumb for the intermittent "right-click open did nothing"
            // reports — visible in Console.app (filter: NebulaScope).
            qInfo("NebulaScope FileOpen: %s",
                  qPrintable(f.isEmpty() ? "<empty> url=" + fo->url().toString() : f));
            if (!f.isEmpty()) {
                // Finder delivers a multi-selection as one event per file:
                // coalesce briefly and open them as ONE batch (a single
                // decode+display instead of one per file).
                m_pending << f;
                if (m_win) {
                    if (!m_batchTimer) {
                        m_batchTimer = new QTimer(this);
                        m_batchTimer->setSingleShot(true);
                        m_batchTimer->setInterval(150);
                        connect(m_batchTimer, &QTimer::timeout, this, [this] {
                            if (!m_win || m_pending.isEmpty()) return;
                            m_win->openPaths(m_pending);
                            m_pending.clear();
                            raiseWindow();
                        });
                    }
                    m_batchTimer->start();
                }
            }
            return true;
        }
        return QApplication::event(e);
    }
private:
    // A QFileOpenEvent does not activate the app: when NebulaScope is already
    // running (or another app is frontmost), the image loads into a window
    // that stays buried — Finder "Open With" looks like it did nothing.
    void raiseWindow() {
        if (!m_win) return;
        m_win->setWindowState(m_win->windowState() & ~Qt::WindowMinimized);
        m_win->show();
        m_win->raise();
        m_win->activateWindow();
    }
private:
    QPointer<astro::MainWindow> m_win;
    QStringList m_pending;
    QTimer* m_batchTimer = nullptr;
};

static void printUsage() {
    std::printf(
        "NebulaScope \u2014 astronomical image inspector (FITS / XISF / JPEG / PNG / TIFF)\n"
        "\n"
        "Usage:\n"
        "  nebulascope [options] [files...]\n"
        "\n"
        "Arguments:\n"
        "  files                 One or more images to open. Globs (e.g. *.fits) are\n"
        "                        expanded by the shell, or by NebulaScope if unexpanded.\n"
        "\n"
        "Options:\n"
        "  -l, --list <file>     Load a saved image list (one path per line; blank\n"
        "                        lines and #-comments ignored; relative paths resolved\n"
        "                        against the list file's directory).\n"
        "      --split <RxC>     Split the view into R rows \u00d7 C columns (max 5x5) and\n"
        "                        assign the first R*C images to the cells in raster\n"
        "                        order, e.g. --split 2x1.\n"
        "      --shared-stf      Auto-stretch the first image and share that stretch\n"
        "                        with every loaded image (same-session frames).\n"
        "      --lang <code>     Override the UI language for this run (en, fr, or\n"
        "                        system). The default is Preferences ▸ Language.\n"
        "      --run <script>    Execute a line-based command script (testing/batch)\n"
        "                        and exit with the number of failed assertions.\n"
        "                        Headless: QT_QPA_PLATFORM=offscreen. See docs/MANUAL.md.\n"
        "      --run list        List all script commands with one-line summaries.\n"
        "  -h, --help [command]  Show this help — or detailed help for one script\n"
        "                        command, e.g. nebulascope --help transport.\n"
        "  -V, --version         Print version and exact build id (git describe).\n"
        "\n"
        "Supported formats: .fits .fit .fts .fz .xisf .jpg .jpeg .png .tif .tiff .webp\n"
        "\n"
        "Examples:\n"
        "  nebulascope M51.fits\n"
        "  nebulascope *.fits\n"
        "  nebulascope --list tonight.txt\n"
        "  nebulascope --split 1x2 lum.fits ha.fits\n");
}

// Dark "astro tool" theme, roughly matching the mockup.
static void applyTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window,        QColor("#0a0e13"));
    p.setColor(QPalette::WindowText,    QColor("#c8d2dc"));
    p.setColor(QPalette::Base,          QColor("#0b1016"));
    p.setColor(QPalette::AlternateBase, QColor("#0c1117"));
    p.setColor(QPalette::Text,          QColor("#c8d2dc"));
    p.setColor(QPalette::Button,        QColor("#11171e"));
    p.setColor(QPalette::ButtonText,    QColor("#c8d2dc"));
    p.setColor(QPalette::Highlight,     QColor("#2a557e"));
    p.setColor(QPalette::HighlightedText, QColor("#eaf2fb"));
    p.setColor(QPalette::ToolTipBase,   QColor("#11171e"));
    p.setColor(QPalette::ToolTipText,   QColor("#c8d2dc"));
    app.setPalette(p);

    app.setStyleSheet(R"(
        QMainWindow, QDockWidget { background:#0a0e13; }
        QDockWidget::title { background:#0c1117; padding:6px 10px; color:#7e8b98;
            font-size:11px; letter-spacing:1px; }
        QToolBar { background:#0c1117; border-bottom:1px solid #18222d; spacing:6px; padding:5px; }
        QStatusBar { background:#0c1117; color:#6b7886; }
        QStatusBar QLabel { color:#aeb9c4; font-family:'IBM Plex Mono', monospace; }
        QMenuBar { background:#0c1117; color:#c8d2dc; }
        QMenuBar::item:selected { background:#13202e; }
        QMenu { background:#11171e; color:#c8d2dc; border:1px solid #1d2833; }
        QMenu::item:selected { background:#13202e; }
        QListWidget { background:#0b1016; border:none; color:#c8d2dc; padding:4px; }
        QListWidget::item { padding:6px; border-radius:4px; }
        QListWidget::item:selected { background:#13202e; }
        QPushButton { background:transparent; color:#8492a0; border:1px solid #1f2b37;
            border-radius:5px; padding:5px 10px; font-size:11px; }
        QPushButton:hover { border-color:#2a557e; }
        QPushButton:checked { background:#13202e; color:#8fc0f5; border-color:#2a557e; }
        QLabel { color:#9fabb8; font-size:11px; }
        QSlider::groove:horizontal { height:3px; background:#243040; border-radius:2px; }
        QSlider::handle:horizontal { width:12px; height:12px; margin:-5px 0;
            background:#cdd7e1; border-radius:6px; }
        QToolButton { color:#8492a0; padding:5px 9px; border-radius:5px; }
        QToolButton:hover { background:#13202e; color:#8fc0f5; }
    )");
}

int main(int argc, char** argv) {
    // Handle --help/-h before constructing the GUI so it works headless too.
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--version" || a == "-V") {
            // Official version + exact build provenance (git describe).
            std::printf("NebulaScope %s (%s)\n", appinfo::kVersion, appbuild::gitDescribe());
            return 0;
        }
        if (a == "--help" || a == "-h") {
            // "--help <command>" documents one script command in detail.
            if (i + 1 < argc) {
                const QString cmd = QString::fromLocal8Bit(argv[i + 1]);
                if (astro::ScriptRunner::printCommandHelp(cmd)) return 0;
                std::fprintf(stderr, "Unknown script command \"%s\" — list them with: nebulascope --run list\n\n",
                             cmd.toLocal8Bit().constData());
                return 2;
            }
            printUsage();
            return 0;
        }
        // "--run list" is a reserved word: print the script-command reference.
        if (a == "--run" && i + 1 < argc &&
            QString::fromLocal8Bit(argv[i + 1]) == QLatin1String("list")) {
            astro::ScriptRunner::printCommandList();
            return 0;
        }
    }

    NebulaApp app(argc, argv);
    app.setApplicationName("NebulaScope");
    app.setApplicationDisplayName("NebulaScope");
    app.setOrganizationName("NebulaScope");
    app.setWindowIcon(QIcon(":/appicon.png"));   // bundled via Qt resource; .icns drives the macOS dock
    applyTheme(app);

    // UI language: Preferences ▸ Language ("" = follow the system), overridable
    // per-run with --lang (parsed here, before any widget is constructed, so
    // every tr() call resolves against the right translator). English is the
    // source language: no translator is installed for it.
    QString lang = astro::Preferences::get().language;
    {
        const QStringList a = app.arguments();
        const int i = a.indexOf(QLatin1String("--lang"));
        if (i >= 0 && i + 1 < a.size()) lang = a[i + 1].toLower();
        if (lang == QLatin1String("system")) lang.clear();
    }
    const QLocale uiLocale = lang.isEmpty() ? QLocale::system() : QLocale(lang);
    static QTranslator appTr, qtTr;              // must outlive app.exec()
    if (appTr.load(uiLocale, QStringLiteral("nebulascope"), QStringLiteral("_"),
                   QStringLiteral(":/i18n")))
        app.installTranslator(&appTr);
    if (qtTr.load(uiLocale, QStringLiteral("qtbase"), QStringLiteral("_"),
                  QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTr);            // Qt's own dialogs/buttons

    astro::MainWindow w;
    w.show();
    astro::tagWindowAsSRgb(w.windowHandle());    // wide-gamut panels: OS converts sRGB
    app.setWindow(&w);                           // replays any Finder open-file events

    // Command line:  nebulascope *.fits        (files; shell usually globs)
    //                nebulascope --list set.txt (a saved image list)
    QStringList files;
    int splitR = 0, splitC = 0;
    QString scriptPath;
    bool sharedStf = false;
    const QStringList args = app.arguments().mid(1);
    for (int i = 0; i < args.size(); ++i) {
        const QString& a = args[i];
        if (a.startsWith("-psn_") || a.startsWith("-NSDocumentRevisionsDebugMode")) {
            // Cocoa-injected args when Finder/Launchpad launches the .app bundle
            // (process serial number, document-revisions debug). Ignore them so
            // they aren't mistaken for unknown options or filenames.
            if (a == "-NSDocumentRevisionsDebugMode" && i + 1 < args.size()) ++i;  // skip its value
            continue;
        }
        if ((a == "--list" || a == "-l") && i + 1 < args.size()) {
            w.importListFile(args[++i]);            // load a saved list file
        } else if (a == "--split" && i + 1 < args.size()) {
            // --split "2x1": split the view, assign the first N files in raster order.
            const QStringList parts = args[++i].toLower().split(QLatin1Char('x'));
            bool okR = false, okC = false;
            if (parts.size() == 2) { splitR = parts[0].toInt(&okR); splitC = parts[1].toInt(&okC); }
            if (!okR || !okC || splitR < 1 || splitR > 5 || splitC < 1 || splitC > 5) {
                std::fprintf(stderr, "Bad --split argument (expected e.g. \"2x1\", max 5x5)\n");
                printUsage();
                return 2;
            }
        } else if (a == "--shared-stf") {
            sharedStf = true;
        } else if (a == "--lang" && i + 1 < args.size()) {
            ++i;                                    // consumed above, before the GUI existed
        } else if (a == "--run" && i + 1 < args.size()) {
            scriptPath = args[++i];
        } else if (a.startsWith('-')) {
            std::fprintf(stderr, "Unknown option: %s\n", a.toLocal8Bit().constData());
            printUsage();
            return 2;
        } else if (a.contains('*') || a.contains('?') || a.contains('[')) {
            const QFileInfo fi(a);
            QDir dir(fi.absolutePath());
            const auto matches = dir.entryList(QStringList{ fi.fileName() }, QDir::Files, QDir::Name);
            for (const QString& m : matches) files << dir.absoluteFilePath(m);
        } else if (QFileInfo::exists(a)) {
            files << QFileInfo(a).absoluteFilePath();
        }
    }
    if (!files.isEmpty()) w.openPaths(files);
    if (splitR > 0) w.applySplitLayout(splitR, splitC);
    if (sharedStf) w.sharedStfStartup();

    astro::ScriptRunner* runner = nullptr;
    if (!scriptPath.isEmpty()) {
        runner = new astro::ScriptRunner(&w, scriptPath, &app);
        if (!runner->load()) return 3;
        runner->start();                       // exits the app with the failure count
    }

    return app.exec();
}

#include "ui/MainWindow.h"
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

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
    QApplication app(argc, argv);
    app.setApplicationName("NebulaScope");
    applyTheme(app);

    astro::MainWindow w;
    w.show();
    return app.exec();
}

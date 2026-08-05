#include "app/MacWindowColor.h"

#ifdef Q_OS_MACOS
#import <AppKit/AppKit.h>
#include <QGuiApplication>
#include <QWindow>

namespace astro {
void tagWindowAsSRgb(QWindow* w) {
    if (!w) return;
    // winId() is only an NSView under the cocoa platform — headless runs
    // (QT_QPA_PLATFORM=offscreen, used by the script harness) crash on it.
    if (QGuiApplication::platformName() != QLatin1String("cocoa")) return;
    NSView* view = reinterpret_cast<NSView*>(w->winId());
    if (view && view.window)
        [view.window setColorSpace:[NSColorSpace sRGBColorSpace]];
}
}
#endif

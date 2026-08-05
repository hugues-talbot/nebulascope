#pragma once
//
// macOS display colour management. Qt paints widget content without tagging
// the window's colour space, so raw sRGB values land on wide-gamut (P3)
// panels unconverted — visibly oversaturated. Declaring the window's
// contents sRGB makes the OS compositor convert to the panel, matching what
// colour-managed applications (PixInsight, Preview) show. No-op elsewhere.
//
#include <QtGlobal>

class QWindow;

namespace astro {
#ifdef Q_OS_MACOS
void tagWindowAsSRgb(QWindow* w);
#else
inline void tagWindowAsSRgb(QWindow*) {}
#endif
}

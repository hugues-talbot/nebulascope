#pragma once
//
// Preferences — user-configurable defaults, persisted with QSettings
// (INI "NebulaScope/preferences"). Loaded once at startup; the Preferences
// dialog (Edit ▸ Preferences…) edits and saves them. These collect the values
// that were previously hardcoded around the UI.
//
#include <QColor>

namespace astro {

struct Preferences {
    int    gridTargetLines = 6;               // RA/Dec grid density: ~lines across the frame
    QColor annColor        = QColor("#8fc0f5"); // default colour for new annotations
    double annTextSize     = 12.0;             // default text size (screen points)
    double annLineWidth    = 1.0;              // ellipse/line stroke width in screen px (0 = hairline)
    double markerFrac      = 40.0;             // "Annotate Here" radius = imageWidth / this
    bool   autoLoadSidecar = true;             // load <image>_annotation.json on open

    static Preferences& get();                 // singleton, loaded on first use
    void load();
    void save() const;
};

} // namespace astro

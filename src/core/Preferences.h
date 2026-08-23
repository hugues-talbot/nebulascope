#pragma once
//
// Preferences — user-configurable defaults, persisted with QSettings
// (INI "NebulaScope/preferences"). Loaded once at startup; the Preferences
// dialog (Edit ▸ Preferences…) edits and saves them. These collect the values
// that were previously hardcoded around the UI.
//
#include <QColor>
#include <QString>

namespace astro {

struct Preferences {
    QString language;                          // UI language: "" = system, "en", "fr"
                                               // (applied at startup; --lang overrides)
    int    gridTargetLines = 6;               // RA/Dec grid density: ~lines across the frame
    QColor annColor        = QColor("#8fc0f5"); // default colour for new annotations
    double annTextSize     = 12.0;             // default text size (screen points)
    double annLineWidth    = 1.0;              // ellipse/line stroke width in screen px (0 = hairline)
    double markerFrac      = 40.0;             // "Annotate Here" radius = imageWidth / this
    bool   autoLoadSidecar = true;             // load <image>_annotation.json on open
    double overlayOpacity  = 1.0;              // floating-panel background opacity;
                                               // 1.0 = opaque (fast: repaints clip like
                                               // docks), <1 = see-through (recomposites
                                               // panels on every view repaint)
    int    recentImagesMax = 10;               // history length: images
    int    recentJsonMax   = 5;                // history length: annotation files
    int    debayerMethod   = 2;                // DebayerMethod: 0 superpixel,
                                               // 1 bilinear, 2 RCD (default)
    int    zoomStepCoarse  = 10;               // keyboard zoom > / < step, percent
    int    zoomStepFine    = 3;                // keyboard zoom . / , step, percent
    bool   commonAxis      = true;             // histogram: RGB channels on one pooled axis

    static Preferences& get();                 // singleton, loaded on first use
    void load();
    void save() const;
};

} // namespace astro

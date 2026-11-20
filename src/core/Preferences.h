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
    QString language;                          // UI language: "" = system, "en", "fr", "it"
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
    int    imageCacheMB    = 4096;             // decoded-image cache budget (0 = off)
    bool   ds9RightDrag    = false;            // right-button drag adjusts contrast/bias
                                               // as in SAOImage DS9 (default: it pans)
    int    skyOrient       = 0;                // plate-solved FITS at open: 0 as stored
                                               // (default), 1 oriented as the sky with
                                               // flips/quarter turns only (exact pixels),
                                               // 2 north exactly up (resampled)

    // Per-run overrides from the command line (--mouse, --orient): never
    // saved, and they win over the stored values for this process only.
    // -1 = no override.
    int    runDs9RightDrag = -1;
    int    runSkyOrient    = -1;
    bool   effectiveDs9RightDrag() const { return runDs9RightDrag >= 0 ? runDs9RightDrag != 0 : ds9RightDrag; }
    int    effectiveSkyOrient() const { return runSkyOrient >= 0 ? runSkyOrient : skyOrient; }

    static Preferences& get();                 // singleton, loaded on first use
    void load();
    void save() const;
};

} // namespace astro

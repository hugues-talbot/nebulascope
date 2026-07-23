#include "core/Preferences.h"
#include <QSettings>

namespace astro {

static QSettings prefStore() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("NebulaScope"), QStringLiteral("preferences"));
}

Preferences& Preferences::get() {
    static Preferences p;
    static bool loaded = false;
    if (!loaded) { p.load(); loaded = true; }
    return p;
}

void Preferences::load() {
    QSettings s = prefStore();
    s.beginGroup(QStringLiteral("defaults"));
    gridTargetLines = s.value(QStringLiteral("grid_target_lines"), gridTargetLines).toInt();
    const QColor c(s.value(QStringLiteral("annotation_color"), annColor.name()).toString());
    if (c.isValid()) annColor = c;
    annTextSize     = s.value(QStringLiteral("annotation_text_size"), annTextSize).toDouble();
    annLineWidth    = s.value(QStringLiteral("annotation_line_width"), annLineWidth).toDouble();
    markerFrac      = s.value(QStringLiteral("marker_size_fraction"), markerFrac).toDouble();
    autoLoadSidecar = s.value(QStringLiteral("auto_load_sidecar"), autoLoadSidecar).toBool();
    overlayOpacity  = s.value(QStringLiteral("overlay_opacity"), overlayOpacity).toDouble();
    recentImagesMax = s.value(QStringLiteral("recent_images_max"), recentImagesMax).toInt();
    recentJsonMax   = s.value(QStringLiteral("recent_json_max"), recentJsonMax).toInt();
    s.endGroup();
    gridTargetLines = qBound(3, gridTargetLines, 20);
    annTextSize     = qBound(5.0, annTextSize, 72.0);
    annLineWidth    = qBound(0.0, annLineWidth, 8.0);
    markerFrac      = qBound(5.0, markerFrac, 200.0);
    recentImagesMax = qBound(0, recentImagesMax, 50);
    recentJsonMax   = qBound(0, recentJsonMax, 50);
    overlayOpacity  = qBound(0.5, overlayOpacity, 1.0);
}

void Preferences::save() const {
    QSettings s = prefStore();
    s.beginGroup(QStringLiteral("defaults"));
    s.setValue(QStringLiteral("grid_target_lines"), gridTargetLines);
    s.setValue(QStringLiteral("annotation_color"), annColor.name());
    s.setValue(QStringLiteral("annotation_text_size"), annTextSize);
    s.setValue(QStringLiteral("annotation_line_width"), annLineWidth);
    s.setValue(QStringLiteral("marker_size_fraction"), markerFrac);
    s.setValue(QStringLiteral("auto_load_sidecar"), autoLoadSidecar);
    s.setValue(QStringLiteral("overlay_opacity"), overlayOpacity);
    s.setValue(QStringLiteral("recent_images_max"), recentImagesMax);
    s.setValue(QStringLiteral("recent_json_max"), recentJsonMax);
    s.endGroup();
}

} // namespace astro

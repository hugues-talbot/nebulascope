#pragma once
//
// MacSavePanel — a native NSSavePanel with an AppKit accessory view, for the
// two option-rich save dialogs (Export View As…, Save Data As…). The Qt
// dialog can only host inline option rows in its own non-native widget; the
// platform's intended design for "save with options" is the accessory view
// (Preview.app's export panel). This wrapper provides:
//   * a Format popup (drives the allowed extension, like the Qt filter combo),
//   * one optional secondary popup (pixel depth / XISF compression),
//   * one optional slider with live value (JPEG/WebP quality),
//   * per-format enabling of the two controls,
//   * click-any-image name adoption, base name only (delegate-enforced), and
//   * a guarantee that the saved suffix follows the chosen format (no
//     "name.xisf.png").
// Only usable on the cocoa platform — callers must fall back to the Qt
// dialog when savePanelAvailable() is false (offscreen tests, other OSes).
//
#include <QString>
#include <QStringList>
#include <QVector>

namespace astro::mac {

struct SaveFormat {
    QString label;            // shown in the Format popup, e.g. "TIFF 16-bit"
    QStringList suffixes;     // acceptable extensions; first is the default
    bool popupEnabled = false;   // secondary popup active for this format
    bool sliderEnabled = false;  // slider active for this format
};

struct SavePanelSpec {
    QString title;            // panel message line (native panels have no title bar text)
    QString directory;        // initial directory
    QString suggestedName;    // prefilled base name (may be empty)
    QVector<SaveFormat> formats;
    int formatIndex = 0;
    QString formatLabel;      // "Format:" (localised by the caller)
    QString popupLabel;       // empty = no secondary popup
    QStringList popupItems;
    int popupIndex = 0;
    QString sliderLabel;      // empty = no slider
    int sliderMin = 1;
    int sliderMax = 100;
    int sliderValue = 90;
    QStringList clickableSuffixes;  // extra extensions kept enabled for name adoption
};

struct SavePanelResult {
    bool accepted = false;
    QString path;             // suffix already matching the chosen format
    int formatIndex = 0;
    int popupIndex = 0;
    int sliderValue = 0;
};

bool savePanelAvailable();                       // cocoa platform only
SavePanelResult runSavePanel(const SavePanelSpec& spec);

} // namespace astro::mac

#include "ui/ScriptRunner.h"
#include <QFileInfo>
#include "ui/MainWindow.h"
#include "ui/ViewGrid.h"
#include "ui/ImageView.h"
#include "ui/RotateDialog.h"
#include "ui/CombineDialog.h"
#include "ui/PreferencesDialog.h"
#include "ui/HistogramPanel.h"
#include "ui/HistogramView.h"
#include "render/DisplayRenderer.h"
#include "core/Debayer.h"
#include "core/Preferences.h"
#include "io/ImageWriter.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSlider>
#include <QListWidget>
#include <QTextStream>
#include <QRegularExpression>
#include <QTimer>
#include <QMouseEvent>
#include <cmath>
#include <set>
#include <cstdio>

namespace astro {

// --- script-command reference (single source for --run list / --help <cmd>) --
namespace {
struct CommandHelp { const char* usage; const char* desc; };
struct CommandRef  { const char* name; CommandHelp help; };
const CommandRef kCommands[] = {
  {"open",       {"open <path>",
    "Load an image into the list and display it (first empty cell when split).\n"
    "Globs are not expanded here; give a concrete path."}},
  {"show",       {"show <n>",
    "Select image-list row n (1-based) and display it."}},
  {"next",       {"next", "Blink forward through the list (wraps)."}},
  {"prev",       {"prev", "Blink backward through the list (wraps)."}},
  {"histdrag",   {"histdrag b|m|w <dx_px>",
    "Synthesize a drag of the histogram's B/M/W grip by dx screen pixels\n"
    "(past the plot edge extends the axis). Test hook."}},
  {"window",     {"window <c|all> <black> <mid> <white>",
    "Set the linear window (B/M/W) in RAW data units for channel c (0..2) or\n"
    "all channels. Values may lie outside the data range."}},
  {"axis",       {"axis common|channel|wide|fit|peak",
    "Histogram axis: RGB range policy (common = one pooled range, the\n"
    "default; channel = each over its own), the Wide extended range / fit\n"
    "back to the data, or peak = snap the GHS symmetry point to the mode."}},
  {"regpick",    {"regpick <cell> <x> <y>",
    "A Match pick: image pixel (x,y) in view cell n (1-based), while\n"
    "Match is armed (action register_views, or register_views_2 for the\n"
    "second pair). Two picks in different cells complete a pair."}},
  {"hover",      {"hover <x> <y> | hover off",
    "Synthesize a pointer hover over image pixel (x,y) of the active view\n"
    "(status-bar readout; per-cell overlays when Values in All Views is on)."}},
  {"activate",   {"activate <n>",
    "Make view cell n (1-based, raster order) the active cell — the same\n"
    "path as a mouse click in that cell."}},
  {"split",      {"split <RxC>",
    "Split the view into R rows x C columns (max 5x5) and assign the first\n"
    "R*C list images to the cells in raster order."}},
  {"fn",         {"fn linear|log|asinh|ghs", "Set the stretch transfer function."}},
  {"autostf",    {"autostf [linked]",
    "Automatic stretch from image statistics; 'linked' pools all channels\n"
    "(preserves colour balance). Use only on LINEAR data - processed masters\n"
    "look right with 'reset'."}},
  {"reset",      {"reset", "Plain min-max linear window; clears adjustments."}},
  {"stfall",     {"stfall",
    "Share the displayed stretch (+adjustments) with every image in the\n"
    "list: each applies as that image loads (same-session frames).\n"
    "CLI equivalent at startup: --shared-stf."}},
  {"adjust",     {"adjust <name> <value>",
    "Post-stretch adjustment. Names: brightness contrast gamma shadows\n"
    "highlights blackpoint whitepoint temperature tint hue saturation vibrance."}},
  {"crop",       {"crop <x> <y> <w> <h> | crop view",
    "Full-depth crop into a new in-memory list entry (save it with `save`).\n"
    "The plate solution is rebased exactly (pure CRPIX shift) and written as\n"
    "standard FITS cards; annotations translate with the pixels. `view`\n"
    "crops to the currently visible region (menu: Image > Crop, Shift+C)."}},
  {"rot90",      {"rot90 cw|ccw", "Lossless 90-degree rotation of the data."}},
  {"flip",       {"flip h|v", "Lossless horizontal/vertical flip of the data."}},
  {"rotate",     {"rotate <deg>",
    "Absolute arbitrary rotation (bilinear, expanded canvas, NaN corners)."}},
  {"export",     {"export [region] <path>",
    "Write the displayed rendition (stretch, adjustments, colormap baked)\n"
    "as PNG/JPEG/TIFF."}},
  {"saveann",    {"saveann [path]",
    "Write the annotation sidecar (shapes, orientation, full display block)\n"
    "to <path>, or to the image's default \"<image>_annotation.json\"."}},
  {"bake",       {"bake <path>",
    "Write the display transfer baked at full precision (Float32 FITS, no\n"
    "dither/8-bit) — the reference artifact for the sidecar conformance\n"
    "test against tools/render_sidecar.py."}},
  {"save",       {"save <path>",
    "Write the DATA (Float32) as FITS/XISF/16-bit TIFF. XISF saves use the\n"
    "default compression (Zlib, byte-shuffled). An in-memory result's list\n"
    "entry takes the saved name (as in the GUI)."}},
  {"assert",     {"assert size <W> <H> | channels <n> | rows <n> | name <text> | stretch <c> <b> <m> <w> [tol] | adjust <name> <v> [tol] | fn <name> | pixel <x> <y> <v...> [tol] | range <min> <max> [tol] | mapped <x> <y> <cell> <qx> <qy> [tol] | wcsmatch <cell> <x> <y> [tol] | levels <c> <min>",
    "Test assertions against the displayed image's raw data (rows: the\n"
    "image-list row count). Failures are counted; the process exit code is\n"
    "the failure count."}},
  {"tag",        {"tag on|off|toggle",
    "The current row's keep-check (checked = keep). Interactive equivalent:\n"
    "the checkbox, or the B key while blinking."}},
  {"tagsort",    {"tagsort", "Reorder the list: checked rows first (stable)."}},
  {"tagremove",  {"tagremove checked|unchecked",
    "Remove all rows in that tag state from the list (files untouched)."}},
  {"tagmove",    {"tagmove checked|unchecked <dir>",
    "Move the files behind rows in that tag state into <dir> (created if\n"
    "missing), annotation sidecars and per-image state included; the list\n"
    "rekeys to the new locations."}},
  {"sleep",      {"sleep <ms>", "Pause the script (event loop keeps running)."}},
  {"waitloaded", {"waitloaded [ms]",
    "Block until the current image and its statistics are ready (default\n"
    "timeout 10000 ms). Prefer this over sleep after open/show."}},
  {"screenshot", {"screenshot <path> [dialog]",
    "Grab the whole main window (or, with 'dialog', the dialog opened by the\n"
    "dialog command) to PNG after the async render pipeline drains.\n"
    "Works headless (QT_QPA_PLATFORM=offscreen)."}},
  {"cmap",       {"cmap gray|heat|viridis|magma|inferno|cividis|a|b|bb|he|cool|rainbow|standard|i8|aips0|sls",
    "False-colour base map (mono images only). The second group are\n"
    "SAOImage DS9's classic palettes (identical control points)."}},
  {"cmapmod",    {"cmapmod invert|split on|off [t]",
    "Colormap modifiers; split folds the map at threshold t (0..1)."}},
  {"panels",     {"panels on|off", "Show/hide all overlay panels (Image Only)."}},
  {"action",     {"action <name>",
    "Trigger any menu action by its shortcut-registry name (e.g. toggle_grid).\n"
    "Avoid actions that open modal dialogs - they block the script."}},
  {"debayer",    {"debayer auto|off|rggb|bggr|grbg|gbrg [rcd|bilinear|superpixel] [all]",
    "Demosaic mode for the displayed OSC frame: auto detects BAYERPAT from\n"
    "the header, a pattern name forces it, off shows the raw mosaic. The\n"
    "optional second argument sets the global algorithm (persisted); a\n"
    "trailing `all` stamps the mode onto every list row (for metadata-less\n"
    "capture streams, e.g. raw-mosaic PNG dumps)."}},
  {"transport",  {"transport <row> [strength%] [stretch [colour]]",
    "Colour-transport the displayed image toward list row <row> (1-based) as\n"
    "reference (sliced optimal transport, as-displayed data); the result\n"
    "becomes a new display-ready list entry. Default strength 100. With\n"
    "`stretch`, no pixels are written: per-channel B/M/W are FITTED so the\n"
    "display matches the transported colours — non-destructive, cannot\n"
    "posterize. Adding `colour` also fits the cross-channel adjustments\n"
    "(temperature/tint/hue/saturation) for hue-rotation matches."}},
  {"dialog",     {"dialog rotate|combine|preferences|close",
    "Open a dialog NON-modally (for dlgclick/dlgcombo/screenshot ... dialog),\n"
    "or close the open one."}},
  {"dlgclick",   {"dlgclick <button text>",
    "Click a button in the open dialog by its visible text (e.g. a Combine\n"
    "preset, or 'Create Image')."}},
  {"dlgcombo",   {"dlgcombo <n> <prefix>",
    "Set the n-th combo box (creation order, 0-based) of the open dialog to\n"
    "the first entry whose text starts with <prefix>."}},
  {"quit",       {"quit", "End the script; the process exits with the failure count."}},
};
} // namespace

void ScriptRunner::printCommandList() {
    std::printf("NebulaScope script commands (one per line in a --run file; #-comments).\n"
                "Details: nebulascope --help <command>\n\n");
    for (const CommandRef& c : kCommands) {
        const QString firstLine = QString::fromUtf8(c.help.desc).section('\n', 0, 0);
        std::printf("  %-14s %s\n", c.name, firstLine.toLocal8Bit().constData());
    }
}

bool ScriptRunner::printCommandHelp(const QString& cmd) {
    for (const CommandRef& c : kCommands) {
        if (cmd.compare(QLatin1String(c.name), Qt::CaseInsensitive) != 0) continue;
        std::printf("%s\n\nUsage:  %s\n\n%s\n", c.name, c.help.usage, c.help.desc);
        return true;
    }
    return false;
}

ScriptRunner::ScriptRunner(MainWindow* w, const QString& scriptPath, QObject* parent)
    : QObject(parent), m_w(w), m_path(scriptPath) {
    // Scripts must never block on confirmation modals (only on the dialogs
    // they open themselves via `dialog`/`dlgclick`).
    m_w->m_scriptDriving = true;
}

bool ScriptRunner::load() {
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "script: cannot read %s\n", m_path.toLocal8Bit().constData());
        return false;
    }
    QTextStream ts(&f);
    while (!ts.atEnd()) m_lines << ts.readLine();
    return true;
}

void ScriptRunner::start() {
    QTimer::singleShot(m_delayMs, this, &ScriptRunner::step);
}

void ScriptRunner::step() {
    while (m_pc < m_lines.size()) {
        QString line = m_lines[m_pc++].trimmed();
        const int hash = line.indexOf(QLatin1Char('#'));
        if (hash >= 0) line = line.left(hash).trimmed();
        if (line.isEmpty()) continue;

        if (line == QLatin1String("quit")) break;

        QString err;
        if (!execute(line, err)) {
            std::fprintf(stderr, "script:%d: FAIL  %s\n      %s\n",
                         m_pc, line.toLocal8Bit().constData(), err.toLocal8Bit().constData());
            ++m_failures;
        } else {
            std::printf("script:%d: ok    %s\n", m_pc, line.toLocal8Bit().constData());
        }

        if (line.startsWith(QLatin1String("sleep "))) return;   // step() rescheduled by execute
        // Yield to the event loop between commands (renders, list signals).
        QTimer::singleShot(m_delayMs, this, &ScriptRunner::step);
        return;
    }
    std::printf("script: done, %d failure(s)\n", m_failures);
    QCoreApplication::exit(m_failures);
}

bool ScriptRunner::execute(const QString& line, QString& err) {
    const QStringList t = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    const QString& cmd = t[0];

    auto needArgs = [&](int n) {
        if (t.size() < n + 1) { err = QStringLiteral("expected %1 argument(s)").arg(n); return false; }
        return true;
    };

    if (cmd == QLatin1String("open")) {
        if (!needArgs(1)) return false;
        // Absolutized so the list key matches GUI opens and post-save rebrands
        // (duplicate detection compares keys verbatim).
        m_w->openPaths({ QFileInfo(t.mid(1).join(QLatin1Char(' '))).absoluteFilePath() });
        return true;
    }
    if (cmd == QLatin1String("show")) {
        if (!needArgs(1)) return false;
        const int row = t[1].toInt() - 1;
        if (row < 0 || row >= m_w->m_fileList->count()) { err = "row out of range"; return false; }
        // ClearAndSelect: a click-like activation. The single-arg overload
        // only ADDS to an ExtendedSelection, silently growing the selection.
        m_w->m_fileList->setCurrentRow(row, QItemSelectionModel::ClearAndSelect);
        return true;
    }
    if (cmd == QLatin1String("next")) { m_w->nextImage(); return true; }
    if (cmd == QLatin1String("prev")) { m_w->prevImage(); return true; }
    if (cmd == QLatin1String("histdrag")) {
        // histdrag b|m|w <dx_px> — synthesize a press on the Linear-mode grip
        // of handle b/m/w in the histogram plot, a drag by dx pixels (may run
        // past the plot edge), and a release. Exercises the edge-extend path.
        if (t.size() < 3) { err = "histdrag b|m|w <dx_px>"; return false; }
        HistogramView* hv = m_w->m_hist->histogramView();
        const QString h = t[1].toLower();
        const int c = hv->activeChannel() < 0 ? 0 : hv->activeChannel();
        const ChannelStretch cs = m_w->m_model.channel(c);
        const double v = h == QLatin1String("b") ? cs.black : h == QLatin1String("m") ? cs.mid : cs.white;
        const QPointF start = hv->gripPos(v);
        const QPointF end(start.x() + t[2].toDouble(), start.y());
        auto send = [&](QEvent::Type ty, const QPointF& pos) {
            QMouseEvent ev(ty, pos, hv->mapToGlobal(pos.toPoint()), Qt::LeftButton,
                           ty == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(hv, &ev);
        };
        send(QEvent::MouseButtonPress, start);
        const int steps = 12;
        for (int i = 1; i <= steps; ++i)
            send(QEvent::MouseMove, start + (end - start) * (double(i) / steps));
        send(QEvent::MouseButtonRelease, end);
        return true;
    }
    if (cmd == QLatin1String("window")) {
        // window <c|all> <black> <mid> <white> — set the linear window in RAW
        // data units. Values may lie outside the data range (black below the
        // minimum, white above the maximum): the handle domain is one full
        // span beyond the data on each side.
        if (t.size() < 5) { err = "window <c|all> <black> <mid> <white>"; return false; }
        if (!m_w->m_image.isValid()) { err = "no image shown"; return false; }
        const bool all = t[1].toLower() == QLatin1String("all");
        const int c0 = all ? 0 : t[1].toInt();
        const int c1 = all ? m_w->m_model.channelCount() - 1 : c0;
        if (c0 < 0 || c1 > 2) { err = "channel is 0..2 or all"; return false; }
        const double b = t[2].toDouble(), m = t[3].toDouble(), w = t[4].toDouble();
        if (!(b < m && m < w)) { err = "need black < mid < white"; return false; }
        for (int c = c0; c <= c1; ++c) {
            const double lo = m_w->m_model.lo(c), hi = m_w->m_model.hi(c);
            const double span = std::max(1e-12, hi - lo);
            ChannelStretch cs;
            cs.black = std::max(-1.0, std::min(2.0, (b - lo) / span));
            cs.mid   = std::max(-1.0, std::min(2.0, (m - lo) / span));
            cs.white = std::max(-1.0, std::min(2.0, (w - lo) / span));
            m_w->m_model.setChannel(c, cs);
        }
        return true;
    }
    if (cmd == QLatin1String("axis")) {
        // axis common|channel — the histogram's RGB range policy (display
        // unchanged; handles are re-expressed on the new ranges).
        if (!needArgs(1)) return false;
        const QString w = t[1].toLower();
        if (w == QLatin1String("wide") || w == QLatin1String("fit")) {
            m_w->m_hist->histogramView()->setWideAxis(w == QLatin1String("wide"));
            return true;
        }
        if (w == QLatin1String("peak")) {                 // GHS: SP -> histogram peak
            m_w->m_hist->histogramView()->snapSpToMode();
            return true;
        }
        if (w != QLatin1String("common") && w != QLatin1String("channel")) { err = "axis common|channel|wide|fit|peak"; return false; }
        m_w->m_hist->setCommonAxisChecked(w == QLatin1String("common"));
        emit m_w->m_hist->commonAxisToggled(w == QLatin1String("common"));
        return true;
    }
    if (cmd == QLatin1String("regpick")) {
        // regpick <cell> <x> <y> — a Register pick: image pixel (x,y) in cell n
        // (1-based) while Register is armed (action register_views / _2).
        if (t.size() < 4) { err = "regpick <cell> <x> <y>"; return false; }
        ViewCell* c = m_w->m_grid->cellAt(t[1].toInt() - 1);
        if (!c) { err = "cell out of range"; return false; }
        if (!m_w->m_regArmed) { err = "match not armed (action register_views first)"; return false; }
        m_w->onRegisterPointPicked(c->view(), t[2].toDouble(), t[3].toDouble());
        return true;
    }
    if (cmd == QLatin1String("assert") && t.size() >= 6 && t[1].toLower() == QLatin1String("mapped")) {
        // assert mapped <x> <y> <cell> <qx> <qy> [tol] — the active cell's
        // image pixel (x,y) corresponds (through the calibrated-link worlds)
        // to pixel (qx,qy) in cell n. Verifies registration geometry.
        if (t.size() < 7) { err = "assert mapped x y cell qx qy [tol]"; return false; }
        ViewCell* act = m_w->m_grid->activeCell();
        ViewCell* o = m_w->m_grid->cellAt(t[4].toInt() - 1);
        if (!act || !o) { err = "cell out of range"; return false; }
        if (!(act->calibrated && o->calibrated)) { err = "cells are not calibration-linked"; return false; }
        const QPointF q = o->world.inverted().map(act->world.map(QPointF(t[2].toDouble(), t[3].toDouble())));
        const double tol = t.size() > 7 ? t[7].toDouble() : 0.05;
        const double ex = t[5].toDouble(), ey = t[6].toDouble();
        if (std::abs(q.x() - ex) > tol || std::abs(q.y() - ey) > tol) {
            err = QStringLiteral("mapped to (%1, %2)").arg(q.x(), 0, 'f', 4).arg(q.y(), 0, 'f', 4);
            return false;
        }
        return true;
    }
    if (cmd == QLatin1String("assert") && t.size() >= 5 && t[1].toLower() == QLatin1String("wcsmatch")) {
        // assert wcsmatch <cell> <x> <y> [tol] — the active cell's pixel
        // (x,y), mapped through the calibrated-link worlds into cell n, lands
        // within tol px of where the two PLATE SOLUTIONS say it should
        // (pixel→sky in the active image, sky→pixel in cell n). Verifies the
        // WCS-based Match: the fitted affine agrees with the projections.
        ViewCell* act = m_w->m_grid->activeCell();
        ViewCell* o = m_w->m_grid->cellAt(t[2].toInt() - 1);
        if (!act || !o) { err = "cell out of range"; return false; }
        if (!(act->calibrated && o->calibrated)) { err = "cells are not calibration-linked"; return false; }
        const double x = t[3].toDouble(), y = t[4].toDouble();
        const double tol = t.size() > 5 ? t[5].toDouble() : 0.05;
        double ra, dec, ex, ey;
        if (!m_w->m_wcs.pixelToSky(x, y, ra, dec) || !o->wcs.skyToPixel(ra, dec, ex, ey)) {
            err = "no plate solution on one of the cells"; return false;
        }
        const QPointF q = o->world.inverted().map(act->world.map(QPointF(x, y)));
        if (std::abs(q.x() - ex) > tol || std::abs(q.y() - ey) > tol) {
            err = QStringLiteral("affine maps to (%1, %2), WCS says (%3, %4)")
                      .arg(q.x(), 0, 'f', 3).arg(q.y(), 0, 'f', 3).arg(ex, 0, 'f', 3).arg(ey, 0, 'f', 3);
            return false;
        }
        return true;
    }
    if (cmd == QLatin1String("hover")) {
        // hover <x> <y> — synthesize a pointer hover over image pixel (x,y) of
        // the active view: drives the status-bar readout and, with Values in
        // All Views on, the per-cell overlays. `hover off` = pointer left.
        if (!needArgs(1)) return false;
        if (t[1].toLower() == QLatin1String("off")) {
            m_w->onPixelHovered(0, 0, 0, 0, 0, false);
            return true;
        }
        if (t.size() < 3) { err = "hover <x> <y> | hover off"; return false; }
        const ImageData& img = m_w->m_image;
        if (!img.isValid()) { err = "no image shown"; return false; }
        const int x = t[1].toInt(), y = t[2].toInt();
        if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) { err = "pixel out of bounds"; return false; }
        const std::size_t i = std::size_t(y) * img.width() + x;
        const double r = img.plane<float>(0)[i];
        const double g = img.channels() >= 3 ? img.plane<float>(1)[i] : r;
        const double b = img.channels() >= 3 ? img.plane<float>(2)[i] : r;
        m_w->onPixelHovered(x, y, r, g, b, true);
        return true;
    }
    if (cmd == QLatin1String("activate")) {
        // activate <n> — make view cell n (1-based, raster order) the active
        // one: exactly the path a mouse click in that cell takes.
        if (!needArgs(1)) return false;
        ViewCell* c = m_w->m_grid->cellAt(t[1].toInt() - 1);
        if (!c) { err = "cell out of range"; return false; }
        m_w->m_grid->activate(c);
        return true;
    }
    if (cmd == QLatin1String("split")) {
        if (!needArgs(1)) return false;
        const QStringList p = t[1].toLower().split(QLatin1Char('x'));
        if (p.size() != 2) { err = "expected RxC"; return false; }
        m_w->applySplitLayout(p[0].toInt(), p[1].toInt());
        return true;
    }
    if (cmd == QLatin1String("fn")) {
        if (!needArgs(1)) return false;
        const QString f = t[1].toLower();
        StretchFn fn;
        if (f == "linear") fn = StretchFn::Linear;
        else if (f == "log") fn = StretchFn::Log;
        else if (f == "asinh") fn = StretchFn::Asinh;
        else if (f == "ghs") fn = StretchFn::GHS;
        else { err = "unknown function"; return false; }
        m_w->m_model.setFn(fn);
        return true;
    }
    if (cmd == QLatin1String("autostf")) {
        if (m_w->m_curStats.empty()) { err = "no image shown"; return false; }
        if (t.size() > 1 && t[1] == QLatin1String("linked"))
            m_w->m_model.autoStretchLinked(m_w->m_curStats);
        else
            m_w->m_model.autoStretch(m_w->m_curStats);
        return true;
    }
    if (cmd == QLatin1String("reset")) { m_w->m_model.reset(); return true; }
    if (cmd == QLatin1String("adjust")) {
        if (!needArgs(2)) return false;
        AdjustParams a = m_w->m_model.adjust();
        const QString k = t[1].toLower();
        const double v = t[2].toDouble();
        if      (k == "brightness")  a.brightness = v;
        else if (k == "contrast")    a.contrast = v;
        else if (k == "gamma")       a.gamma = v;
        else if (k == "shadows")     a.shadows = v;
        else if (k == "highlights")  a.highlights = v;
        else if (k == "blackpoint")  a.blackpoint = v;
        else if (k == "whitepoint")  a.whitepoint = v;
        else if (k == "temperature") a.temperature = v;
        else if (k == "tint")        a.tint = v;
        else if (k == "hue")         a.hue = v;
        else if (k == "saturation")  a.saturation = v;
        else if (k == "vibrance")    a.vibrance = v;
        else if (k == "mix") {                     // adjust mix m0 .. m8 (row-major)
            if (t.size() < 11) { err = "mix needs 9 values"; return false; }
            for (int i = 0; i < 9; ++i) a.mix[i] = t[2 + i].toDouble();
        }
        else { err = "unknown adjustment"; return false; }
        m_w->m_model.setAdjust(a);
        return true;
    }
    if (cmd == QLatin1String("rot90")) {
        if (!needArgs(1)) return false;
        m_w->applyTransform(t[1].toLower() == "ccw" ? MainWindow::Xform::RotCCW
                                                    : MainWindow::Xform::RotCW);
        return true;
    }
    if (cmd == QLatin1String("flip")) {
        if (!needArgs(1)) return false;
        m_w->applyTransform(t[1].toLower() == "v" ? MainWindow::Xform::FlipV
                                                  : MainWindow::Xform::FlipH);
        return true;
    }
    if (cmd == QLatin1String("rotate")) {
        if (!needArgs(1)) return false;
        m_w->pushRotateTo(t[1].toDouble());
        return true;
    }
    if (cmd == QLatin1String("export")) {
        if (!needArgs(1)) return false;
        if (!m_w->m_image.isValid()) { err = "no image shown"; return false; }
        const QImage full = DisplayRenderer::render(m_w->m_image, m_w->m_model);
        // `export region <path>`: the Export Zoomed Region render — visible
        // pixels only, WYSIWYG through a rotated navigation.
        if (t[1] == QLatin1String("region")) {
            if (!needArgs(2)) return false;
            QImage img;
            if (m_w->m_view->navigationRotated()) {
                img = m_w->m_view->renderVisible(full);
            } else {
                const QRect roi = m_w->m_view->visibleImageRect().intersected(full.rect());
                if (roi.isEmpty()) { err = "nothing visible"; return false; }
                img = full.copy(roi);
            }
            if (img.isNull() || !img.save(t[2])) { err = "could not write " + t[2]; return false; }
            return true;
        }
        if (!full.save(t[1])) { err = "could not write " + t[1]; return false; }
        return true;
    }
    if (cmd == QLatin1String("saveann")) {
        // saveann [path] — write the annotation sidecar (shapes, orientation,
        // and the full display block) to <path>, or to the image's default
        // sidecar when omitted. Same writer as Save Annotations (& Display).
        if (m_w->m_currentPath.isEmpty() || !m_w->m_image.isValid()) { err = "no image shown"; return false; }
        QString path = t.size() > 1 ? t[1] : QString();
        if (path.isEmpty()) {
            m_w->saveAnnotations();
            return true;
        }
        if (!m_w->writeAnnotationsFileFor(m_w->m_currentPath, path)) {
            err = "could not write " + path; return false;
        }
        return true;
    }
    if (cmd == QLatin1String("bake")) {
        // bake <path> — the display transfer baked at full precision (the
        // renderFloat path: windowing + LUT interpolation + adjustments, no
        // dither, no 8-bit) as Float32 FITS. This is the reference artifact
        // tools/render_sidecar.py is checked against (tests/conformance).
        if (!needArgs(1)) return false;
        if (!m_w->m_image.isValid()) { err = "no image shown"; return false; }
        ImageData baked = DisplayRenderer::renderFloat(m_w->m_image, m_w->m_model);
        ImageHeader h;
        io::SaveResult sr = io::saveImage(t[1], baked, h);
        if (!sr.ok) { err = sr.error; return false; }
        return true;
    }
    if (cmd == QLatin1String("save")) {
        if (!needArgs(1)) return false;
        if (!m_w->m_image.isValid()) { err = "no image shown"; return false; }
        io::SaveResult sr = io::saveImage(t[1], m_w->m_image, m_w->m_header);
        if (!sr.ok) { err = sr.error; return false; }
        m_w->rebrandSyntheticAfterSave(t[1]);   // same semantics as the GUI saves
        return true;
    }
    if (cmd == QLatin1String("cmap")) {
        // False-colour base map (mono images) — drives the toolbar combo so
        // model, legend and rendering all update through the normal path.
        if (!needArgs(1)) return false;
        if (!m_w->m_cmapCombo) { err = "no colormap control"; return false; }
        static const char* names[] = { "gray", "heat", "viridis", "magma", "inferno", "cividis",
                                       "a", "b", "bb", "he", "cool", "rainbow",
                                       "standard", "i8", "aips0", "sls" };
        const QString want = t[1].toLower();
        for (int i = 0; i < int(sizeof(names) / sizeof(*names)); ++i)
            if (want == QLatin1String(names[i])) { m_w->m_cmapCombo->setCurrentIndex(i); return true; }
        err = "unknown colormap (gray|heat|viridis|magma|inferno|cividis|"
              "a|b|bb|he|cool|rainbow|standard|i8|aips0|sls)";
        return false;
    }
    if (cmd == QLatin1String("cmapmod")) {
        // cmapmod invert on|off   ·   cmapmod split on|off [t]
        if (!needArgs(2)) return false;
        const bool on = t[2].toLower() == QLatin1String("on");
        // click(), not setChecked(): the UI reacts to the user-interaction
        // signal, which programmatic setChecked never emits.
        if (t[1].toLower() == QLatin1String("invert")) {
            if (!m_w->m_invertCheck) { err = "no invert control"; return false; }
            if (m_w->m_invertCheck->isChecked() != on) m_w->m_invertCheck->click();
            return true;
        }
        if (t[1].toLower() == QLatin1String("split")) {
            if (!m_w->m_splitCheck) { err = "no split control"; return false; }
            if (t.size() > 3 && m_w->m_splitSlider)
                m_w->m_splitSlider->setValue(int(t[3].toDouble() * 100));
            if (m_w->m_splitCheck->isChecked() != on) m_w->m_splitCheck->click();
            return true;
        }
        err = "cmapmod invert|split on|off [t]";
        return false;
    }
    if (cmd == QLatin1String("action")) {
        // Trigger any named menu action (the shortcut-registry names, e.g.
        // toggle_grid). Modal dialogs block the script until closed — avoid.
        if (!needArgs(1)) return false;
        QAction* a = m_w->m_actionRegistry.value(t[1]);
        if (!a) { err = "unknown action \"" + t[1] + "\""; return false; }
        a->trigger();
        return true;
    }
    if (cmd == QLatin1String("panels")) {
        // panels on|off — the Image Only toggle (hide/show all overlay boxes).
        if (!needArgs(1)) return false;
        const bool wantPanels = t[1].toLower() != QLatin1String("off");
        if (wantPanels == m_w->m_imageOnly) m_w->toggleImageOnly();
        return true;
    }
    if (cmd == QLatin1String("crop")) {
        // crop <x> <y> <w> <h>  |  crop view — full-depth crop into a new
        // in-memory entry (WCS rebased, annotations translated).
        if (!needArgs(1)) return false;
        if (!m_w->m_image.isValid()) { err = "no image shown"; return false; }
        if (t[1].toLower() == QLatin1String("view")) {
            m_w->cropCurrentToRect(m_w->m_view->visibleImageRect());
            return true;
        }
        if (t.size() < 5) { err = "crop <x> <y> <w> <h> | crop view"; return false; }
        m_w->cropCurrentToRect(QRect(t[1].toInt(), t[2].toInt(),
                                     t[3].toInt(), t[4].toInt()));
        return true;
    }
    if (cmd == QLatin1String("tag")) {
        // tag on|off|toggle — strictly the CURRENT row's keep-check. The
        // guard keeps the UI's checkbox→selection fan-out (and B's group
        // toggle) out of the script API: scripts stay single-row.
        if (!needArgs(1)) return false;
        QListWidgetItem* it = m_w->m_fileList->currentItem();
        if (!it) { err = "no current image"; return false; }
        const QString w = t[1].toLower();
        m_w->m_tagPropagating = true;
        if      (w == QLatin1String("on"))     it->setCheckState(Qt::Checked);
        else if (w == QLatin1String("off"))    it->setCheckState(Qt::Unchecked);
        else if (w == QLatin1String("toggle"))
            it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked
                                                              : Qt::Checked);
        else { m_w->m_tagPropagating = false; err = "tag on|off|toggle"; return false; }
        m_w->m_tagPropagating = false;
        return true;
    }
    if (cmd == QLatin1String("tagsort")) {
        m_w->sortListByTag();
        return true;
    }
    if (cmd == QLatin1String("tagremove")) {
        if (!needArgs(1)) return false;
        const QString w = t[1].toLower();
        if (w != QLatin1String("checked") && w != QLatin1String("unchecked")) {
            err = "tagremove checked|unchecked"; return false;
        }
        m_w->removeTaggedFromList(w == QLatin1String("checked"));
        return true;
    }
    if (cmd == QLatin1String("tagmove")) {
        if (!needArgs(2)) return false;
        const QString w = t[1].toLower();
        if (w != QLatin1String("checked") && w != QLatin1String("unchecked")) {
            err = "tagmove checked|unchecked <dir>"; return false;
        }
        m_w->moveTaggedFiles(w == QLatin1String("checked"),
                             QStringList(t.mid(2)).join(QLatin1Char(' ')));
        return true;
    }
    if (cmd == QLatin1String("stfall")) {
        // Share the current stretch with every image in the list.
        if (!m_w->m_image.isValid()) { err = "no image shown"; return false; }
        m_w->applyStretchToAllList();
        return true;
    }
    if (cmd == QLatin1String("debayer")) {
        // debayer auto|off|rggb|bggr|grbg|gbrg [rcd|bilinear|superpixel] [all]
        if (!needArgs(1)) return false;
        if (m_w->m_currentPath.isEmpty()) { err = "no image shown"; return false; }
        int argc = t.size();
        bool applyAll = false;
        if (argc > 2 && t[argc - 1].toLower() == QLatin1String("all")) {
            applyAll = true;
            --argc;
        }
        int method = MainWindow::kKeepDebayer;
        if (argc > 2) {
            const QString meth = t[2].toLower();
            method = meth == QLatin1String("superpixel") ? 0
                   : meth == QLatin1String("bilinear")   ? 1
                   : meth == QLatin1String("rcd")        ? 2 : -1;
            if (method < 0) { err = "method rcd|bilinear|superpixel"; return false; }
        }
        const QString want = t[1].toLower();
        int mode = 0;
        if      (want == QLatin1String("auto")) mode = 0;
        else if (want == QLatin1String("off"))  mode = -1;
        else {
            const BayerPattern p = bayerPatternFromString(want.toLatin1().constData());
            if (p == BayerPattern::None) { err = "debayer auto|off|rggb|bggr|grbg|gbrg"; return false; }
            mode = int(p);
        }
        m_w->requestDebayerChange(mode, method);           // undoable
        if (applyAll) m_w->applyDebayerToAll();
        return true;
    }
    if (cmd == QLatin1String("transport")) {
        // transport <row> [strengthPct] — colour-transport the displayed
        // image toward list row n (1-based) as reference; result becomes a
        // new display-ready list entry (same as Tools > Transport Colors).
        if (!needArgs(1)) return false;
        const int row = t[1].toInt() - 1;
        if (row < 0 || row >= m_w->m_fileList->count()) { err = "row out of range"; return false; }
        const QString key = m_w->m_fileList->item(row)->data(Qt::UserRole).toString();
        const int strength = t.size() > 2 ? t[2].toInt() : 100;
        const bool asStretch = t.size() > 3 &&
                               t[3].toLower() == QLatin1String("stretch");
        const bool fitColour = asStretch && t.size() > 4 &&
                               (t[4].toLower() == QLatin1String("colour") ||
                                t[4].toLower() == QLatin1String("color"));
        QString terr;
        if (!m_w->runColorTransport(key, strength, &terr, asStretch, fitColour)) {
            err = terr.isEmpty() ? QStringLiteral("transport failed") : terr;
            return false;
        }
        return true;
    }
    if (cmd == QLatin1String("dialog")) {
        // Open a dialog NON-modally for scripted captures (exec() would block
        // the script), or close the one that's open.
        if (!needArgs(1)) return false;
        const QString which = t[1].toLower();
        if (which == QLatin1String("close")) {
            if (m_dialog) m_dialog->deleteLater();
            m_dialog.clear();
            return true;
        }
        if (m_dialog) { err = "a dialog is already open (dialog close first)"; return false; }
        QDialog* d = nullptr;
        if      (which == QLatin1String("rotate"))      d = m_w->makeRotateDialog();
        else if (which == QLatin1String("combine")) {
            QString why;
            CombineDialog* cd = m_w->makeCombineDialog(&why);
            if (!cd) { err = why; return false; }
            // Non-modal: land the result on Create Image, as the modal
            // slot does after exec().
            connect(cd, &QDialog::accepted, m_w,
                    [this, cd] { m_w->adoptCombineResult(*cd); });
            d = cd;
        }
        else if (which == QLatin1String("preferences")) d = new PreferencesDialog(m_w);
        else { err = "dialog rotate|combine|preferences|close"; return false; }
        if (!d) { err = "cannot open dialog (no image?)"; return false; }
        m_dialog = d;
        d->show();
        return true;
    }
    if (cmd == QLatin1String("dlgclick")) {
        // Click a button in the open dialog by its visible text (e.g. a
        // Combine preset): dlgclick SHO
        if (!needArgs(1)) return false;
        if (!m_dialog) { err = "no dialog open"; return false; }
        const QString want = QStringList(t.mid(1)).join(QLatin1Char(' '));
        const auto buttons = m_dialog->findChildren<QPushButton*>();
        for (QPushButton* b : buttons)
            if (QString(b->text()).remove(QLatin1Char('&')) == want) { b->click(); return true; }
        err = "no button \"" + want + "\" in the dialog";
        return false;
    }
    if (cmd == QLatin1String("dlgcombo")) {
        // Set the n-th combo box (creation order, 0-based) of the open dialog
        // to the entry whose text starts with the given prefix:
        //   dlgcombo 0 S      -> "S (SII)"
        if (!needArgs(2)) return false;
        if (!m_dialog) { err = "no dialog open"; return false; }
        const auto combos = m_dialog->findChildren<QComboBox*>();
        const int n = t[1].toInt();
        if (n < 0 || n >= combos.size()) {
            err = QStringLiteral("combo index out of range (dialog has %1)").arg(combos.size());
            return false;
        }
        const QString want = QStringList(t.mid(2)).join(QLatin1Char(' ')).toLower();
        for (int i = 0; i < combos[n]->count(); ++i)
            if (combos[n]->itemText(i).toLower().startsWith(want)) {
                combos[n]->setCurrentIndex(i);
                return true;
            }
        err = "no entry starting with \"" + want + "\"";
        return false;
    }
    if (cmd == QLatin1String("screenshot")) {
        // Render the whole main window (widget tree, docks and all) — or,
        // with the `dialog` argument, the dialog opened by the `dialog`
        // command — to a PNG. Works on the offscreen platform too: the basis
        // for reproducible documentation captures.
        if (!needArgs(1)) return false;
        // Drain the async display pipeline first: a grab taken while a
        // re-render is in flight captures the previous frame.
        QElapsedTimer et; et.start();
        while (et.elapsed() < 15000 && m_w->m_renderWatcher &&
               (m_w->m_renderWatcher->isRunning() || m_w->m_renderPending))
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QWidget* target = m_w;
        if (t.size() > 2 && t[2].toLower() == QLatin1String("dialog")) {
            if (!m_dialog) { err = "no dialog open"; return false; }
            target = m_dialog;
        }
        const QPixmap px = target->grab();
        if (px.isNull() || !px.save(t[1])) { err = "could not write " + t[1]; return false; }
        return true;
    }
    if (cmd == QLatin1String("waitloaded")) {
        // Block (pumping the event loop) until the current image and its
        // statistics are ready — deterministic where fixed sleeps race the
        // async decode/render on slow machines (CI runners).
        const int timeout = t.size() > 1 ? t[1].toInt() : 10000;
        QElapsedTimer et; et.start();
        while (et.elapsed() < timeout &&
               (!m_w->m_image.isValid() || m_w->m_curStats.empty()))
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!m_w->m_image.isValid() || m_w->m_curStats.empty()) {
            err = QStringLiteral("image not loaded within %1 ms").arg(timeout);
            return false;
        }
        return true;
    }
    if (cmd == QLatin1String("assert")) return doAssert(t, err);
    if (cmd == QLatin1String("sleep")) {
        if (!needArgs(1)) return false;
        QTimer::singleShot(t[1].toInt(), this, &ScriptRunner::step);
        return true;
    }
    err = "unknown command";
    return false;
}

bool ScriptRunner::doAssert(const QStringList& t, QString& err) {
    if (t.size() < 2) { err = "assert what?"; return false; }
    const ImageData& img = m_w->m_image;
    const QString what = t[1];

    if (what == QLatin1String("size")) {
        if (t.size() < 4) { err = "assert size W H"; return false; }
        const int w = t[2].toInt(), h = t[3].toInt();
        if (img.width() != w || img.height() != h) {
            err = QStringLiteral("size is %1x%2, expected %3x%4")
                      .arg(img.width()).arg(img.height()).arg(w).arg(h);
            return false;
        }
        return true;
    }
    if (what == QLatin1String("rows")) {
        if (t.size() < 3) { err = "assert rows n"; return false; }
        const int n = m_w->m_fileList->count();
        if (n != t[2].toInt()) {
            err = QStringLiteral("list has %1 row(s)").arg(n);
            return false;
        }
        return true;
    }
    if (what == QLatin1String("adjust")) {
        // assert adjust <name> <value> [tol] — one display adjustment (same
        // names as the `adjust` command). Exercises the sidecar display
        // round-trip and per-image adjustment isolation.
        if (t.size() < 4) { err = "assert adjust name value [tol]"; return false; }
        const AdjustParams a = m_w->m_model.adjust();
        const QString k = t[2].toLower();
        double got = 0;
        if      (k == "brightness")  got = a.brightness;
        else if (k == "contrast")    got = a.contrast;
        else if (k == "gamma")       got = a.gamma;
        else if (k == "shadows")     got = a.shadows;
        else if (k == "highlights")  got = a.highlights;
        else if (k == "blackpoint")  got = a.blackpoint;
        else if (k == "whitepoint")  got = a.whitepoint;
        else if (k == "temperature") got = a.temperature;
        else if (k == "tint")        got = a.tint;
        else if (k == "hue")         got = a.hue;
        else if (k == "saturation")  got = a.saturation;
        else if (k == "vibrance")    got = a.vibrance;
        else { err = "unknown adjustment"; return false; }
        const double tol = t.size() > 4 ? t[4].toDouble() : 1e-6;
        if (std::abs(got - t[3].toDouble()) > tol) {
            err = QStringLiteral("adjust %1 is %2").arg(k).arg(got, 0, 'g', 9);
            return false;
        }
        return true;
    }
    if (what == QLatin1String("fn")) {
        // assert fn linear|log|asinh|ghs — the current transfer function.
        if (t.size() < 3) { err = "assert fn linear|log|asinh|ghs"; return false; }
        static const char* names[] = { "linear", "log", "asinh", "ghs" };
        const QString got = QLatin1String(names[int(m_w->m_model.fn())]);
        if (got != t[2].toLower()) { err = QStringLiteral("fn is %1").arg(got); return false; }
        return true;
    }
    if (what == QLatin1String("levels")) {
        // assert levels <c> <min> — the displayed image's channel c holds at
        // least <min> distinct values (up to 200k samples). Guards against
        // quantization creeping into data-producing paths (the combine bake
        // posterized through a nearest-LUT while the display interpolated).
        if (t.size() < 4) { err = "assert levels <c> <min>"; return false; }
        const ImageData& img = m_w->m_image;
        const int c = t[2].toInt();
        if (!img.isValid() || c < 0 || c >= img.channels()) { err = "bad channel"; return false; }
        const float* p = img.plane<float>(c);
        const std::size_t n = img.samplesPerChannel();
        const std::size_t step = n > 200000 ? n / 200000 : 1;
        std::set<float> uniq;
        for (std::size_t i = 0; i < n; i += step)
            if (std::isfinite(p[i])) uniq.insert(p[i]);
        if (int(uniq.size()) < t[3].toInt()) {
            err = QStringLiteral("only %1 distinct values").arg(uniq.size());
            return false;
        }
        return true;
    }
    if (what == QLatin1String("stretch")) {
        // assert stretch <c> <black> <mid> <white> [tol] — the current
        // image's channel-c stretch in RAW data units (as the value boxes
        // show). Exercises the DisplayFunction import + Mobius rebase.
        if (t.size() < 6) { err = "assert stretch c black mid white [tol]"; return false; }
        const int c = t[2].toInt();
        if (c < 0 || c > 2) { err = "channel is 0..2"; return false; }
        const double tol = t.size() > 6 ? t[6].toDouble() : 1e-6;
        const ChannelStretch cs = m_w->m_model.channel(c);
        const double lo = m_w->m_model.lo(c), hi = m_w->m_model.hi(c);
        const double got[3] = { lo + cs.black * (hi - lo),
                                lo + cs.mid   * (hi - lo),
                                lo + cs.white * (hi - lo) };
        const double want[3] = { t[3].toDouble(), t[4].toDouble(), t[5].toDouble() };
        for (int i = 0; i < 3; ++i)
            if (std::abs(got[i] - want[i]) > tol) {
                err = QStringLiteral("stretch ch%1 is %2 / %3 / %4").arg(c)
                          .arg(got[0], 0, 'g', 9).arg(got[1], 0, 'g', 9)
                          .arg(got[2], 0, 'g', 9);
                return false;
            }
        return true;
    }
    if (what == QLatin1String("name")) {
        // assert name <text> — the current list row's visible text (e.g. the
        // saved filename after a synthetic entry was written to disk).
        if (t.size() < 3) { err = "assert name text"; return false; }
        const QString expect = QStringList(t.mid(2)).join(QLatin1Char(' '));
        QListWidgetItem* cur = m_w->m_fileList->currentItem();
        const QString got = cur ? cur->text() : QString();
        if (got != expect) {
            err = QStringLiteral("current row is \"%1\"").arg(got);
            return false;
        }
        return true;
    }
    if (what == QLatin1String("channels")) {
        if (t.size() < 3) { err = "assert channels n"; return false; }
        if (img.channels() != t[2].toInt()) {
            err = QStringLiteral("channels is %1").arg(img.channels());
            return false;
        }
        return true;
    }
    if (what == QLatin1String("pixel")) {
        // mono: x y v [tol]   rgb: x y r g b [tol]
        if (!img.isValid()) { err = "no image"; return false; }
        if (t.size() < 5) { err = "assert pixel x y v… [tol]"; return false; }
        const int x = t[2].toInt(), y = t[3].toInt();
        if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) { err = "pixel out of bounds"; return false; }
        const std::size_t i = std::size_t(y) * img.width() + x;
        const int ch = img.channels() >= 3 ? 3 : 1;
        const int nvals = (ch == 3) ? 3 : 1;
        if (t.size() < 4 + nvals) { err = "not enough expected values"; return false; }
        const double tol = (t.size() > 4 + nvals) ? t[4 + nvals].toDouble() : 1e-4;
        for (int c = 0; c < nvals; ++c) {
            const double got = img.plane<float>(c)[i];
            const double want = t[4 + c].toDouble();
            if (std::fabs(got - want) > tol) {
                err = QStringLiteral("pixel(%1,%2) ch%3 = %4, expected %5 ±%6")
                          .arg(x).arg(y).arg(c).arg(got).arg(want).arg(tol);
                return false;
            }
        }
        return true;
    }
    if (what == QLatin1String("range")) {
        if (!img.isValid()) { err = "no image"; return false; }
        if (t.size() < 4) { err = "assert range min max [tol]"; return false; }
        const double tol = t.size() > 4 ? t[4].toDouble() : 1e-3;
        const float* p = img.plane<float>(0);
        float mn = p[0], mx = p[0];
        for (std::size_t i = 0; i < img.samplesPerChannel(); ++i) {
            if (std::isnan(p[i])) continue;
            mn = std::min(mn, p[i]); mx = std::max(mx, p[i]);
        }
        if (std::fabs(mn - t[2].toDouble()) > tol || std::fabs(mx - t[3].toDouble()) > tol) {
            err = QStringLiteral("range is [%1, %2]").arg(mn).arg(mx);
            return false;
        }
        return true;
    }
    err = "unknown assertion";
    return false;
}

} // namespace astro

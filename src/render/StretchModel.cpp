#include "render/StretchModel.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace astro {

void StretchModel::autoStretch(const std::vector<ChannelStats>& stats) {
    m_fn = StretchFn::Linear;
    const int n = std::min<int>(int(stats.size()), 3);
    for (int c = 0; c < n; ++c) {
        const double mn = stats[c].min, mx = stats[c].max;
        const double span = std::max(1e-6, mx - mn);
        m_lo[c] = mn;
        m_hi[c] = mx;
        m_chan[c] = stfFor(stats[c].median, stats[c].mad, mn, span);
    }
    emit changed();
}

// One STF computed from the pooled channel statistics, applied IDENTICALLY to
// all channels (same display range, same B/M/W). Because every channel goes
// through the same transfer, the R:G:B ratios — the colour balance — are
// preserved; the per-channel autoStretch above instead equalises the channels
// (useful for uncalibrated data, but it neutralises the colour cast).
void StretchModel::autoStretchLinked(const std::vector<ChannelStats>& stats) {
    m_fn = StretchFn::Linear;
    const int n = std::min<int>(int(stats.size()), 3);
    if (n == 0) { emit changed(); return; }

    // Pooled range: cover all channels with one window.
    double mn = stats[0].min, mx = stats[0].max;
    double medSum = 0, madSum = 0;
    for (int c = 0; c < n; ++c) {
        mn = std::min(mn, double(stats[c].min));
        mx = std::max(mx, double(stats[c].max));
        medSum += stats[c].median;
        madSum += stats[c].mad;
    }
    const double span = std::max(1e-6, mx - mn);
    const ChannelStretch cs = stfFor(medSum / n, madSum / n, mn, span);

    for (int c = 0; c < 3; ++c) { m_lo[c] = mn; m_hi[c] = mx; m_chan[c] = cs; }
    emit changed();
}

// Shared STF solver: shadows clipped just below the background, midtone chosen
// so the background displays at ~0.25.
ChannelStretch StretchModel::stfFor(double median, double mad, double mn, double span) {
    const double nMed = (median - mn) / span;
    double nMad = mad / span;
    if (nMad < 1e-6) nMad = 0.01;

    ChannelStretch cs;
    cs.black = std::min(0.5, std::max(0.0, nMed - 2.8 * nMad));   // clip shadows just below background
    cs.white = 1.0;

    double x = (nMed - cs.black) / std::max(1e-6, cs.white - cs.black);
    x = std::min(0.95, std::max(0.02, x));
    const double y = 0.25;
    double m = x * (y - 1.0) / ((2.0 * y * x) - y - x);
    if (!(m > 0.0 && m < 1.0)) m = 0.5;

    cs.mid = cs.black + m * (cs.white - cs.black);
    cs.mid = std::min(cs.white - 1e-3, std::max(cs.black + 1e-3, cs.mid));
    return cs;
}

void StretchModel::linearWindow(const std::vector<ChannelStats>& stats) {
    // Plain min→max linear ramp (same look as Reset, but with the display range
    // fitted to the data). No percentile "boost" — the user asked first views to
    // be predictable; Auto STF / Auto Linked remain the boosted options.
    m_fn = StretchFn::Linear;
    const int n = std::min<int>(int(stats.size()), 3);
    for (int c = 0; c < n; ++c) {
        m_lo[c] = stats[c].min;
        m_hi[c] = std::max(double(stats[c].min) + 1e-6, double(stats[c].max));
        m_chan[c] = ChannelStretch{};        // black 0, mid 0.5, white 1 → identity ramp
    }
    emit changed();
}

void StretchModel::reset() {
    m_fn = StretchFn::Linear;
    m_adj = AdjustParams{};                 // adjustments are display state too
    for (int c = 0; c < 3; ++c) m_chan[c] = ChannelStretch{};
    m_ghs = GHSParams{};
    emit changed();
}

// ---- sidecar (de)serialization ----------------------------------------------

namespace {
const char* fnName(StretchFn f) {
    switch (f) {
        case StretchFn::Linear: return "linear";
        case StretchFn::Log:    return "log";
        case StretchFn::Asinh:  return "asinh";
        case StretchFn::GHS:    return "ghs";
    }
    return "asinh";
}
StretchFn fnFromName(const QString& n, bool* ok) {
    *ok = true;
    if (n == QLatin1String("linear")) return StretchFn::Linear;
    if (n == QLatin1String("log"))    return StretchFn::Log;
    if (n == QLatin1String("asinh"))  return StretchFn::Asinh;
    if (n == QLatin1String("ghs"))    return StretchFn::GHS;
    *ok = false;
    return StretchFn::Asinh;
}
Colormap cmapFromName(const QString& n) {
    for (int i = 0; i < kColormapCount; ++i)
        if (n == QLatin1String(colormapName(static_cast<Colormap>(i))))
            return static_cast<Colormap>(i);
    return Colormap::Gray;
}
} // namespace

QJsonObject StretchModel::stateToJson(const State& s) {
    QJsonObject o;
    o["fn"] = fnName(s.fn);
    o["count"] = s.count;
    QJsonArray chans;
    for (int c = 0; c < 3; ++c) {
        QJsonObject ch;
        ch["black"] = s.chan[c].black;
        ch["mid"]   = s.chan[c].mid;
        ch["white"] = s.chan[c].white;
        ch["lo"]    = s.lo[c];
        ch["hi"]    = s.hi[c];
        chans.append(ch);
    }
    o["channels"] = chans;
    QJsonObject g;
    g["D"] = s.ghs.D; g["b"] = s.ghs.b; g["SP"] = s.ghs.SP;
    g["LP"] = s.ghs.LP; g["HP"] = s.ghs.HP;
    o["ghs"] = g;
    o["cmap"] = QLatin1String(colormapName(s.cmap));
    o["cmapInvert"] = s.cmapInvert;
    o["cmapSplit"]  = s.cmapSplit;
    o["split"]      = s.split;
    QJsonObject a;
    a["blackpoint"] = s.adj.blackpoint;   a["whitepoint"] = s.adj.whitepoint;
    a["shadows"]    = s.adj.shadows;      a["highlights"] = s.adj.highlights;
    a["brightness"] = s.adj.brightness;   a["contrast"]   = s.adj.contrast;
    a["gamma"]      = s.adj.gamma;        a["temperature"]= s.adj.temperature;
    a["tint"]       = s.adj.tint;         a["hue"]        = s.adj.hue;
    a["saturation"] = s.adj.saturation;   a["vibrance"]   = s.adj.vibrance;
    o["adjust"] = a;
    return o;
}

StretchModel::State StretchModel::stateFromJson(const QJsonObject& o) {
    State s;
    bool fnOk = false;
    s.fn = fnFromName(o.value("fn").toString(), &fnOk);
    const QJsonArray chans = o.value("channels").toArray();
    if (!fnOk || chans.size() != 3) return s;          // .valid stays false
    s.count = std::clamp(o.value("count").toInt(3), 1, 3);
    for (int c = 0; c < 3; ++c) {
        const QJsonObject ch = chans[c].toObject();
        s.chan[c].black = ch.value("black").toDouble(0.0);
        s.chan[c].mid   = ch.value("mid").toDouble(0.5);
        s.chan[c].white = ch.value("white").toDouble(1.0);
        s.lo[c]         = ch.value("lo").toDouble(0.0);
        s.hi[c]         = ch.value("hi").toDouble(1.0);
    }
    const QJsonObject g = o.value("ghs").toObject();
    s.ghs.D  = g.value("D").toDouble(s.ghs.D);
    s.ghs.b  = g.value("b").toDouble(s.ghs.b);
    s.ghs.SP = g.value("SP").toDouble(s.ghs.SP);
    s.ghs.LP = g.value("LP").toDouble(s.ghs.LP);
    s.ghs.HP = g.value("HP").toDouble(s.ghs.HP);
    s.cmap = cmapFromName(o.value("cmap").toString());
    s.cmapInvert = o.value("cmapInvert").toBool(false);
    s.cmapSplit  = o.value("cmapSplit").toBool(false);
    s.split      = o.value("split").toDouble(0.25);
    const QJsonObject a = o.value("adjust").toObject();
    s.adj.blackpoint = a.value("blackpoint").toDouble(0.0);
    s.adj.whitepoint = a.value("whitepoint").toDouble(1.0);
    s.adj.shadows    = a.value("shadows").toDouble(0.0);
    s.adj.highlights = a.value("highlights").toDouble(0.0);
    s.adj.brightness = a.value("brightness").toDouble(0.0);
    s.adj.contrast   = a.value("contrast").toDouble(0.0);
    s.adj.gamma      = a.value("gamma").toDouble(1.0);
    s.adj.temperature= a.value("temperature").toDouble(0.0);
    s.adj.tint       = a.value("tint").toDouble(0.0);
    s.adj.hue        = a.value("hue").toDouble(0.0);
    s.adj.saturation = a.value("saturation").toDouble(0.0);
    s.adj.vibrance   = a.value("vibrance").toDouble(0.0);
    s.valid = true;
    return s;
}

} // namespace astro

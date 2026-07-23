#include "render/StretchModel.h"
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
    for (int c = 0; c < 3; ++c) m_chan[c] = ChannelStretch{};
    m_ghs = GHSParams{};
    emit changed();
}

} // namespace astro

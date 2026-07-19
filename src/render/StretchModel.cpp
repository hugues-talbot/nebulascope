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

        const double nMed = (double(stats[c].median) - mn) / span;
        double nMad = double(stats[c].mad) / span;
        if (nMad < 1e-6) nMad = 0.01;

        ChannelStretch cs;
        cs.black = std::min(0.5, std::max(0.0, nMed - 2.8 * nMad));   // clip shadows just below background
        cs.white = 1.0;

        // Solve the MTF midtone so the background (nMed) displays at ~0.25.
        double x = (nMed - cs.black) / std::max(1e-6, cs.white - cs.black);
        x = std::min(0.95, std::max(0.02, x));
        const double y = 0.25;
        double m = x * (y - 1.0) / ((2.0 * y * x) - y - x);
        if (!(m > 0.0 && m < 1.0)) m = 0.5;

        cs.mid = cs.black + m * (cs.white - cs.black);
        cs.mid = std::min(cs.white - 1e-3, std::max(cs.black + 1e-3, cs.mid));
        m_chan[c] = cs;
    }
    emit changed();
}

void StretchModel::linearWindow(const std::vector<ChannelStats>& stats) {
    m_fn = StretchFn::Linear;
    const int n = std::min<int>(int(stats.size()), 3);
    for (int c = 0; c < n; ++c) {
        const double mn = stats[c].min, mx = stats[c].max;
        const double span = std::max(1e-6, mx - mn);
        m_lo[c] = mn;
        m_hi[c] = mx;

        ChannelStretch cs;
        cs.black = 0.0;                                              // preserve the lowest values
        cs.white = (double(stats[c].p99) - mn) / span;               // window top at the 99th percentile
        cs.white = std::min(1.0, std::max(0.02, cs.white));
        cs.mid   = 0.5 * (cs.black + cs.white);                      // MTF m = 0.5 → identity ramp
        m_chan[c] = cs;
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

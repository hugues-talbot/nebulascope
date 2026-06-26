#pragma once
//
// StretchModel — the single source of truth for how the image is displayed.
// Both the histogram views and the image renderer read/write this one object,
// so a change in either place is reflected everywhere (exactly like the mockup).
//
#include "core/Stretch.h"
#include "core/ImageStats.h"
#include <QObject>
#include <vector>

namespace astro {

class StretchModel : public QObject {
    Q_OBJECT
public:
    explicit StretchModel(QObject* parent = nullptr) : QObject(parent) {}

    StretchFn fn() const { return m_fn; }
    void setFn(StretchFn f) { if (f != m_fn) { m_fn = f; emit changed(); } }

    const ChannelStretch& channel(int c) const { return m_chan[clampC(c)]; }
    void setChannel(int c, const ChannelStretch& cs) { m_chan[clampC(c)] = cs; emit changed(); }

    const GHSParams& ghs() const { return m_ghs; }
    void setGhs(const GHSParams& g) { m_ghs = g; emit changed(); }

    double lo(int c) const { return m_lo[clampC(c)]; }
    double hi(int c) const { return m_hi[clampC(c)]; }
    void setRange(int c, double lo, double hi) { c = clampC(c); m_lo[c] = lo; m_hi[c] = hi; }

    int channelCount() const { return m_count; }
    void setChannelCount(int n) { m_count = n < 1 ? 1 : (n > 3 ? 3 : n); }

    // STF-style auto stretch: sets per-channel display ranges and a midtone that
    // lifts the background to ~0.25. Switches to the Linear+MTF transfer.
    void autoStretch(const std::vector<ChannelStats>& stats);
    void reset();

signals:
    void changed();

private:
    static int clampC(int c) { return c < 0 ? 0 : (c > 2 ? 2 : c); }

    StretchFn m_fn = StretchFn::Asinh;
    int m_count = 3;
    ChannelStretch m_chan[3];
    GHSParams m_ghs;
    double m_lo[3] = {0, 0, 0};
    double m_hi[3] = {1, 1, 1};
};

} // namespace astro

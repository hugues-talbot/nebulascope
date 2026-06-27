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

    // A full snapshot of the display parameters, so each image in the list can
    // remember (and later restore) the last stretch the user applied to it.
    struct State {
        bool           valid = false;
        StretchFn      fn = StretchFn::Asinh;
        int            count = 3;
        ChannelStretch chan[3];
        GHSParams      ghs;
        double         lo[3] = {0, 0, 0};
        double         hi[3] = {1, 1, 1};
    };
    State state() const {
        State s;
        s.valid = true; s.fn = m_fn; s.count = m_count; s.ghs = m_ghs;
        for (int c = 0; c < 3; ++c) { s.chan[c] = m_chan[c]; s.lo[c] = m_lo[c]; s.hi[c] = m_hi[c]; }
        return s;
    }
    void setState(const State& s) {
        if (!s.valid) return;
        m_fn = s.fn; m_count = s.count; m_ghs = s.ghs;
        for (int c = 0; c < 3; ++c) { m_chan[c] = s.chan[c]; m_lo[c] = s.lo[c]; m_hi[c] = s.hi[c]; }
        emit changed();
    }

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

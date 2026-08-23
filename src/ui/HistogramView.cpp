#include "ui/HistogramView.h"
#include "core/Stretch.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace astro {

static const QColor CH_COL[3] = { QColor("#ff6b6b"), QColor("#3fd07f"), QColor("#5aa9ff") };
static const QColor GHS_COL("#ffd27f");

HistogramView::HistogramView(StretchModel* model, QWidget* parent)
    : QWidget(parent), m_model(model) {
    setMinimumHeight(220);
    setMouseTracking(true);
    connect(m_model, &StretchModel::changed, this, [this]{ update(); });
}

void HistogramView::setSource(const ImageData* img) {
    m_src = img;
    m_binSrc = nullptr;   // the ImageData lives at a fixed address (MainWindow's
                          // member), so a new image can alias the old pointer —
                          // always invalidate the rebin cache on source change.
    recomputeHistogram();
}

void HistogramView::setActiveChannel(int c) {
    m_active = (c < -1 || c > 2) ? -1 : c;
    update();
}

void HistogramView::recomputeHistogram() {
    if (!m_src) { m_hist.clear(); m_binSrc = nullptr; update(); return; }
    const int ch = m_src->channels();
    const int bins = 256;
    const std::size_t n = m_src->samplesPerChannel();
    const std::size_t step = n > 400000 ? n / 400000 : 1;

    double a, b; viewRange(a, b);   // bin only the visible window

    // Skip the (expensive) rebin when nothing that feeds the bins has changed.
    // This fires on every StretchModel::changed — e.g. per drag tick of the Mid
    // handle or a GHS slider — where only the curve, not the bins, moved.
    bool same = (m_binSrc == m_src && m_binA == a && m_binB == b && !m_hist.empty());
    if (same)
        for (int c = 0; c < ch && same; ++c)
            same = (m_binLo[c] == m_model->lo(c) && m_binHi[c] == m_model->hi(c));
    if (same) { update(); return; }

    m_hist.clear();
    const double vspan = std::max(1e-6, b - a);

    for (int c = 0; c < ch; ++c) {
        std::vector<float> hb(bins, 0.0f);
        const float* p = m_src->plane<float>(c);
        const double lo = m_model->lo(c), hi = m_model->hi(c);
        const double span = std::max(1e-9, hi - lo);
        for (std::size_t i = 0; i < n; i += step) {
            const double pv = double(p[i]);
            if (!std::isfinite(pv)) continue;            // skip NaN/Inf blanks
            const double u = (pv - lo) / span;           // normalized over [lo,hi]
            if (u < a || u > b) continue;                // outside the view window
            const double x = (u - a) / vspan;            // 0..1 across the plot
            int bi = int(x * (bins - 1));
            if (bi < 0) bi = 0; else if (bi > bins - 1) bi = bins - 1;
            hb[bi] += 1.0f;
        }
        m_hist.push_back(std::move(hb));                 // RAW counts; scaled at paint
    }
    m_binSrc = m_src; m_binA = a; m_binB = b;
    for (int c = 0; c < 3; ++c) { m_binLo[c] = m_model->lo(c); m_binHi[c] = m_model->hi(c); }
    update();
}

QRectF HistogramView::plotRect() const {
    return QRectF(rect()).adjusted(10, 16, -10, -14);   // headroom for handle grips
}

void HistogramView::viewRange(double& a, double& b) const {
    if (m_axis == AxisMode::Manual) { a = m_manA; b = m_manB; return; }
    if (m_model->fn() == StretchFn::Linear) {
        // Fit the data AND the handles: a black point below the minimum or a
        // white above the maximum must stay reachable after a refit.
        a = 0.0; b = 1.0;
        for (int c = 0; c < m_model->channelCount(); ++c) {
            const ChannelStretch cs = m_model->channel(c);
            a = std::min(a, cs.black); b = std::max(b, cs.white);
        }
    } else {
        const ChannelStretch w = m_model->channel(0);
        a = w.black; b = w.white;
        if (b - a < 1e-4) { a = 0.0; b = 1.0; }          // safety for a collapsed window
    }
    if (m_axis == AxisMode::Wide) {                      // half a span of air on each side
        const double span = b - a;
        a -= 0.5 * span; b += 0.5 * span;
    }
}

void HistogramView::setWideAxis(bool on) {
    const AxisMode want = on ? AxisMode::Wide : AxisMode::Auto;
    if (want == m_axis) return;
    m_axis = want;
    recomputeHistogram();
    emit axisModeChanged();
}

void HistogramView::resetAxis() {
    if (m_axis == AxisMode::Auto) return;
    m_axis = AxisMode::Auto;
    recomputeHistogram();
    emit axisModeChanged();
}

double HistogramView::modeU(int c) const {
    if (c < 0 || c >= int(m_hist.size())) return std::nan("");
    const auto& hb = m_hist[c];
    int best = -1; float mx = 0.0f;
    for (int i = 0; i < int(hb.size()); ++i) if (hb[i] > mx) { mx = hb[i]; best = i; }
    if (best < 0) return std::nan("");
    double a, b; viewRange(a, b);
    return a + (best + 0.5) / double(hb.size()) * (b - a);
}

void HistogramView::snapSpToMode() {
    if (m_model->fn() != StretchFn::GHS) return;
    const int ch = int(m_hist.size());
    const int c = (m_active < 0 || m_active >= ch) ? 0 : m_active;
    const double u = modeU(c);
    if (!std::isfinite(u)) return;
    const ChannelStretch wc = m_model->channel(0);
    const double span = std::max(1e-6, wc.white - wc.black);
    GHSParams g = m_model->ghs();
    const double eps = 0.006;
    double p = (u - wc.black) / span;
    p = std::max(kHandleMin, std::min(kHandleMax, p));
    // Keep the protection zones consistent: LP stays below SP, HP above.
    g.SP = p;
    if (g.LP > g.SP - eps) g.LP = g.SP - eps;
    if (g.HP < g.SP + eps) g.HP = g.SP + eps;
    m_model->setGhs(g);
}

void HistogramView::wheelEvent(QWheelEvent* e) {
    const QRectF r = plotRect();
    double a, b; viewRange(a, b);
    const double span = b - a;
    const QPoint ad = e->angleDelta();
    if (e->modifiers() & Qt::ShiftModifier || (ad.x() != 0 && ad.y() == 0)) {
        // Pan: Shift+wheel (or a horizontal wheel/trackpad swipe).
        const int d = ad.x() != 0 ? ad.x() : ad.y();
        const double shift = -d / 120.0 * 0.1 * span;
        m_manA = a + shift; m_manB = b + shift;
    } else {
        // Zoom about the cursor's value, 15% per notch.
        const double frac = (e->position().x() - r.left()) / std::max(1.0, r.width());
        const double pivot = a + frac * span;
        const double k = std::pow(1.15, -ad.y() / 120.0);
        double na = pivot - (pivot - a) * k, nb = pivot + (b - pivot) * k;
        if (nb - na < 0.002) { e->accept(); return; }   // don't collapse
        m_manA = na; m_manB = nb;
    }
    // Outer bound: the handle domain plus a little air.
    const double outerLo = kHandleMin - 0.5, outerHi = kHandleMax + 0.5;
    if (m_manA < outerLo) { m_manB += outerLo - m_manA; m_manA = outerLo; }
    if (m_manB > outerHi) { m_manA -= m_manB - outerHi; m_manB = outerHi; }
    m_manA = std::max(outerLo, m_manA); m_manB = std::min(outerHi, m_manB);
    m_axis = AxisMode::Manual;
    recomputeHistogram();
    emit axisModeChanged();
    e->accept();
}

void HistogramView::mouseDoubleClickEvent(QMouseEvent* e) {
    // On the GHS symmetry-point grip: snap SP to the histogram peak.
    // Anywhere else: back to the automatic axis range.
    if (m_model->fn() == StretchFn::GHS) {
        const ChannelStretch wc = m_model->channel(0);
        const double span = std::max(1e-6, wc.white - wc.black);
        const double spx = valToX(wc.black + m_model->ghs().SP * span);
        if (std::fabs(e->position().x() - spx) < 10.0) { snapSpToMode(); return; }
    }
    resetAxis();
}

double HistogramView::valToX(double v) const {
    const QRectF r = plotRect();
    double a, b; viewRange(a, b);
    return r.left() + (v - a) / std::max(1e-6, b - a) * r.width();
}
double HistogramView::xToVal(double px) const {
    const QRectF r = plotRect();
    double a, b; viewRange(a, b);
    double v = a + (px - r.left()) / std::max(1.0, r.width()) * (b - a);
    return v < a ? a : (v > b ? b : v);                  // can't drag off the plot
}

void HistogramView::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = plotRect();

    g.fillRect(rect(), QColor("#0b1016"));
    g.fillRect(r, QColor("#070b10"));

    // GHS protection bands (mapped through the black/white window)
    const bool ghs = m_model->fn() == StretchFn::GHS;
    if (ghs) {
        const GHSParams gp = m_model->ghs();
        const ChannelStretch wc = m_model->channel(0);
        const double span = std::max(1e-6, wc.white - wc.black);
        auto wx = [&](double p){ return std::max(r.left(), std::min(r.right(), valToX(wc.black + p * span))); };
        g.fillRect(QRectF(r.left(), r.top(), wx(gp.LP) - r.left(), r.height()), QColor(91, 104, 118, 32));
        g.fillRect(QRectF(wx(gp.HP), r.top(), r.right() - wx(gp.HP), r.height()), QColor(91, 104, 118, 32));
    }

    // grid (fixed fractions of the plot width)
    g.setPen(QColor("#121b24"));
    for (double gx = 0.25; gx < 1.0; gx += 0.25) {
        const double px = r.left() + gx * r.width();
        g.drawLine(QPointF(px, r.top()), QPointF(px, r.bottom()));
    }
    // When the view extends past the data, mark where the data ends (min /
    // max of the axis range) and, if it lies in view and differs from the
    // minimum, the true data zero — so negative values read as negative.
    {
        double va, vb; viewRange(va, vb);
        g.setFont(QFont(g.font().family(), 8));
        auto mark = [&](double u, const QString& label, const QColor& col) {
            if (u < va || u > vb) return;
            const double px = valToX(u);
            g.setPen(QPen(col, 1.0, Qt::DotLine));
            g.drawLine(QPointF(px, r.top()), QPointF(px, r.bottom()));
            g.setPen(col);
            g.drawText(QRectF(px + 2, r.bottom() - 14, 40, 12), Qt::AlignLeft, label);
        };
        if (va < 0.0 || vb > 1.0) {
            mark(0.0, QStringLiteral("min"), QColor("#35455a"));
            mark(1.0, QStringLiteral("max"), QColor("#35455a"));
        }
        const double lo0 = m_model->lo(0), hi0 = m_model->hi(0);
        const double u0 = -lo0 / std::max(1e-12, hi0 - lo0);     // where value 0 falls
        if (std::fabs(u0) > 1e-6) mark(u0, QStringLiteral("0"), QColor("#4a3a2a"));
    }

    // histogram areas (raw counts scaled linear or log per the toggle)
    const int ch = int(m_hist.size());
    for (int c = 0; c < ch; ++c) {
        const auto& hb = m_hist[c];
        float mx = 0.0f;
        for (float v : hb) { const float s = m_logHist ? std::log1p(v) : v; if (s > mx) mx = s; }
        QPainterPath path;
        path.moveTo(r.left(), r.bottom());
        for (int i = 0; i < int(hb.size()); ++i) {
            const double x = r.left() + (double(i) / (hb.size() - 1)) * r.width();
            const double s = m_logHist ? std::log1p(hb[i]) : double(hb[i]);
            const double y = r.bottom() - (mx > 0 ? s / mx : 0.0) * (r.height() - 4);
            path.lineTo(x, y);
        }
        path.lineTo(r.right(), r.bottom());
        path.closeSubpath();
        QColor fill = (ch == 1) ? QColor("#9fb3c8") : CH_COL[c];
        QColor f2 = fill; f2.setAlpha(ch == 1 ? 70 : 55);
        g.setPen(QPen(fill, 1.0));
        g.fillPath(path, f2);
        g.drawPath(path);
    }

    // transfer curve (sampled across the visible window)
    const int N = 512;
    const int curveCh = (m_active < 0 || m_active >= ch) ? 0 : m_active;
    const ChannelStretch cw = m_model->channel(curveCh);
    std::vector<float> lut = buildLut(m_model->fn(), cw, m_model->ghs(), N);
    // The curve must show the FULL data→display mapping: post-stretch tone
    // adjustments are part of it (colour ops are cross-channel — not drawable
    // per channel; they show in the image and colorbar).
    if (m_model->fn() == StretchFn::GHS && !m_model->adjust().toneIdentity())
        for (float& v : lut) v = applyTone(v, m_model->adjust());
    const double wDenom = std::max(1e-6, cw.white - cw.black);
    double va, vb; viewRange(va, vb);
    QPainterPath curve;
    for (int i = 0; i < N; ++i) {
        const double frac = double(i) / (N - 1);
        const double u = va + frac * (vb - va);            // value coord under this x
        double t = (u - cw.black) / wDenom;                // windowed coord
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        // Exact evaluation, not nearest-LUT: an imported display function can
        // put the white point far beyond the data range, leaving the plotted
        // span only a handful of LUT samples — the curve drew as a staircase.
        float yv;
        if (m_model->fn() == StretchFn::GHS) {             // GHS: interpolate the LUT
            const double f = t * (N - 1);
            const int i0 = int(f);
            const int i1 = i0 < N - 1 ? i0 + 1 : i0;
            const float fr = float(f - i0);
            yv = lut[i0] * (1.0f - fr) + lut[i1] * fr;
        } else {
            yv = float(transferAt(t, m_model->fn(), cw, m_model->ghs()));
        }
        if (m_model->fn() != StretchFn::GHS && !m_model->adjust().toneIdentity())
            yv = applyTone(yv, m_model->adjust());
        const double x = r.left() + frac * r.width();
        const double y = r.bottom() - yv * (r.height() - 4);
        if (i == 0) curve.moveTo(x, y); else curve.lineTo(x, y);
    }
    g.setPen(QPen(ghs ? GHS_COL : QColor("#eef3f8"), 1.8));
    g.drawPath(curve);

    // Mode marker: a small triangle at the histogram peak of the curve
    // channel (the GHS tutorial's anchor for the symmetry point).
    {
        const double mu = modeU(curveCh);
        if (std::isfinite(mu)) {
            const double px = valToX(mu);
            QPainterPath tri;
            tri.moveTo(px, r.top() + 9); tri.lineTo(px - 4, r.top() + 1); tri.lineTo(px + 4, r.top() + 1);
            tri.closeSubpath();
            g.fillPath(tri, QColor(ch == 1 ? "#9fb3c8" : CH_COL[curveCh].name()));
        }
    }

    // handles. `bottom` places the grip at the lower edge so GHS window (top)
    // and GHS shape (bottom) handles don't collide when they share an x.
    auto drawHandle = [&](double v, const QColor& col, const QString& label, bool bottom) {
        const double px = valToX(v);
        g.setPen(QPen(col, 2.0));
        g.drawLine(QPointF(px, r.top() + 8), QPointF(px, r.bottom()));
        QRectF grip = bottom ? QRectF(px - 9, r.bottom() - 8, 18, 16)
                             : QRectF(px - 9, r.top() - 8, 18, 16);
        g.fillRect(grip, col);
        g.setPen(QColor("#06080b"));
        g.drawText(grip, Qt::AlignCenter, label);
    };

    if (ghs) {
        const GHSParams gp = m_model->ghs();
        const ChannelStretch wc = m_model->channel(0);
        const double span = std::max(1e-6, wc.white - wc.black);
        auto wv = [&](double p){ return wc.black + p * span; };   // windowed pos -> value
        drawHandle(wv(gp.LP), QColor("#7e8b98"), "LP", false);
        drawHandle(wv(gp.SP), GHS_COL, "SP", false);
        drawHandle(wv(gp.HP), QColor("#7e8b98"), "HP", false);
        // B/W (the window) are set in Linear mode; not shown here.
    } else if (m_model->fn() == StretchFn::Linear) {
        // RGB mode: each channel keeps its own B/M/W (auto-STF sets them apart).
        // Show all three as thin channel-coloured lines (M dashed), draggable
        // individually from the plot body; the labelled grips on top stay the
        // LINKED handles (drag all three together).
        if (m_active < 0 && ch >= 3) {
            for (int c = 0; c < 3; ++c) {
                const ChannelStretch cc = m_model->channel(c);
                QColor col = CH_COL[c]; col.setAlpha(170);
                QPen thin(col, 1.2);
                g.setPen(thin);
                g.drawLine(QPointF(valToX(cc.black), r.top() + 8), QPointF(valToX(cc.black), r.bottom()));
                g.drawLine(QPointF(valToX(cc.white), r.top() + 8), QPointF(valToX(cc.white), r.bottom()));
                thin.setStyle(Qt::DashLine);
                g.setPen(thin);
                g.drawLine(QPointF(valToX(cc.mid), r.top() + 8), QPointF(valToX(cc.mid), r.bottom()));
            }
        }
        const ChannelStretch cs = m_model->channel(curveCh);
        const QColor hc = (m_active < 0 || ch == 1) ? QColor("#cdd7e1") : CH_COL[curveCh];
        drawHandle(cs.black, hc, "B", false);
        drawHandle(cs.mid, QColor("#cdd7e1"), "M", false);
        drawHandle(cs.white, hc, "W", false);
    } else {   // Log / Asinh: window fixed by Linear; only the midtone here
        const ChannelStretch cs = m_model->channel(curveCh);
        drawHandle(cs.mid, QColor("#cdd7e1"), "M", false);
    }
}

void HistogramView::mousePressEvent(QMouseEvent* e) {
    const double px = e->position().x();
    const bool ghs = m_model->fn() == StretchFn::GHS;
    auto near = [&](double v) { return std::fabs(px - valToX(v)) < 10.0; };

    m_dragHandle.clear();
    m_dragChannel = -1;
    if (ghs) {
        const GHSParams gp = m_model->ghs();
        const ChannelStretch wc = m_model->channel(0);
        const double span = std::max(1e-6, wc.white - wc.black);
        auto wv = [&](double p){ return wc.black + p * span; };
        if (near(wv(gp.SP))) m_dragHandle = "SP";
        else if (near(wv(gp.LP))) m_dragHandle = "LP";
        else if (near(wv(gp.HP))) m_dragHandle = "HP";
    } else if (m_model->fn() == StretchFn::Linear) {
        const int nch = m_src ? m_src->channels() : 1;
        // RGB mode, click in the plot body: grab the nearest per-channel line.
        // Clicks on the top grip strip keep the linked-drag behaviour.
        if (m_active < 0 && nch >= 3 && e->position().y() > plotRect().top() + 12.0) {
            double best = 6.0; int bc = -1; QString bh;
            for (int c = 0; c < 3; ++c) {
                const ChannelStretch cc = m_model->channel(c);
                const struct { const char* h; double v; } cand[3] =
                    { {"b", cc.black}, {"m", cc.mid}, {"w", cc.white} };
                for (const auto& k : cand) {
                    const double d = std::fabs(px - valToX(k.v));
                    if (d < best) { best = d; bc = c; bh = QLatin1String(k.h); }
                }
            }
            if (bc >= 0) { m_dragChannel = bc; m_dragHandle = bh; }
        }
        if (m_dragHandle.isEmpty()) {
            const int c = (m_active < 0) ? 0 : m_active;
            const ChannelStretch cs = m_model->channel(c);
            if (near(cs.mid)) m_dragHandle = "m";
            else if (near(cs.black)) m_dragHandle = "b";
            else if (near(cs.white)) m_dragHandle = "w";
        }
    } else {   // Log / Asinh: only the midtone is adjustable here
        const int c = (m_active < 0) ? 0 : m_active;
        if (near(m_model->channel(c).mid)) m_dragHandle = "m";
    }
    if (!m_dragHandle.isEmpty()) applyDrag(xToVal(px));
}

void HistogramView::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragHandle.isEmpty()) return;
    // Dragging a handle past the plot's edge EXTENDS the axis in that
    // direction (the view follows the hand) — the natural way to take a
    // black point below the data minimum or a white above the maximum
    // without first pressing Wide. Growth is proportional to the overshoot
    // and bounded by the handle domain; the plot never collapses.
    const QRectF r = plotRect();
    const double px = e->position().x();
    double a, b; viewRange(a, b);
    const double span = std::max(1e-6, b - a);
    double v = a + (px - r.left()) / std::max(1.0, r.width()) * span;   // unclamped
    const double outerLo = kHandleMin - 0.5, outerHi = kHandleMax + 0.5;
    bool grew = false;
    if (v < a && a > outerLo) { m_manA = std::max(outerLo, v); m_manB = b; grew = true; }
    else if (v > b && b < outerHi) { m_manB = std::min(outerHi, v); m_manA = a; grew = true; }
    if (grew) {
        m_axis = AxisMode::Manual;
        recomputeHistogram();
        emit axisModeChanged();
    }
    applyDrag(xToVal(px));
}

void HistogramView::mouseReleaseEvent(QMouseEvent*) {
    m_dragHandle.clear();
    m_dragChannel = -1;
}

void HistogramView::applyDrag(double v) {
    const double eps = 0.006;
    if (m_model->fn() == StretchFn::GHS) {
        // SP/LP/HP are positions within the black/white window (set in Linear);
        // convert the dragged value into windowed [0,1].
        const ChannelStretch wc = m_model->channel(0);
        const double span = std::max(1e-6, wc.white - wc.black);
        double p = (v - wc.black) / span;
        // SP/LP/HP may leave the window (the curve stays monotone: the slope
        // function is positive everywhere — an SP below the window gives the
        // log-like, steepest-at-the-sky shape that a clipped-away mode needs).
        p = std::max(kHandleMin, std::min(kHandleMax, p));
        GHSParams g = m_model->ghs();
        if (m_dragHandle == "SP") g.SP = std::min(g.HP - eps, std::max(g.LP + eps, p));
        else if (m_dragHandle == "LP") g.LP = std::min(g.SP - eps, std::max(kHandleMin, p));
        else if (m_dragHandle == "HP") g.HP = std::max(g.SP + eps, std::min(kHandleMax, p));
        m_model->setGhs(g);
        return;
    }

    auto clampSet = [&](ChannelStretch cs) {
        if (m_dragHandle == "b") cs.black = std::min(cs.mid - eps, std::max(kHandleMin, v));
        else if (m_dragHandle == "m") cs.mid = std::min(cs.white - eps, std::max(cs.black + eps, v));
        else if (m_dragHandle == "w") cs.white = std::max(cs.mid + eps, std::min(kHandleMax, v));
        return cs;
    };

    if (m_active < 0 && m_dragChannel >= 0) {    // one channel's line, grabbed in the plot
        m_model->setChannel(m_dragChannel, clampSet(m_model->channel(m_dragChannel)));
    } else if (m_active < 0) {                   // RGB: move all channels together
        const int n = m_model->channelCount();
        const double cur = (m_dragHandle == "b") ? m_model->channel(0).black
                         : (m_dragHandle == "m") ? m_model->channel(0).mid
                                                 : m_model->channel(0).white;
        double delta = v - cur;
        // RIGID group: clamp the COMMON delta so the tightest channel just
        // reaches its bound, and never clamp channels individually — a
        // per-channel clamp would ratchet the channels' offsets apart on
        // every bounce (non-reversible drags that visibly re-balance colour;
        // an imported display function's tiny midtones made this bite on the
        // very first leftward wiggle). Note the white bound: imported far
        // whites may legitimately sit beyond the data maximum, so the group
        // may keep (not grow) a white above 1.
        for (int c = 0; c < n; ++c) {
            const ChannelStretch cs = m_model->channel(c);
            double lob, hib;
            if (m_dragHandle == "b")      { lob = kHandleMin;     hib = cs.mid - eps; }
            else if (m_dragHandle == "m") { lob = cs.black + eps; hib = cs.white - eps; }
            else                          { lob = cs.mid + eps;   hib = std::max(kHandleMax, cs.white); }
            const double base = (m_dragHandle == "b") ? cs.black
                              : (m_dragHandle == "m") ? cs.mid : cs.white;
            delta = std::max(delta, lob - base);
            delta = std::min(delta, hib - base);
        }
        for (int c = 0; c < n; ++c) {
            ChannelStretch cs = m_model->channel(c);
            if (m_dragHandle == "b") cs.black += delta;
            else if (m_dragHandle == "m") cs.mid += delta;
            else cs.white += delta;
            m_model->setChannel(c, cs);
        }
    } else {
        m_model->setChannel(m_active, clampSet(m_model->channel(m_active)));
    }
}

} // namespace astro

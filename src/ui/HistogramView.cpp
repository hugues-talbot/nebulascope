#include "ui/HistogramView.h"
#include "core/Stretch.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
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
    recomputeHistogram();
}

void HistogramView::setActiveChannel(int c) {
    m_active = (c < -1 || c > 2) ? -1 : c;
    update();
}

void HistogramView::recomputeHistogram() {
    m_hist.clear();
    if (!m_src) { update(); return; }
    const int ch = m_src->channels();
    const int bins = 256;
    const std::size_t n = m_src->samplesPerChannel();
    const std::size_t step = n > 400000 ? n / 400000 : 1;

    for (int c = 0; c < ch; ++c) {
        std::vector<float> hb(bins, 0.0f);
        const float* p = m_src->plane<float>(c);
        const double lo = m_model->lo(c), hi = m_model->hi(c);
        const double span = std::max(1e-9, hi - lo);
        for (std::size_t i = 0; i < n; i += step) {
            double x = (double(p[i]) - lo) / span;
            if (x < 0 || x > 1) continue;
            int bi = int(x * (bins - 1));
            hb[bi] += 1.0f;
        }
        float mx = 0.0f;
        for (float v : hb) { v = std::log1p(v); if (v > mx) mx = v; }     // log-scale frequency
        for (auto& v : hb) v = mx > 0 ? std::log1p(v) / mx : 0.0f;
        m_hist.push_back(std::move(hb));
    }
    update();
}

QRectF HistogramView::plotRect() const {
    return QRectF(rect()).adjusted(10, 16, -10, -14);   // headroom for handle grips
}

double HistogramView::valToX(double v) const {
    const QRectF r = plotRect();
    return r.left() + v * r.width();
}
double HistogramView::xToVal(double px) const {
    const QRectF r = plotRect();
    double v = (px - r.left()) / std::max(1.0, r.width());
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

void HistogramView::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = plotRect();

    g.fillRect(rect(), QColor("#0b1016"));
    g.fillRect(r, QColor("#070b10"));

    // GHS protection bands
    const bool ghs = m_model->fn() == StretchFn::GHS;
    if (ghs) {
        const GHSParams gp = m_model->ghs();
        g.fillRect(QRectF(r.left(), r.top(), gp.LP * r.width(), r.height()), QColor(91, 104, 118, 32));
        g.fillRect(QRectF(valToX(gp.HP), r.top(), r.right() - valToX(gp.HP), r.height()), QColor(91, 104, 118, 32));
    }

    // grid
    g.setPen(QColor("#121b24"));
    for (double gx = 0.25; gx < 1.0; gx += 0.25) g.drawLine(QPointF(valToX(gx), r.top()), QPointF(valToX(gx), r.bottom()));

    // histogram areas
    const int ch = int(m_hist.size());
    for (int c = 0; c < ch; ++c) {
        const auto& hb = m_hist[c];
        QPainterPath path;
        path.moveTo(r.left(), r.bottom());
        for (int i = 0; i < int(hb.size()); ++i) {
            const double x = r.left() + (double(i) / (hb.size() - 1)) * r.width();
            const double y = r.bottom() - hb[i] * (r.height() - 4);
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

    // transfer curve
    const int N = 512;
    const int curveCh = (m_active < 0 || m_active >= ch) ? 0 : m_active;
    std::vector<float> lut = buildLut(m_model->fn(), m_model->channel(curveCh), m_model->ghs(), N);
    QPainterPath curve;
    for (int i = 0; i < N; ++i) {
        const double x = r.left() + (double(i) / (N - 1)) * r.width();
        const double y = r.bottom() - lut[i] * (r.height() - 4);
        if (i == 0) curve.moveTo(x, y); else curve.lineTo(x, y);
    }
    g.setPen(QPen(ghs ? GHS_COL : QColor("#eef3f8"), 1.8));
    g.drawPath(curve);

    // handles
    auto drawHandle = [&](double v, const QColor& col, const QString& label) {
        const double px = valToX(v);
        g.setPen(QPen(col, 2.0));
        g.drawLine(QPointF(px, r.top() + 8), QPointF(px, r.bottom()));
        QRectF grip(px - 9, r.top() - 8, 18, 16);
        g.fillRect(grip, col);
        g.setPen(QColor("#06080b"));
        g.drawText(grip, Qt::AlignCenter, label);
    };

    if (ghs) {
        const GHSParams gp = m_model->ghs();
        drawHandle(gp.LP, QColor("#7e8b98"), "LP");
        drawHandle(gp.SP, GHS_COL, "SP");
        drawHandle(gp.HP, QColor("#7e8b98"), "HP");
    } else {
        const ChannelStretch cs = m_model->channel(curveCh);
        const QColor hc = (m_active < 0 || ch == 1) ? QColor("#cdd7e1") : CH_COL[curveCh];
        drawHandle(cs.black, hc, "B");
        drawHandle(cs.mid, QColor("#cdd7e1"), "M");
        drawHandle(cs.white, hc, "W");
    }
}

void HistogramView::mousePressEvent(QMouseEvent* e) {
    const double px = e->position().x();
    const bool ghs = m_model->fn() == StretchFn::GHS;
    auto near = [&](double v) { return std::fabs(px - valToX(v)) < 10.0; };

    m_dragHandle.clear();
    if (ghs) {
        const GHSParams gp = m_model->ghs();
        if (near(gp.SP)) m_dragHandle = "SP";
        else if (near(gp.LP)) m_dragHandle = "LP";
        else if (near(gp.HP)) m_dragHandle = "HP";
    } else {
        const int c = (m_active < 0) ? 0 : m_active;
        const ChannelStretch cs = m_model->channel(c);
        if (near(cs.mid)) m_dragHandle = "m";
        else if (near(cs.black)) m_dragHandle = "b";
        else if (near(cs.white)) m_dragHandle = "w";
    }
    if (!m_dragHandle.isEmpty()) applyDrag(xToVal(px));
}

void HistogramView::mouseMoveEvent(QMouseEvent* e) {
    if (!m_dragHandle.isEmpty()) applyDrag(xToVal(e->position().x()));
}

void HistogramView::mouseReleaseEvent(QMouseEvent*) {
    m_dragHandle.clear();
}

void HistogramView::applyDrag(double v) {
    const double eps = 0.006;
    if (m_model->fn() == StretchFn::GHS) {
        GHSParams g = m_model->ghs();
        if (m_dragHandle == "SP") g.SP = std::min(g.HP - eps, std::max(g.LP + eps, v));
        else if (m_dragHandle == "LP") g.LP = std::min(g.SP - eps, std::max(0.0, v));
        else if (m_dragHandle == "HP") g.HP = std::max(g.SP + eps, std::min(1.0, v));
        m_model->setGhs(g);
        return;
    }

    auto clampSet = [&](ChannelStretch cs) {
        if (m_dragHandle == "b") cs.black = std::min(cs.mid - eps, std::max(0.0, v));
        else if (m_dragHandle == "m") cs.mid = std::min(cs.white - eps, std::max(cs.black + eps, v));
        else if (m_dragHandle == "w") cs.white = std::max(cs.mid + eps, std::min(1.0, v));
        return cs;
    };

    if (m_active < 0) {                          // RGB: move all channels together
        const int n = m_model->channelCount();
        const double cur = (m_dragHandle == "b") ? m_model->channel(0).black
                         : (m_dragHandle == "m") ? m_model->channel(0).mid
                                                 : m_model->channel(0).white;
        const double delta = v - cur;
        for (int c = 0; c < n; ++c) {
            ChannelStretch cs = m_model->channel(c);
            const double base = (m_dragHandle == "b") ? cs.black : (m_dragHandle == "m") ? cs.mid : cs.white;
            const double nv = base + delta;
            if (m_dragHandle == "b") cs.black = std::min(cs.mid - eps, std::max(0.0, nv));
            else if (m_dragHandle == "m") cs.mid = std::min(cs.white - eps, std::max(cs.black + eps, nv));
            else cs.white = std::max(cs.mid + eps, std::min(1.0, nv));
            m_model->setChannel(c, cs);
        }
    } else {
        m_model->setChannel(m_active, clampSet(m_model->channel(m_active)));
    }
}

} // namespace astro

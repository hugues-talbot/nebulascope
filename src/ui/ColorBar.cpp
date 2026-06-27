#include "ui/ColorBar.h"
#include "core/Stretch.h"
#include "core/Colormap.h"
#include <QPainter>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

namespace astro {

ColorBar::ColorBar(StretchModel* model, QWidget* parent)
    : QWidget(parent), m_model(model) {
    setMinimumHeight(46);
    connect(m_model, &StretchModel::changed, this, [this]{ update(); });
}

static QString fmtVal(double v) {
    const double a = std::fabs(v);
    if (a != 0 && (a < 1e-3 || a >= 1e5)) return QString::number(v, 'e', 1);
    return QString::number(v, 'g', 4);
}

void ColorBar::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing, false);

    const QRectF full = rect();
    const int barH = 16;
    const QRectF bar(full.left() + 2, full.top() + 2, full.width() - 4, barH);

    const StretchFn fn = m_model->fn();
    const bool mono = m_model->channelCount() == 1;
    const ChannelStretch cs = m_model->channel(0);

    const int N = 512;
    const std::vector<float> lut = buildLut(fn, cs, m_model->ghs(), N);
    const bool useCmap = mono && m_model->colormap() != Colormap::Gray;
    const std::vector<std::uint8_t> cmap =
        useCmap ? buildColormapLut(m_model->colormap(), 256, m_model->splitThreshold())
                : std::vector<std::uint8_t>();

    // Gradient: x across the bar = input fraction over [lo,hi]; colour = the
    // stretched value, optionally through the colormap.
    const int w = int(bar.width());
    for (int px = 0; px < w; ++px) {
        const double x = (w <= 1) ? 0.0 : double(px) / (w - 1);
        const float y = lut[int(x * (N - 1))];
        int rr, gg, bb;
        if (useCmap) {
            const int idx = std::clamp(int(y * 255.0f + 0.5f), 0, 255);
            rr = cmap[idx*3+0]; gg = cmap[idx*3+1]; bb = cmap[idx*3+2];
        } else {
            rr = gg = bb = std::clamp(int(y * 255.0f + 0.5f), 0, 255);
        }
        g.setPen(QColor(rr, gg, bb));
        g.drawLine(QPointF(bar.left() + px, bar.top()), QPointF(bar.left() + px, bar.bottom()));
    }
    g.setPen(QColor("#2a3744"));
    g.drawRect(bar);

    // Value ticks in raw data units, from channel 0's display range.
    const double lo = m_model->lo(0), hi = m_model->hi(0);
    g.setPen(QColor("#8492a0"));
    QFont f = g.font(); f.setPointSizeF(8.0); g.setFont(f);
    const QFontMetrics fm(f);
    const double ticks[5] = { 0.0, 0.25, 0.5, 0.75, 1.0 };
    for (double t : ticks) {
        const double px = bar.left() + t * bar.width();
        g.setPen(QColor("#3a4654"));
        g.drawLine(QPointF(px, bar.bottom()), QPointF(px, bar.bottom() + 3));
        const QString label = fmtVal(lo + t * (hi - lo));
        int tx = int(px) - fm.horizontalAdvance(label) / 2;
        tx = std::max(int(full.left()), std::min(tx, int(full.right()) - fm.horizontalAdvance(label)));
        g.setPen(QColor("#8492a0"));
        g.drawText(tx, int(bar.bottom()) + 4 + fm.ascent(), label);
    }
}

} // namespace astro

#pragma once
//
// HistogramView — the interactive plot. Draws the per-channel histogram, the
// active transfer curve, and draggable handles that write straight back into
// the shared StretchModel:
//   * Linear / Log / Asinh : Black / Mid / White handles (per active channel)
//   * GHS                  : SP / LP / HP handles (D & b come from the panel)
//
#include <QWidget>
#include <vector>
#include "core/ImageData.h"
#include "render/StretchModel.h"

namespace astro {

class HistogramView : public QWidget {
    Q_OBJECT
public:
    explicit HistogramView(StretchModel* model, QWidget* parent = nullptr);

    void setSource(const ImageData* img);
    void setActiveChannel(int c);          // -1 = all/RGB, 0/1/2 = R/G/B
    int  activeChannel() const { return m_active; }
    void setLogScale(bool on) { m_logHist = on; update(); }
    bool logScale() const { return m_logHist; }

public slots:
    void recomputeHistogram();             // rebuild bins from current ranges

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    QSize sizeHint() const override { return QSize(380, 300); }

private:
    QRectF plotRect() const;
    double xToVal(double px) const;
    double valToX(double v) const;
    // Plot x-axis span in normalized [0,1]-over-[lo,hi] coords: full [0,1] for
    // Linear, the black/white window for Log/Asinh/GHS (so their controls fill
    // the widget). Linear is therefore the coarse windowing tool.
    void   viewRange(double& a, double& b) const;
    void   applyDrag(double v);

    StretchModel* m_model;
    const ImageData* m_src = nullptr;
    std::vector<std::vector<float>> m_hist;   // per channel, RAW counts per bin

    int m_active = -1;
    bool m_logHist = true;                    // log vs linear frequency axis
    QString m_dragHandle;                     // "", b/m/w, SP/LP/HP
};

} // namespace astro

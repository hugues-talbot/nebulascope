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

    // Abscissa range. Auto = the classic fit (full data range in Linear, the
    // black/white window in Log/Asinh/GHS). Wide = the same, extended by half
    // a span on each side, so handles can go BEYOND the data (black below the
    // minimum lifts a floor; white above the maximum gives headroom; GHS SP on
    // a mode that the black point has clipped away). Manual = wheel-zoomed.
    enum class AxisMode { Auto, Wide, Manual };
    AxisMode axisMode() const { return m_axis; }
    void setWideAxis(bool on);
    void resetAxis();                       // back to Auto
    // Snap the GHS symmetry point to the histogram peak (the mode) of the
    // curve channel — the GHS tutorial's first move, as one gesture.
    void snapSpToMode();

signals:
    void axisModeChanged();

public slots:
    void recomputeHistogram();             // rebuild bins from current ranges

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
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
    double modeU(int c) const;             // histogram peak of channel c, in view coords (NaN if none)

    // Handle domain in normalized [lo,hi] units: one full span beyond the
    // data on each side. The renderer windows by an affine map and clamps,
    // so out-of-range handles are already well-defined there.
    static constexpr double kHandleMin = -1.0;
    static constexpr double kHandleMax =  2.0;
    AxisMode m_axis = AxisMode::Auto;
    double m_manA = 0.0, m_manB = 1.0;

    StretchModel* m_model;
    const ImageData* m_src = nullptr;
    std::vector<std::vector<float>> m_hist;   // per channel, RAW counts per bin

    int m_active = -1;
    bool m_logHist = true;                    // log vs linear frequency axis
    QString m_dragHandle;                     // "", b/m/w, SP/LP/HP
    int m_dragChannel = -1;                   // Linear RGB: drag one channel's line (-1 = linked)

    // Rebin cache: recomputeHistogram() fires on every model change (each drag
    // tick), but the bins only depend on these inputs — skip when unchanged.
    const ImageData* m_binSrc = nullptr;
    double m_binLo[3] = {0,0,0}, m_binHi[3] = {0,0,0};
    double m_binA = -1, m_binB = -1;          // view window used for binning
};

} // namespace astro

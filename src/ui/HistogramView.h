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
    void   applyDrag(double v);

    StretchModel* m_model;
    const ImageData* m_src = nullptr;
    std::vector<std::vector<float>> m_hist;   // per channel, normalized log bins

    int m_active = -1;
    QString m_dragHandle;                     // "", b/m/w, SP/LP/HP
};

} // namespace astro

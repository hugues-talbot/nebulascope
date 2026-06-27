#pragma once
//
// ColorBar — a horizontal legend under the histogram. It shows the actual
// value→display mapping: position spans the channel's display range [lo,hi],
// the colour is the stretched value run through the active colormap (mono) or a
// neutral grey ramp (RGB), and ticks label raw data values.
//
#include <QWidget>
#include "render/StretchModel.h"

namespace astro {

class ColorBar : public QWidget {
    Q_OBJECT
public:
    explicit ColorBar(StretchModel* model, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
    QSize sizeHint() const override { return QSize(380, 52); }

private:
    StretchModel* m_model;
};

} // namespace astro

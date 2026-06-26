#pragma once
//
// HistogramPanel — the right dock contents: stretch-function tabs, channel
// chips, the interactive HistogramView, GHS D/b sliders, and Auto/Reset.
// All controls drive the shared StretchModel.
//
#include <QWidget>
#include "core/ImageData.h"
#include "render/StretchModel.h"

class QButtonGroup;
class QSlider;
class QWidget;

namespace astro {

class HistogramView;

class HistogramPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistogramPanel(StretchModel* model, QWidget* parent = nullptr);
    void setSource(const ImageData* img);

private slots:
    void syncFromModel();

private:
    StretchModel* m_model;
    const ImageData* m_src = nullptr;
    HistogramView* m_view = nullptr;
    QButtonGroup* m_fnGroup = nullptr;
    QButtonGroup* m_chanGroup = nullptr;
    QWidget* m_ghsBox = nullptr;
    QSlider* m_dSlider = nullptr;
    QSlider* m_bSlider = nullptr;
};

} // namespace astro

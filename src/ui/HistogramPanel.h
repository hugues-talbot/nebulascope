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
class QLineEdit;
class QLabel;

namespace astro {

class HistogramView;

class HistogramPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistogramPanel(StretchModel* model, QWidget* parent = nullptr);
    void setSource(const ImageData* img);

private slots:
    void syncFromModel();
    void onParamEdited(int idx);
    void onRgbEdited(int c, int idx);   // 3×3 grid: channel c, 0=B 1=M 2=W
    void onAdjChanged();                // any adjustment slider moved

private:
    int  editChannel() const;

    StretchModel* m_model;
    const ImageData* m_src = nullptr;
    HistogramView* m_view = nullptr;
    QButtonGroup* m_fnGroup = nullptr;
    QButtonGroup* m_chanGroup = nullptr;
    QWidget* m_ghsBox = nullptr;
    QSlider* m_dSlider = nullptr;
    QSlider* m_bSlider = nullptr;

    // Editable numeric fields for the active mode's parameters (precise entry).
    static constexpr int kParamFields = 5;
    QWidget*   m_pRow[kParamFields] = {};
    QLabel*    m_pLbl[kParamFields] = {};
    QLineEdit* m_pEdit[kParamFields] = {};
    // RGB images: per-channel 3×3 grid (R/G/B × Black/Mid/White) instead of the
    // single linked row above.
    QWidget*   m_rgbBox = nullptr;
    QLineEdit* m_rgbEdit[3][3] = {};
    QLabel*    m_rgbColLbl[3] = {};
    // Post-stretch display adjustments (always visible, any stretch mode).
    // Order: bright, contrast, gamma, shadows, highlights, blackpt, whitept,
    // temp, tint, hue, saturation, vibrance. Indices >= kAdjColorFrom are the
    // cross-channel colour ops (disabled for mono sources).
    static constexpr int kAdjSliders = 12;
    static constexpr int kAdjColorFrom = 7;
    QSlider* m_adjSlider[kAdjSliders] = {};
};

} // namespace astro

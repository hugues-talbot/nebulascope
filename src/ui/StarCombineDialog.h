#pragma once
//
// StarCombineDialog — recompose a starless image with a stars-only image via
// screen blending:  out = 1 − (1 − starless) · (1 − k·stars), per channel.
// Screening is additive-like in the dark and saturation-safe in the bright —
// the standard star-recomposition op after StarNet/StarXTerminator separation.
//
// Both inputs are RGB (or both mono). The blend runs on display-ready [0,1]
// values: each image is passed through its CURRENT view stretch ("as
// displayed"), matching how the user prepared the two renditions.
//
#include <QDialog>
#include <QString>
#include <functional>
#include <memory>
#include <vector>
#include "core/ImageData.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSlider;

namespace astro {

class StarCombineDialog : public QDialog {
    Q_OBJECT
public:
    struct Source {
        QString name;
        std::shared_ptr<ImageData> img;                 // decoded pixels
        std::function<void(ImageData&)> toDisplay;      // in-place: raw -> display [0,1]
    };

    StarCombineDialog(std::vector<Source> sources, QWidget* parent = nullptr);

    bool             hasResult() const { return m_result.isValid(); }
    const ImageData& result()    const { return m_result; }
    QString          resultName() const;

private:
    void updatePreview();
    void accept() override;                             // builds m_result
    ImageData blend(bool preview, QString& err);

    std::vector<Source> m_sources;
    QComboBox*      m_starlessCombo = nullptr;
    QComboBox*      m_starsCombo    = nullptr;
    QSlider*        m_amount        = nullptr;          // 0..150 -> k = /100
    QLabel*         m_amountLbl     = nullptr;
    QLineEdit*      m_nameEdit      = nullptr;
    QLabel*         m_preview       = nullptr;
    QLabel*         m_status        = nullptr;
    ImageData m_result;

    // Reopen with the last-used pairing/amount.
    static bool    s_hasMemory;
    static QString s_starless, s_stars;
    static int     s_amount;
};

} // namespace astro

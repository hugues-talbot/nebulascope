#pragma once
//
// CombineDialog — the "Combine Channels" pixel-math UI. Assign each mono frame a
// role (R/G/B/S/H/O/L), pick a palette preset (which fills an editable 3×N
// weight matrix), choose pre-normalization + luminance handling, watch a live
// preview, and produce a new linear RGB ImageData.
//
#include <QDialog>
#include <memory>
#include <vector>
#include "core/ImageData.h"
#include "core/ChannelCombine.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

namespace astro {

class CombineDialog : public QDialog {
    Q_OBJECT
public:
    struct Source { QString name; std::shared_ptr<ImageData> img; };

    CombineDialog(std::vector<Source> monoSources, QWidget* parent = nullptr);

    bool             hasResult() const { return m_result.isValid(); }
    const ImageData& result()    const { return m_result; }
    QString          resultName() const;

private:
    struct Row {
        QCheckBox*      enable = nullptr;
        QComboBox*      role   = nullptr;
        QDoubleSpinBox* wR = nullptr;
        QDoubleSpinBox* wG = nullptr;
        QDoubleSpinBox* wB = nullptr;
        int w = 0, h = 0;
    };

    void applyPreset(int presetIndex);
    void updatePreview();
    void accept() override;                       // builds m_result
    bool gatherPlanes(bool preview,
                      std::vector<CombinePlane>& planes,
                      const float*& lum,
                      std::vector<std::vector<float>>& scratch,
                      int& w, int& h, QString& err);
    PreNorm  preNorm() const;
    LumMode  lumMode() const;

    std::vector<Source> m_sources;
    std::vector<Row>    m_rows;
    QComboBox*  m_preNormCombo = nullptr;
    QComboBox*  m_domainCombo  = nullptr;   // Linear / Stretched
    QComboBox*  m_lumCombo     = nullptr;
    QDoubleSpinBox* m_lumAmount = nullptr;
    QLineEdit*  m_nameEdit = nullptr;
    QLabel*     m_preview  = nullptr;
    QLabel*     m_status   = nullptr;

    ImageData m_result;
};

} // namespace astro

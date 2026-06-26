#include "ui/HistogramPanel.h"
#include "ui/HistogramView.h"
#include "core/ImageStats.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QButtonGroup>
#include <QSlider>
#include <QLabel>

namespace astro {

static QPushButton* tab(const QString& text) {
    auto* b = new QPushButton(text);
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

HistogramPanel::HistogramPanel(StretchModel* model, QWidget* parent)
    : QWidget(parent), m_model(model) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    // --- stretch-function tabs ---
    auto* fnRow = new QHBoxLayout();
    fnRow->setSpacing(4);
    m_fnGroup = new QButtonGroup(this);
    const char* fns[] = { "Linear", "Log", "Asinh", "GHS" };
    for (int i = 0; i < 4; ++i) { auto* b = tab(fns[i]); m_fnGroup->addButton(b, i); fnRow->addWidget(b); }
    root->addLayout(fnRow);

    // --- channel chips ---
    auto* chRow = new QHBoxLayout();
    chRow->setSpacing(4);
    m_chanGroup = new QButtonGroup(this);
    const char* chips[] = { "RGB", "R", "G", "B" };
    for (int i = 0; i < 4; ++i) { auto* b = tab(chips[i]); m_chanGroup->addButton(b, i - 1); chRow->addWidget(b); }
    chRow->addStretch();
    root->addLayout(chRow);

    // --- the plot ---
    m_view = new HistogramView(m_model);
    root->addWidget(m_view, 1);

    // --- GHS sliders (shown only in GHS mode) ---
    m_ghsBox = new QWidget();
    auto* gl = new QVBoxLayout(m_ghsBox);
    gl->setContentsMargins(0, 0, 0, 0);
    gl->setSpacing(6);
    auto addSlider = [&](const QString& name, int lo, int hi, int val) {
        auto* row = new QHBoxLayout();
        auto* lab = new QLabel(name);
        lab->setMinimumWidth(118);
        auto* s = new QSlider(Qt::Horizontal);
        s->setRange(lo, hi);
        s->setValue(val);
        row->addWidget(lab);
        row->addWidget(s, 1);
        gl->addLayout(row);
        return s;
    };
    m_dSlider = addSlider("D · strength", 0, 800, 160);     // /100
    m_bSlider = addSlider("b · focus", -500, 1500, 600);    // /100
    root->addWidget(m_ghsBox);

    // --- Auto / Reset ---
    auto* btnRow = new QHBoxLayout();
    auto* autoBtn = new QPushButton("Auto STF");
    auto* resetBtn = new QPushButton("Reset");
    btnRow->addWidget(autoBtn);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    // --- wiring ---
    connect(m_fnGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_model->setFn(static_cast<StretchFn>(id));
    });
    connect(m_chanGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_view->setActiveChannel(id);   // -1 = RGB
    });
    connect(m_dSlider, &QSlider::valueChanged, this, [this](int v) {
        GHSParams g = m_model->ghs(); g.D = v / 100.0; m_model->setGhs(g);
    });
    connect(m_bSlider, &QSlider::valueChanged, this, [this](int v) {
        GHSParams g = m_model->ghs(); g.b = v / 100.0; m_model->setGhs(g);
    });
    connect(autoBtn, &QPushButton::clicked, this, [this] {
        if (m_src) m_model->autoStretch(computeStats(*m_src));
    });
    connect(resetBtn, &QPushButton::clicked, this, [this] { m_model->reset(); });
    connect(m_model, &StretchModel::changed, this, &HistogramPanel::syncFromModel);

    if (auto* b = m_chanGroup->button(-1)) b->setChecked(true);
    syncFromModel();
}

void HistogramPanel::setSource(const ImageData* img) {
    m_src = img;
    m_view->setSource(img);
    syncFromModel();
}

void HistogramPanel::syncFromModel() {
    if (auto* b = m_fnGroup->button(static_cast<int>(m_model->fn()))) b->setChecked(true);
    const bool ghs = m_model->fn() == StretchFn::GHS;
    m_ghsBox->setVisible(ghs);
    m_chanGroup->button(0) && m_chanGroup->button(0);   // no-op guard
    // keep sliders in sync without re-emitting
    QSignalBlocker bd(m_dSlider), bb(m_bSlider);
    m_dSlider->setValue(int(m_model->ghs().D * 100));
    m_bSlider->setValue(int(m_model->ghs().b * 100));
    m_view->recomputeHistogram();
}

} // namespace astro

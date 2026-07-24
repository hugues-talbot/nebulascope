#include "ui/HistogramPanel.h"
#include "ui/HistogramView.h"
#include "ui/ColorBar.h"
#include "core/ImageStats.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QButtonGroup>
#include <QSlider>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleValidator>
#include <QLocale>
#include <QEvent>
#include <algorithm>
#include <cmath>

namespace astro {

static QPushButton* tab(const QString& text) {
    auto* b = new QPushButton(text);
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

// Wheel adjusts a slider ONLY once it has been clicked (has focus) — hovering
// a slider while wheel-zooming the panel must not silently edit the display.
class WheelWhenFocused : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Wheel) {
            auto* w = qobject_cast<QWidget*>(o);
            if (w && !w->hasFocus()) { e->ignore(); return true; }
        }
        return QObject::eventFilter(o, e);
    }
};

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
    auto* logBtn = new QPushButton("Log");
    logBtn->setCheckable(true);
    logBtn->setChecked(true);
    logBtn->setCursor(Qt::PointingHandCursor);
    logBtn->setToolTip("Logarithmic vs linear histogram frequency axis");
    chRow->addWidget(logBtn);
    root->addLayout(chRow);

    // --- the plot ---
    m_view = new HistogramView(m_model);
    root->addWidget(m_view, 1);

    // --- editable parameter fields (precise entry; relabelled per mode) ---
    auto* pGrid = new QHBoxLayout();
    pGrid->setSpacing(8);
    for (int i = 0; i < kParamFields; ++i) {
        m_pRow[i] = new QWidget();
        auto* rl = new QVBoxLayout(m_pRow[i]);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(2);
        m_pLbl[i] = new QLabel("");
        m_pLbl[i]->setStyleSheet("color:#7e8b98; font-size:10px;");
        m_pEdit[i] = new QLineEdit();
        auto* dv = new QDoubleValidator(this);
        dv->setLocale(QLocale::c());                 // '.' decimal, locale-independent
        m_pEdit[i]->setValidator(dv);
        m_pEdit[i]->setLocale(QLocale::c());
        m_pEdit[i]->setMaximumWidth(90);
        rl->addWidget(m_pLbl[i]);
        rl->addWidget(m_pEdit[i]);
        pGrid->addWidget(m_pRow[i]);
        connect(m_pEdit[i], &QLineEdit::editingFinished, this, [this, i] { onParamEdited(i); });
    }
    pGrid->addStretch();
    root->addLayout(pGrid);

    // --- RGB images: 3×3 per-channel grid (rows R/G/B × cols Black/Mid/White) ---
    m_rgbBox = new QWidget();
    {
        auto* gl2 = new QGridLayout(m_rgbBox);
        gl2->setContentsMargins(0, 0, 0, 0);
        gl2->setHorizontalSpacing(8);
        gl2->setVerticalSpacing(3);
        const char* cols[3] = { "Black", "Mid", "White" };
        const char* rows[3] = { "R", "G", "B" };
        const char* rowCol[3] = { "#ff6b6b", "#3fd07f", "#5aa9ff" };
        for (int j = 0; j < 3; ++j) {
            m_rgbColLbl[j] = new QLabel(cols[j]);
            m_rgbColLbl[j]->setStyleSheet("color:#7e8b98; font-size:10px;");
            gl2->addWidget(m_rgbColLbl[j], 0, j + 1);
        }
        for (int c = 0; c < 3; ++c) {
            auto* rl2 = new QLabel(rows[c]);
            rl2->setStyleSheet(QStringLiteral("color:%1; font-size:11px; font-weight:600;").arg(rowCol[c]));
            gl2->addWidget(rl2, c + 1, 0);
            for (int j = 0; j < 3; ++j) {
                auto* e = new QLineEdit();
                auto* dv2 = new QDoubleValidator(this);
                dv2->setLocale(QLocale::c());
                e->setValidator(dv2);
                e->setLocale(QLocale::c());
                e->setMaximumWidth(90);
                gl2->addWidget(e, c + 1, j + 1);
                m_rgbEdit[c][j] = e;
                connect(e, &QLineEdit::editingFinished, this, [this, c, j] { onRgbEdited(c, j); });
            }
        }
        gl2->setColumnStretch(4, 1);
    }
    root->addWidget(m_rgbBox);

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

    // --- post-stretch display adjustments (always visible, any mode) ---
    {
        auto* hdr = new QHBoxLayout();
        auto* al = new QLabel("ADJUST");
        al->setStyleSheet("color:#5b6876; font-size:10px; letter-spacing:1.5px; font-weight:600;");
        auto* adjReset = new QPushButton("Reset");
        adjReset->setCursor(Qt::PointingHandCursor);
        adjReset->setStyleSheet("font-size:10px; padding:1px 8px;");
        adjReset->setToolTip("Reset adjustments only (stretch untouched)");
        hdr->addWidget(al);
        hdr->addStretch();
        hdr->addWidget(adjReset);
        root->addLayout(hdr);

        auto* ag = new QGridLayout();
        ag->setHorizontalSpacing(10);
        ag->setVerticalSpacing(2);
        struct Def { const char* name; int lo, hi; const char* tip; };
        const Def defs[kAdjSliders] = {
            { "Bright",   -100, 100, "Brightness" },
            { "Contrast", -100, 100, "Contrast (pivot at mid-gray)" },
            { "Gamma",    -100, 100, "Gamma 0.33–3 (log scale)" },
            { "Shadows",  -100, 100, "Lift / crush dark tones (black point pinned)" },
            { "Highlights",-100, 100, "Boost / recover bright tones (white point pinned)" },
            { "Black pt",    0, 100, "Clip-in from black (display space)" },
            { "White pt",    0, 100, "Clip-in from white (display space)" },
            { "Temp",     -100, 100, "Colour temperature: blue ↔ amber" },
            { "Tint",     -100, 100, "Tint: green ↔ magenta" },
            { "Hue",      -180, 180, "Hue rotation (degrees)" },
            { "Saturation",-100, 100, "Saturation about luminance" },
            { "Vibrance", -100, 100, "Saturation weighted to muted pixels (protects stars)" },
        };
        // Grid placement pairs complementary controls across the two columns:
        //   Bright|Contrast, Highlights|Shadows, White pt|Black pt,
        //   Gamma|Temp, Tint|Hue, Saturation|Vibrance.
        // (defs[] order is the model/index order — do not reorder it.)
        const int place[kAdjSliders][2] = {   // slider index -> {row, column}
            /*0 Bright*/     {0, 0}, /*1 Contrast*/ {0, 1},
            /*2 Gamma*/      {3, 0}, /*3 Shadows*/  {1, 1},
            /*4 Highlights*/ {1, 0}, /*5 Black pt*/ {2, 1},
            /*6 White pt*/   {2, 0}, /*7 Temp*/     {3, 1},
            /*8 Tint*/       {4, 0}, /*9 Hue*/      {4, 1},
            /*10 Saturation*/{5, 0}, /*11 Vibrance*/{5, 1},
        };
        auto* wheelGuard = new WheelWhenFocused(this);
        for (int i = 0; i < kAdjSliders; ++i) {
            auto* lb = new QLabel(defs[i].name);
            lb->setStyleSheet("color:#7e8b98; font-size:10px;");
            auto* s = new QSlider(Qt::Horizontal);
            s->setRange(defs[i].lo, defs[i].hi);
            s->setValue(0);
            s->setToolTip(defs[i].tip);
            s->setFocusPolicy(Qt::ClickFocus);        // click first, then wheel works
            s->setSingleStep(1);
            s->setPageStep(5);
            s->installEventFilter(wheelGuard);
            m_adjSlider[i] = s;
            ag->addWidget(lb, place[i][0], place[i][1] * 2);
            ag->addWidget(s,  place[i][0], place[i][1] * 2 + 1);
            connect(s, &QSlider::valueChanged, this, &HistogramPanel::onAdjChanged);
        }
        ag->setColumnStretch(1, 1);
        ag->setColumnStretch(3, 1);
        root->addLayout(ag);
        connect(adjReset, &QPushButton::clicked, this, [this] { m_model->setAdjust(AdjustParams{}); });
        // Same click-then-wheel behaviour for the GHS sliders.
        for (QSlider* s : { m_dSlider, m_bSlider }) {
            s->setFocusPolicy(Qt::ClickFocus);
            s->installEventFilter(wheelGuard);
        }
    }

    // --- Auto / Reset ---
    auto* btnRow = new QHBoxLayout();
    auto* autoBtn = new QPushButton("Auto STF");
    autoBtn->setToolTip("Per-channel auto stretch — equalises the channels (neutralises colour cast)");
    auto* autoLinkedBtn = new QPushButton("Auto Linked");
    autoLinkedBtn->setToolTip("One shared auto stretch for all channels — preserves colour balance");
    auto* resetBtn = new QPushButton("Reset");
    btnRow->addWidget(autoBtn);
    btnRow->addWidget(autoLinkedBtn);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    // --- colorbar legend (value -> display, reflects stretch + colormap) ---
    auto* cbLabel = new QLabel("COLORBAR");
    cbLabel->setStyleSheet("color:#5b6876; font-size:10px; letter-spacing:1.5px; font-weight:600;");
    root->addWidget(cbLabel);
    root->addWidget(new ColorBar(m_model));

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
    connect(autoLinkedBtn, &QPushButton::clicked, this, [this] {
        if (m_src) m_model->autoStretchLinked(computeStats(*m_src));
    });
    connect(resetBtn, &QPushButton::clicked, this, [this] { m_model->reset(); });
    connect(logBtn, &QPushButton::toggled, this, [this](bool on) { m_view->setLogScale(on); });
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
    // Adjustment sliders follow the model (per-image restore, reset, paste).
    // Colour ops need RGB; keep those sliders disabled for mono sources.
    {
        const AdjustParams& a = m_model->adjust();
        const int av[kAdjSliders] = {
            int(std::lround(a.brightness * 100)),
            int(std::lround(a.contrast * 100)),
            int(std::lround(std::log(std::max(0.05, a.gamma)) / std::log(3.0) * 100)),
            int(std::lround(a.shadows * 100)),
            int(std::lround(a.highlights * 100)),
            int(std::lround(a.blackpoint * 200)),
            int(std::lround((1.0 - a.whitepoint) * 200)),
            int(std::lround(a.temperature * 100)),
            int(std::lround(a.tint * 100)),
            int(std::lround(a.hue)),
            int(std::lround(a.saturation * 100)),
            int(std::lround(a.vibrance * 100)) };
        const bool rgbSrc = m_src && m_src->channels() >= 3;
        for (int i = 0; i < kAdjSliders; ++i) {
            QSignalBlocker bl(m_adjSlider[i]);
            m_adjSlider[i]->setValue(av[i]);
            if (i >= kAdjColorFrom) m_adjSlider[i]->setEnabled(rgbSrc);
        }
    }
    m_view->recomputeHistogram();

    // --- editable parameter fields ---
    auto setField = [this](int i, const QString& label, double value, int prec) {
        m_pRow[i]->setVisible(true);
        m_pLbl[i]->setText(label);
        if (!m_pEdit[i]->hasFocus())                       // don't fight active typing
            m_pEdit[i]->setText(QString::number(value, 'g', prec));
    };
    if (ghs) {
        const GHSParams g = m_model->ghs();
        m_rgbBox->setVisible(false);
        setField(0, "LP", g.LP, 4);
        setField(1, "SP", g.SP, 4);
        setField(2, "HP", g.HP, 4);
        setField(3, "D",  g.D,  4);
        setField(4, "b",  g.b,  4);
    } else {
        const bool linear = m_model->fn() == StretchFn::Linear;
        const bool rgb = m_src && m_src->channels() >= 3;
        m_rgbBox->setVisible(rgb);
        if (rgb) {
            // 3×3 grid in RAW data values; B/W columns only meaningful in Linear.
            for (int c = 0; c < 3; ++c) {
                const double lo = m_model->lo(c), hi = m_model->hi(c);
                const ChannelStretch cc = m_model->channel(c);
                const double vals[3] = { lo + cc.black * (hi - lo),
                                         lo + cc.mid   * (hi - lo),
                                         lo + cc.white * (hi - lo) };
                for (int j = 0; j < 3; ++j) {
                    m_rgbEdit[c][j]->setVisible(j == 1 || linear);
                    if (!m_rgbEdit[c][j]->hasFocus())
                        m_rgbEdit[c][j]->setText(QString::number(vals[j], 'g', 6));
                }
            }
            for (int j = 0; j < 3; ++j) m_rgbColLbl[j]->setVisible(j == 1 || linear);
            for (int i = 0; i < kParamFields; ++i) m_pRow[i]->setVisible(false);
            return;
        }
        const int ec = editChannel();
        const double lo = m_model->lo(ec), hi = m_model->hi(ec);
        const ChannelStretch cs = m_model->channel(ec);
        setField(0, "Black", lo + cs.black * (hi - lo), 6);
        setField(1, "Mid",   lo + cs.mid   * (hi - lo), 6);
        setField(2, "White", lo + cs.white * (hi - lo), 6);
        m_pRow[0]->setVisible(linear);                     // B/W are the window: edit in Linear
        m_pRow[2]->setVisible(linear);
        m_pRow[3]->setVisible(false);
        m_pRow[4]->setVisible(false);
    }
}

int HistogramPanel::editChannel() const {
    const int a = m_view ? m_view->activeChannel() : -1;
    return a < 0 ? 0 : a;
}

void HistogramPanel::onParamEdited(int idx) {
    bool ok = false;
    QString txt = m_pEdit[idx]->text().trimmed();
    txt.replace(',', '.');                          // accept comma decimals too
    const double val = txt.toDouble(&ok);
    if (!ok) { syncFromModel(); return; }
    const double eps = 0.006;
    auto clamp01 = [](double v){ return v < 0 ? 0.0 : (v > 1 ? 1.0 : v); };

    if (m_model->fn() == StretchFn::GHS) {
        GHSParams g = m_model->ghs();
        if (idx == 0)      g.LP = std::min(g.SP - eps, std::max(0.0, clamp01(val)));
        else if (idx == 1) g.SP = std::min(g.HP - eps, std::max(g.LP + eps, clamp01(val)));
        else if (idx == 2) g.HP = std::max(g.SP + eps, std::min(1.0, clamp01(val)));
        else if (idx == 3) g.D  = std::min(8.0,  std::max(0.0,  val));
        else if (idx == 4) g.b  = std::min(15.0, std::max(-5.0, val));
        m_model->setGhs(g);
        return;
    }

    // Linear/Log/Asinh: idx 0=Black 1=Mid 2=White, entered as RAW data values.
    auto applyChan = [&](int c) {
        const double lo = m_model->lo(c), hi = m_model->hi(c);
        double nv = clamp01((val - lo) / std::max(1e-12, hi - lo));
        ChannelStretch cs = m_model->channel(c);
        if (idx == 0)      cs.black = std::min(cs.mid - eps, std::max(0.0, nv));
        else if (idx == 1) cs.mid   = std::min(cs.white - eps, std::max(cs.black + eps, nv));
        else if (idx == 2) cs.white = std::max(cs.mid + eps, std::min(1.0, nv));
        m_model->setChannel(c, cs);
    };
    if (m_view && m_view->activeChannel() < 0)
        for (int c = 0; c < m_model->channelCount(); ++c) applyChan(c);
    else
        applyChan(editChannel());
}

void HistogramPanel::onRgbEdited(int c, int idx) {
    bool ok = false;
    QString txt = m_rgbEdit[c][idx]->text().trimmed();
    txt.replace(',', '.');
    const double val = txt.toDouble(&ok);
    if (!ok) { syncFromModel(); return; }
    const double eps = 0.006;
    const double lo = m_model->lo(c), hi = m_model->hi(c);
    double nv = (val - lo) / std::max(1e-12, hi - lo);
    nv = nv < 0 ? 0.0 : (nv > 1 ? 1.0 : nv);
    ChannelStretch cs = m_model->channel(c);
    if (idx == 0)      cs.black = std::min(cs.mid - eps, std::max(0.0, nv));
    else if (idx == 1) cs.mid   = std::min(cs.white - eps, std::max(cs.black + eps, nv));
    else               cs.white = std::max(cs.mid + eps, std::min(1.0, nv));
    m_model->setChannel(c, cs);
}

void HistogramPanel::onAdjChanged() {
    AdjustParams a;
    a.brightness  = m_adjSlider[0]->value() / 100.0;
    a.contrast    = m_adjSlider[1]->value() / 100.0;
    a.gamma       = std::pow(3.0, m_adjSlider[2]->value() / 100.0);
    a.shadows     = m_adjSlider[3]->value() / 100.0;
    a.highlights  = m_adjSlider[4]->value() / 100.0;
    a.blackpoint  = m_adjSlider[5]->value() / 200.0;
    a.whitepoint  = 1.0 - m_adjSlider[6]->value() / 200.0;
    a.temperature = m_adjSlider[7]->value() / 100.0;
    a.tint        = m_adjSlider[8]->value() / 100.0;
    a.hue         = m_adjSlider[9]->value();
    a.saturation  = m_adjSlider[10]->value() / 100.0;
    a.vibrance    = m_adjSlider[11]->value() / 100.0;
    m_model->setAdjust(a);
}

} // namespace astro

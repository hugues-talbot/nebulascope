#include "ui/StarCombineDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <algorithm>
#include <cmath>

namespace astro {

bool    StarCombineDialog::s_hasMemory = false;
QString StarCombineDialog::s_starless;
QString StarCombineDialog::s_stars;
int     StarCombineDialog::s_amount = 100;

StarCombineDialog::StarCombineDialog(std::vector<Source> sources, QWidget* parent)
    : QDialog(parent), m_sources(std::move(sources)) {
    setWindowTitle("Combine Stars (screen)");
    auto* root = new QVBoxLayout(this);

    auto* form = new QFormLayout();
    m_starlessCombo = new QComboBox();
    m_starsCombo = new QComboBox();
    for (const Source& s : m_sources) {
        m_starlessCombo->addItem(s.name);
        m_starsCombo->addItem(s.name);
    }
    form->addRow("Starless image:", m_starlessCombo);
    form->addRow("Stars-only image:", m_starsCombo);

    auto* amtRow = new QHBoxLayout();
    m_amount = new QSlider(Qt::Horizontal);
    m_amount->setRange(0, 150);
    m_amount->setValue(100);
    m_amount->setToolTip("Star intensity k in  1 \u2212 (1\u2212starless)(1\u2212k\u00b7stars).\n"
                         "100% = plain screen; <100% dims stars; >100% boosts them.");
    m_amountLbl = new QLabel("100%");
    m_amountLbl->setMinimumWidth(40);
    amtRow->addWidget(m_amount, 1);
    amtRow->addWidget(m_amountLbl);
    form->addRow("Star amount:", amtRow);

    m_nameEdit = new QLineEdit("stars_recombined");
    form->addRow("Result name:", m_nameEdit);
    root->addLayout(form);

    m_preview = new QLabel();
    m_preview->setMinimumSize(420, 280);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet("background:#0b1016; border:1px solid #22303e;");
    root->addWidget(m_preview, 1);

    m_status = new QLabel();
    m_status->setStyleSheet("color:#c65b5b; font-size:11px;");
    root->addWidget(m_status);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &StarCombineDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);

    // Default pairing: guess from names ("starless"/"stars"), else remembered,
    // else first two entries.
    if (s_hasMemory) {
        const int i1 = m_starlessCombo->findText(s_starless);
        const int i2 = m_starsCombo->findText(s_stars);
        if (i1 >= 0) m_starlessCombo->setCurrentIndex(i1);
        if (i2 >= 0) m_starsCombo->setCurrentIndex(i2);
        m_amount->setValue(s_amount);
    } else {
        for (int i = 0; i < int(m_sources.size()); ++i) {
            const QString n = m_sources[i].name.toLower();
            if (n.contains("starless")) m_starlessCombo->setCurrentIndex(i);
            else if (n.contains("star")) m_starsCombo->setCurrentIndex(i);
        }
        if (m_starsCombo->currentIndex() == m_starlessCombo->currentIndex() &&
            m_sources.size() >= 2)
            m_starsCombo->setCurrentIndex(m_starlessCombo->currentIndex() == 0 ? 1 : 0);
    }

    auto refresh = [this] {
        m_amountLbl->setText(QStringLiteral("%1%").arg(m_amount->value()));
        updatePreview();
    };
    connect(m_starlessCombo, &QComboBox::currentIndexChanged, this, refresh);
    connect(m_starsCombo, &QComboBox::currentIndexChanged, this, refresh);
    connect(m_amount, &QSlider::valueChanged, this, refresh);
    refresh();
}

QString StarCombineDialog::resultName() const {
    const QString n = m_nameEdit->text().trimmed();
    return n.isEmpty() ? QStringLiteral("stars_recombined") : n;
}

// Downsampled (preview) or full-size screen blend of the two selections,
// both taken through their view stretches to display space first.
ImageData StarCombineDialog::blend(bool preview, QString& err) {
    const int ia = m_starlessCombo->currentIndex();
    const int ib = m_starsCombo->currentIndex();
    if (ia < 0 || ib < 0) { err = "Pick both images."; return {}; }
    if (ia == ib) { err = "Starless and stars-only must be different images."; return {}; }
    const Source& A = m_sources[ia];
    const Source& B = m_sources[ib];
    if (A.img->width() != B.img->width() || A.img->height() != B.img->height()) {
        err = QStringLiteral("Size mismatch: %1\u00d7%2 vs %3\u00d7%4.")
                  .arg(A.img->width()).arg(A.img->height())
                  .arg(B.img->width()).arg(B.img->height());
        return {};
    }
    if ((A.img->channels() >= 3) != (B.img->channels() >= 3)) {
        err = "Both images must be RGB (or both mono).";
        return {};
    }

    // Display-space copies (stride-decimated for the preview; loaders promote
    // pixels to Float32, so plane<float> is the universal view).
    const int step = preview ? std::max(1, A.img->width() / 480) : 1;
    auto sampled = [&](const Source& s) {
        const ImageData& src = *s.img;
        const int sw = src.width(), sh = src.height();
        const int dw = (sw + step - 1) / step, dh = (sh + step - 1) / step;
        const int ch = src.channels() >= 3 ? 3 : 1;
        ImageData d(dw, dh, ch, SampleFormat::Float32,
                    ch == 3 ? ColorSpace::RGB : ColorSpace::Gray);
        for (int c = 0; c < ch; ++c) {
            const float* p = src.plane<float>(c);
            float* o = d.plane<float>(c);
            for (int y = 0; y < dh; ++y) {
                const float* row = p + std::size_t(y) * step * sw;
                float* orow = o + std::size_t(y) * dw;
                for (int x = 0; x < dw; ++x) orow[x] = row[std::size_t(x) * step];
            }
        }
        if (s.toDisplay) s.toDisplay(d);             // raw -> display [0,1]
        return d;
    };
    ImageData a = sampled(A);
    ImageData b = sampled(B);

    const double k = m_amount->value() / 100.0;
    const int ch = a.channels() >= 3 ? 3 : 1;
    ImageData out(a.width(), a.height(), ch, SampleFormat::Float32,
                  ch == 3 ? ColorSpace::RGB : ColorSpace::Gray);
    const std::size_t n = out.samplesPerChannel();
    for (int c = 0; c < ch; ++c) {
        const float* pa = a.plane<float>(c);
        const float* pb = b.plane<float>(c);
        float* po = out.plane<float>(c);
        for (std::size_t i = 0; i < n; ++i) {
            const double va = std::clamp(double(pa[i]), 0.0, 1.0);
            const double vb = std::clamp(double(pb[i]) * k, 0.0, 1.0);
            po[i] = float(1.0 - (1.0 - va) * (1.0 - vb));   // screen
        }
    }
    return out;
}

void StarCombineDialog::updatePreview() {
    QString err;
    ImageData d = blend(true, err);
    if (!d.isValid()) {
        m_status->setText(err);
        m_preview->setPixmap(QPixmap());
        return;
    }
    m_status->clear();
    const int w = d.width(), h = d.height();
    QImage img(w, h, QImage::Format_RGB888);
    const float* r = d.plane<float>(0);
    const float* g = d.channels() >= 3 ? d.plane<float>(1) : r;
    const float* b = d.channels() >= 3 ? d.plane<float>(2) : r;
    auto to8 = [](float v) { return uchar(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    for (int y = 0; y < h; ++y) {
        uchar* row = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + x;
            row[x*3+0] = to8(r[i]); row[x*3+1] = to8(g[i]); row[x*3+2] = to8(b[i]);
        }
    }
    m_preview->setPixmap(QPixmap::fromImage(img).scaled(
        m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void StarCombineDialog::accept() {
    QString err;
    ImageData out = blend(false, err);
    if (!out.isValid()) { m_status->setText(err); return; }
    m_result = std::move(out);
    s_hasMemory = true;
    s_starless = m_starlessCombo->currentText();
    s_stars = m_starsCombo->currentText();
    s_amount = m_amount->value();
    QDialog::accept();
}

} // namespace astro

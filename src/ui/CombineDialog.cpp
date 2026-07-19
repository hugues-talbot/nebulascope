#include "ui/CombineDialog.h"
#include "core/Stretch.h"
#include <QtWidgets>
#include <algorithm>
#include <cmath>

namespace astro {

// Role indices used by the per-row combo box.
enum { RoleUnused = 0, RoleR, RoleG, RoleB, RoleS, RoleH, RoleO, RoleL };
static const char* kRoleNames[] = { "Unused", "R", "G", "B", "S (SII)", "H (Ha)", "O (OIII)", "L (lum)" };

// Preset list (order matches the buttons).
enum { PreSHO = 0, PreHOO, PreHSO, PreLRGB, PreRGB, PreBicolor, PresetCount };
static const char* kPresetNames[] = { "SHO", "HOO", "HSO", "LRGB", "RGB", "Bicolor" };

// (wR,wG,wB) contribution for a given role under a given preset. L handled apart.
static void presetWeights(int preset, int role, double& wR, double& wG, double& wB) {
    wR = wG = wB = 0.0;
    auto set = [&](double r, double g, double b){ wR = r; wG = g; wB = b; };
    switch (preset) {
        case PreSHO:  if (role==RoleS) set(1,0,0); else if (role==RoleH) set(0,1,0); else if (role==RoleO) set(0,0,1);
                      else if (role==RoleR) set(1,0,0); else if (role==RoleG) set(0,1,0); else if (role==RoleB) set(0,0,1); break;
        case PreHOO:  if (role==RoleH) set(1,0,0); else if (role==RoleO) set(0,1,1); break;
        case PreHSO:  if (role==RoleH) set(1,0,0); else if (role==RoleS) set(0,1,0); else if (role==RoleO) set(0,0,1); break;
        case PreLRGB: if (role==RoleR) set(1,0,0); else if (role==RoleG) set(0,1,0); else if (role==RoleB) set(0,0,1); break;
        case PreRGB:  if (role==RoleR) set(1,0,0); else if (role==RoleG) set(0,1,0); else if (role==RoleB) set(0,0,1); break;
        case PreBicolor: if (role==RoleH) set(1,0,0); else if (role==RoleO) set(0,1,1); break;
    }
}

// Guess a role from a file name (Ha / SII / OIII / R / G / B / L).
static int guessRole(const QString& nameIn) {
    const QString n = nameIn.toLower();
    auto has = [&](const char* s){ return n.contains(QString::fromLatin1(s)); };
    if (has("sii") || has("s2") || has("_s") || has("-s")) return RoleS;
    if (has("oiii") || has("o3") || has("_o") || has("-o")) return RoleO;
    if (has("ha") || has("halpha") || has("_h") || has("-h")) return RoleH;
    if (has("lum") || has("_l") || has("-l")) return RoleL;
    if (has("red") || has("_r") || has("-r")) return RoleR;
    if (has("green") || has("_g") || has("-g")) return RoleG;
    if (has("blue") || has("_b") || has("-b")) return RoleB;
    return RoleUnused;
}

// ---- local auto-STF (asinh) so the preview and "stretched" domain match the
//      normal on-open look, reusing the desktop Stretch math. -----------------
//
// The mapper evaluates the transfer ANALYTICALLY in double precision (no LUT):
// a lookup table would quantise each channel to N levels, and combining +
// re-stretching those on display produced visible colour banding (posterization).
struct Mapper { double lo = 0, hi = 1; ChannelStretch cs;
    float map(float v) const {
        if (!std::isfinite(v)) return 0.0f;
        const double t = windowCoord(v, lo, hi, cs);                 // windowed [0,1]
        const double denom = std::max(1e-6, cs.white - cs.black);
        const double m = std::min(0.999, std::max(0.001, (cs.mid - cs.black) / denom));
        return float(mtf(baseShape(t, StretchFn::Asinh), m));        // continuous, un-quantised
    }
};

// Pooled variant: one STF computed from the samples of ALL given planes. Used
// for the preview thumbnail — a per-channel STF would renormalise each output
// channel independently, cancelling out weight changes (scaling a channel by
// 0.5 rescaled its own window by 0.5 → identical thumbnail). One shared
// (linked) transfer keeps the R:G:B ratios, so weight edits are visible.
static Mapper makeMapperPooled(const float* const* planes, int nPlanes, std::size_t n) {
    float mn = 0, mx = 0; bool any = false;
    for (int k = 0; k < nPlanes; ++k)
        for (std::size_t i = 0; i < n; ++i) {
            const float v = planes[k][i]; if (!std::isfinite(v)) continue;
            if (!any) { mn = mx = v; any = true; } else { if (v<mn) mn=v; if (v>mx) mx=v; }
        }
    Mapper m; if (!any) return m;
    const std::size_t total = std::size_t(nPlanes) * n;
    const std::size_t step = total > 120000 ? total / 120000 : 1;
    std::vector<float> s; s.reserve(total/step+1);
    for (int k = 0; k < nPlanes; ++k)
        for (std::size_t i = 0; i < n; i += step)
            if (std::isfinite(planes[k][i])) s.push_back(planes[k][i]);
    float med = mn, mad = 0;
    if (!s.empty()) { const std::size_t k = s.size()/2; std::nth_element(s.begin(), s.begin()+k, s.end()); med = s[k];
        for (auto& v : s) v = std::fabs(v-med); std::nth_element(s.begin(), s.begin()+k, s.end()); mad = s[k]; }
    m.lo = mn; m.hi = mx;
    const double span = std::max(1e-6, double(mx)-double(mn));
    const double nMed = (double(med)-mn)/span; double nMad = double(mad)/span; if (nMad<1e-6) nMad=0.01;
    m.cs.black = std::min(0.5, std::max(0.0, nMed - 2.8*nMad)); m.cs.white = 1.0;
    double xx = (nMed-m.cs.black)/std::max(1e-6, m.cs.white-m.cs.black); xx = std::min(0.95, std::max(0.02, xx));
    double mm = xx*(0.25-1.0)/((2*0.25*xx)-0.25-xx); if (!(mm>0&&mm<1)) mm=0.5;
    m.cs.mid = m.cs.black + mm*(m.cs.white-m.cs.black);
    m.cs.mid = std::min(m.cs.white-1e-3, std::max(m.cs.black+1e-3, m.cs.mid));
    return m;
}

static Mapper makeMapper(const float* p, std::size_t n) {
    return makeMapperPooled(&p, 1, n);
}


// ---------------------------------------------------------------------------

CombineDialog::CombineDialog(std::vector<Source> monoSources, QWidget* parent)
    : QDialog(parent), m_sources(std::move(monoSources)) {
    setWindowTitle("Combine Channels");
    setModal(true);
    resize(880, 680);

    auto* root = new QVBoxLayout(this);

    // Presets row
    auto* presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel("<b>Preset:</b>"));
    for (int i = 0; i < PresetCount; ++i) {
        auto* b = new QPushButton(kPresetNames[i]);
        connect(b, &QPushButton::clicked, this, [this, i]{ applyPreset(i); });
        presetRow->addWidget(b);
    }
    presetRow->addStretch();
    root->addLayout(presetRow);

    // Channel table: enable | name | role | →R | →G | →B
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    const char* heads[] = { "", "Image", "Role", "→ R", "→ G", "→ B" };
    for (int c = 0; c < 6; ++c) { auto* l = new QLabel(QString("<b>%1</b>").arg(heads[c])); grid->addWidget(l, 0, c); }

    int r = 1;
    for (auto& src : m_sources) {
        Row row;
        row.w = src.img ? src.img->width() : 0;
        row.h = src.img ? src.img->height() : 0;
        row.enable = new QCheckBox();
        row.enable->setChecked(true);
        row.role = new QComboBox();
        for (const char* rn : kRoleNames) row.role->addItem(rn);
        row.role->setCurrentIndex(guessRole(src.name));
        auto mkSpin = [&]{ auto* s = new QDoubleSpinBox(); s->setRange(-4, 4); s->setSingleStep(0.05); s->setDecimals(2); return s; };
        row.wR = mkSpin(); row.wG = mkSpin(); row.wB = mkSpin();

        grid->addWidget(row.enable, r, 0);
        auto* nameLbl = new QLabel(QString("%1  <span style='color:#5f6c7a'>%2×%3</span>").arg(src.name).arg(row.w).arg(row.h));
        grid->addWidget(nameLbl, r, 1);
        grid->addWidget(row.role, r, 2);
        grid->addWidget(row.wR, r, 3);
        grid->addWidget(row.wG, r, 4);
        grid->addWidget(row.wB, r, 5);

        for (QWidget* w : { (QWidget*)row.enable, (QWidget*)row.role, (QWidget*)row.wR, (QWidget*)row.wG, (QWidget*)row.wB }) {
            if (auto* cb = qobject_cast<QCheckBox*>(w)) connect(cb, &QCheckBox::toggled, this, [this]{ updatePreview(); });
            if (auto* co = qobject_cast<QComboBox*>(w)) connect(co, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]{ updatePreview(); });
            if (auto* sp = qobject_cast<QDoubleSpinBox*>(w)) connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]{ updatePreview(); });
        }
        m_rows.push_back(row);
        ++r;
    }
    root->addLayout(grid);

    // Options row
    auto* opt = new QGridLayout();
    opt->addWidget(new QLabel("Pre-normalize:"), 0, 0);
    m_preNormCombo = new QComboBox();
    m_preNormCombo->addItems({ "None (raw × weight)", "Median (equalize background)", "Min/Max → [0,1]", "Pedestal (subtract median)" });
    opt->addWidget(m_preNormCombo, 0, 1);

    opt->addWidget(new QLabel("Data:"), 0, 2);
    m_domainCombo = new QComboBox();
    m_domainCombo->addItems({ "Linear (raw)", "Stretched (auto-STF)" });
    opt->addWidget(m_domainCombo, 0, 3);

    opt->addWidget(new QLabel("Luminance (L):"), 1, 0);
    m_lumCombo = new QComboBox();
    m_lumCombo->addItems({ "None", "Linear (add)", "Luminance (LRGB ratio)" });
    opt->addWidget(m_lumCombo, 1, 1);
    opt->addWidget(new QLabel("Amount:"), 1, 2);
    m_lumAmount = new QDoubleSpinBox(); m_lumAmount->setRange(0, 4); m_lumAmount->setValue(1.0); m_lumAmount->setSingleStep(0.05);
    opt->addWidget(m_lumAmount, 1, 3);

    opt->addWidget(new QLabel("Name:"), 2, 0);
    m_nameEdit = new QLineEdit("SHO_combine");
    opt->addWidget(m_nameEdit, 2, 1, 1, 3);
    root->addLayout(opt);

    for (QComboBox* c : { m_preNormCombo, m_domainCombo, m_lumCombo })
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]{ updatePreview(); });
    connect(m_lumAmount, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]{ updatePreview(); });

    // Preview + status
    auto* prevRow = new QHBoxLayout();
    m_preview = new QLabel(); m_preview->setFixedSize(440, 320); m_preview->setStyleSheet("background:#05070a;border:1px solid #1b2530;");
    m_preview->setAlignment(Qt::AlignCenter);
    prevRow->addWidget(m_preview);
    m_status = new QLabel("Assign roles and pick a preset."); m_status->setWordWrap(true);
    m_status->setStyleSheet("color:#8492a0;");
    prevRow->addWidget(m_status, 1);
    root->addLayout(prevRow);

    // Buttons
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Reset | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Create Image");
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, &CombineDialog::resetToDefaults);
    root->addWidget(bb);

    applyRemembered();      // reopen with last-used settings, or SHO defaults
}

QString CombineDialog::resultName() const {
    const QString n = m_nameEdit->text().trimmed();
    return n.isEmpty() ? QStringLiteral("combine") : n;
}

PreNorm CombineDialog::preNorm() const {
    switch (m_preNormCombo->currentIndex()) {
        case 1: return PreNorm::Median;
        case 2: return PreNorm::MinMax;
        case 3: return PreNorm::Pedestal;
        default: return PreNorm::None;
    }
}
LumMode CombineDialog::lumMode() const {
    switch (m_lumCombo->currentIndex()) { case 1: return LumMode::Linear; case 2: return LumMode::Luminance; default: return LumMode::None; }
}

void CombineDialog::applyPreset(int preset) {
    for (auto& row : m_rows) {
        double wR, wG, wB; presetWeights(preset, row.role->currentIndex(), wR, wG, wB);
        QSignalBlocker b1(row.wR), b2(row.wG), b3(row.wB);
        row.wR->setValue(wR); row.wG->setValue(wG); row.wB->setValue(wB);
    }
    // Sensible defaults per palette.
    const bool narrow = (preset == PreSHO || preset == PreHOO || preset == PreHSO || preset == PreBicolor);
    { QSignalBlocker b(m_preNormCombo); m_preNormCombo->setCurrentIndex(narrow ? 1 : 0); }
    { QSignalBlocker b(m_lumCombo); m_lumCombo->setCurrentIndex(preset == PreLRGB ? 2 : 0); }
    { QSignalBlocker b(m_nameEdit); m_nameEdit->setText(QString("%1_combine").arg(kPresetNames[preset])); }
    updatePreview();
}

bool CombineDialog::gatherPlanes(bool preview, std::vector<CombinePlane>& planes,
                                 const float*& lum, std::vector<std::vector<float>>& scratch,
                                 int& w, int& h, QString& err) {
    lum = nullptr;
    int refW = 0, refH = 0;
    // reference dimensions = first enabled source with data
    for (std::size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].enable->isChecked() && m_sources[i].img) { refW = m_rows[i].w; refH = m_rows[i].h; break; }
    if (refW <= 0) { err = "No channels enabled."; return false; }

    // preview downsample factor
    int dw = refW, dh = refH, stride = 1;
    if (preview) { const int maxDim = 440; stride = std::max(1, std::max(refW, refH) / maxDim); dw = (refW + stride - 1) / stride; dh = (refH + stride - 1) / stride; }
    w = dw; h = dh;

    const bool stretched = m_domainCombo->currentIndex() == 1;
    const PreNorm pnDummy = PreNorm::None; (void)pnDummy;
    scratch.reserve(m_rows.size() + 2);

    auto buildWorking = [&](const ImageData* img) -> const float* {
        const float* base = img->plane<float>(0);
        if (!preview && !stretched) return base;                 // linear full-res: use directly
        // else materialize a (possibly downsampled) buffer
        std::vector<float> buf; buf.resize(std::size_t(dw) * dh);
        for (int y = 0; y < dh; ++y) {
            const int sy = std::min(refH - 1, y * stride);
            for (int x = 0; x < dw; ++x) {
                const int sx = std::min(refW - 1, x * stride);
                buf[std::size_t(y) * dw + x] = base[std::size_t(sy) * refW + sx];
            }
        }
        if (stretched) { Mapper mp = makeMapper(buf.data(), buf.size()); for (auto& v : buf) v = mp.map(v); }
        scratch.push_back(std::move(buf));
        return scratch.back().data();
    };

    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        Row& row = m_rows[i];
        if (!row.enable->isChecked() || !m_sources[i].img) continue;
        if (row.w != refW || row.h != refH) { err = QString("“%1” is %2×%3 — all channels must match %4×%5.")
                .arg(m_sources[i].name).arg(row.w).arg(row.h).arg(refW).arg(refH); return false; }

        if (row.role->currentIndex() == RoleL) {                 // luminance source
            if (!lum) lum = buildWorking(m_sources[i].img.get());
            continue;
        }
        const double wR = row.wR->value(), wG = row.wG->value(), wB = row.wB->value();
        if (std::fabs(wR) < 1e-9 && std::fabs(wG) < 1e-9 && std::fabs(wB) < 1e-9) continue;
        CombinePlane p; p.data = buildWorking(m_sources[i].img.get()); p.wR = wR; p.wG = wG; p.wB = wB;
        planes.push_back(p);
    }
    if (planes.empty() && !(lum && lumMode() != LumMode::None)) { err = "Assign at least one channel to R, G or B."; return false; }
    return true;
}

void CombineDialog::updatePreview() {
    std::vector<CombinePlane> planes; const float* lum = nullptr;
    std::vector<std::vector<float>> scratch; int w = 0, h = 0; QString err;
    if (!gatherPlanes(true, planes, lum, scratch, w, h, err)) {
        m_preview->setText("—"); m_status->setText(err); return;
    }
    // For "stretched" domain the planes are already in [0,1]; combine wants raw,
    // so pass PreNorm::None there and the chosen prenorm for linear.
    const PreNorm pn = (m_domainCombo->currentIndex() == 1) ? PreNorm::None : preNorm();
    CombineResult res = combineChannels(w, h, planes, pn, lum, lumMode(), m_lumAmount->value());
    if (!res.ok) { m_preview->setText("—"); m_status->setText(QString::fromStdString(res.error)); return; }

    // One LINKED auto-STF across the three output channels for the thumbnail:
    // a per-channel STF would cancel out weight changes (each channel gets
    // renormalised), making the preview insensitive to the composition factors.
    // Quantise with sub-LSB triangular dither, like the main display renderer —
    // without it the (smooth, high-SNR) combined data bands visibly at 8 bits.
    QImage img(w, h, QImage::Format_RGB888);
    const float* pr = res.image.plane<float>(0); const float* pg = res.image.plane<float>(1); const float* pb = res.image.plane<float>(2);
    const float* pooled[3] = { pr, pg, pb };
    const Mapper mm = makeMapperPooled(pooled, 3, std::size_t(w)*h);
    auto hashU = [](std::uint32_t x){ x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; };
    auto dith8 = [&](float y01, std::size_t i, int c) -> uchar {
        const float r1 = (hashU(std::uint32_t(i) * 3u + std::uint32_t(c) + 0x9e3779b9u) >> 8) * (1.0f / 16777216.0f);
        const float r2 = (hashU(std::uint32_t(i >> 11) * 3u + std::uint32_t(c) * 2u + 0x85ebca6bu) >> 8) * (1.0f / 16777216.0f);
        const int o = int(y01 * 255.0f + 0.5f + (r1 + r2 - 1.0f) * 0.6f);
        return uchar(o < 0 ? 0 : (o > 255 ? 255 : o));
    };
    for (int y = 0; y < h; ++y) { uchar* row = img.scanLine(y); for (int x = 0; x < w; ++x) { const std::size_t i = std::size_t(y)*w+x;
        row[x*3+0] = dith8(mm.map(pr[i]), i, 0); row[x*3+1] = dith8(mm.map(pg[i]), i, 1); row[x*3+2] = dith8(mm.map(pb[i]), i, 2); } }
    m_preview->setPixmap(QPixmap::fromImage(img).scaled(m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_status->setText(QString("%1 colour channel(s)%2 · %3 · output %4×%5")
        .arg(planes.size())
        .arg(lum ? " + L" : "")
        .arg(m_domainCombo->currentIndex() == 1 ? "stretched" : "linear")
        .arg(m_sources.empty() ? 0 : m_rows[0].w).arg(m_sources.empty() ? 0 : m_rows[0].h));
}

void CombineDialog::accept() {
    std::vector<CombinePlane> planes; const float* lum = nullptr;
    std::vector<std::vector<float>> scratch; int w = 0, h = 0; QString err;
    if (!gatherPlanes(false, planes, lum, scratch, w, h, err)) {
        QMessageBox::warning(this, "Combine Channels", err); return;
    }
    const PreNorm pn = (m_domainCombo->currentIndex() == 1) ? PreNorm::None : preNorm();
    CombineResult res = combineChannels(w, h, planes, pn, lum, lumMode(), m_lumAmount->value());
    if (!res.ok) { QMessageBox::warning(this, "Combine Channels", QString::fromStdString(res.error)); return; }
    m_result = std::move(res.image);
    rememberSettings();     // persist for the next time the dialog opens
    QDialog::accept();
}

// ---- cross-invocation memory ----------------------------------------------

bool    CombineDialog::s_hasMemory = false;
int     CombineDialog::s_preset    = 0;   // PreSHO
int     CombineDialog::s_preNorm   = 1;   // Median
int     CombineDialog::s_domain    = 0;   // Linear
int     CombineDialog::s_lum       = 0;   // None
double  CombineDialog::s_lumAmount = 1.0;
QString CombineDialog::s_name;
QHash<QString, CombineDialog::Remembered> CombineDialog::s_perImage;

void CombineDialog::rememberSettings() {
    s_preset    = -1;                          // presets aren't a persistent control
    s_preNorm   = m_preNormCombo->currentIndex();
    s_domain    = m_domainCombo->currentIndex();
    s_lum       = m_lumCombo->currentIndex();
    s_lumAmount = m_lumAmount->value();
    s_name      = m_nameEdit->text();
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        Remembered r;
        r.role = m_rows[i].role->currentIndex();
        r.wR = m_rows[i].wR->value(); r.wG = m_rows[i].wG->value(); r.wB = m_rows[i].wB->value();
        r.enabled = m_rows[i].enable->isChecked();
        s_perImage.insert(m_sources[i].name, r);   // keyed by image name
    }
    s_hasMemory = true;
}

void CombineDialog::applyRemembered() {
    if (!s_hasMemory) { applyPreset(PreSHO); return; }
    // Global options.
    { QSignalBlocker b(m_preNormCombo); m_preNormCombo->setCurrentIndex(s_preNorm); }
    { QSignalBlocker b(m_domainCombo);  m_domainCombo->setCurrentIndex(s_domain); }
    { QSignalBlocker b(m_lumCombo);     m_lumCombo->setCurrentIndex(s_lum); }
    { QSignalBlocker b(m_lumAmount);    m_lumAmount->setValue(s_lumAmount); }
    if (!s_name.isEmpty()) { QSignalBlocker b(m_nameEdit); m_nameEdit->setText(s_name); }
    // Per-image role + weights, matched by name; images not seen before keep
    // their filename-guessed role and zero weights.
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        auto it = s_perImage.constFind(m_sources[i].name);
        if (it == s_perImage.constEnd()) continue;
        const Remembered& r = it.value();
        QSignalBlocker b0(m_rows[i].enable), b1(m_rows[i].role),
                       b2(m_rows[i].wR), b3(m_rows[i].wG), b4(m_rows[i].wB);
        m_rows[i].enable->setChecked(r.enabled);
        m_rows[i].role->setCurrentIndex(r.role);
        m_rows[i].wR->setValue(r.wR); m_rows[i].wG->setValue(r.wG); m_rows[i].wB->setValue(r.wB);
    }
    updatePreview();
}

void CombineDialog::resetToDefaults() {
    s_hasMemory = false;
    s_perImage.clear();
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        QSignalBlocker b(m_rows[i].enable), br(m_rows[i].role);
        m_rows[i].enable->setChecked(true);
        m_rows[i].role->setCurrentIndex(guessRole(m_sources[i].name));   // revert to filename guess
    }
    applyPreset(PreSHO);        // resets weights, prenorm, lum, name to defaults
}

} // namespace astro

#include "ui/RotateDialog.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QtMath>
#include <cmath>

namespace astro {

// ---- AngleKnob --------------------------------------------------------------

AngleKnob::AngleKnob(QWidget* parent) : QWidget(parent) {
    setMinimumSize(150, 150);
    setCursor(Qt::PointingHandCursor);
}

static double normDeg(double a) {
    while (a > 180.0) a -= 360.0;
    while (a < -180.0) a += 360.0;
    return a;
}

void AngleKnob::setAngle(double deg) {
    m_deg = normDeg(deg);
    update();
}

void AngleKnob::nudge(double d) {
    m_deg = normDeg(m_deg + d);
    update();
    emit angleChanged(m_deg);
}

void AngleKnob::setFromPos(const QPointF& p, bool fine) {
    const QPointF c = rect().center();
    const double dx = p.x() - c.x(), dy = p.y() - c.y();
    if (std::hypot(dx, dy) < 8.0) return;                     // dead zone at hub
    // 0° = up, positive = visually CCW (the image rotation convention).
    double a = -(qRadiansToDegrees(std::atan2(dy, dx)) + 90.0);
    a = normDeg(a);
    if (!fine) a = std::round(a * 2.0) / 2.0;                 // 0.5° snap
    if (a != m_deg) { m_deg = a; update(); emit angleChanged(m_deg); }
}

void AngleKnob::mousePressEvent(QMouseEvent* e)  { setFromPos(e->position(), e->modifiers() & Qt::ShiftModifier); }
void AngleKnob::mouseMoveEvent(QMouseEvent* e)   { if (e->buttons() & Qt::LeftButton) setFromPos(e->position(), e->modifiers() & Qt::ShiftModifier); }
void AngleKnob::mouseDoubleClickEvent(QMouseEvent*) { m_deg = 0.0; update(); emit angleChanged(m_deg); }
void AngleKnob::wheelEvent(QWheelEvent* e) {
    const double step = (e->modifiers() & Qt::ShiftModifier) ? 0.1 : 1.0;
    nudge(e->angleDelta().y() > 0 ? step : -step);
}

void AngleKnob::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    const QPointF c = rect().center();
    const double R = std::min(width(), height()) / 2.0 - 8.0;

    g.setPen(QPen(QColor("#27323e"), 2));
    g.setBrush(QColor("#0a0f15"));
    g.drawEllipse(c, R, R);

    // ticks every 15°, majors every 90°
    for (int a = 0; a < 360; a += 15) {
        const bool major = (a % 90) == 0;
        const double phi = qDegreesToRadians(double(a));
        const QPointF d(std::sin(phi), -std::cos(phi));       // 0° up, CCW
        g.setPen(QPen(major ? QColor("#5b6876") : QColor("#27323e"), major ? 2.0 : 1.0));
        g.drawLine(c + d * (R - (major ? 12 : 7)), c + d * (R - 2));
    }

    // needle
    const double phi = qDegreesToRadians(-m_deg - 90.0);
    const QPointF dir(std::cos(phi), std::sin(phi));
    g.setPen(QPen(QColor("#8fc0f5"), 2.5, Qt::SolidLine, Qt::RoundCap));
    g.drawLine(c, c + dir * (R - 14));
    g.setBrush(QColor("#8fc0f5"));
    g.setPen(Qt::NoPen);
    g.drawEllipse(c + dir * (R - 14), 4.5, 4.5);
    g.drawEllipse(c, 3.0, 3.0);

    // readout
    g.setPen(QColor("#c8d2dc"));
    QFont f = g.font(); f.setPointSizeF(11); f.setBold(true); g.setFont(f);
    g.drawText(QRectF(c.x() - 40, c.y() + R * 0.28, 80, 22), Qt::AlignCenter,
               QStringLiteral("%1\u00b0").arg(m_deg, 0, 'f', 1));
}

// ---- RotateDialog -----------------------------------------------------------

RotateDialog::RotateDialog(const QImage& thumb, double currentDeg, double northUpDeg,
                           QWidget* parent)
    : QDialog(parent), m_thumb(thumb), m_baseDeg(currentDeg) {
    setWindowTitle(QStringLiteral("Rotate Image"));
    setModal(true);

    auto* root = new QVBoxLayout(this);
    auto* row = new QHBoxLayout();
    root->addLayout(row, 1);

    m_preview = new QLabel();
    m_preview->setFixedSize(360, 320);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(QStringLiteral("background:#05070a;border:1px solid #1b2530;"));
    row->addWidget(m_preview);

    auto* side = new QVBoxLayout();
    row->addLayout(side);
    m_knob = new AngleKnob();
    side->addWidget(m_knob, 0, Qt::AlignHCenter);

    auto* spinRow = new QHBoxLayout();
    auto* lab = new QLabel(QStringLiteral("Angle"));
    m_spin = new QDoubleSpinBox();
    m_spin->setRange(-180.0, 180.0);
    m_spin->setDecimals(2);
    m_spin->setSingleStep(0.1);
    m_spin->setSuffix(QStringLiteral(" \u00b0"));
    m_spin->setValue(currentDeg);
    spinRow->addStretch();
    spinRow->addWidget(lab);
    spinRow->addWidget(m_spin);
    spinRow->addStretch();
    side->addLayout(spinRow);

    // WCS-derived preset: rotate so the central Dec line is horizontal and
    // north points up — the standard orientation for comparing telescopes/sessions.
    if (std::isfinite(northUpDeg)) {
        auto* northBtn = new QPushButton(QStringLiteral("\u2B06 North Up (%1\u00b0)")
                                             .arg(northUpDeg, 0, 'f', 2));
        northBtn->setToolTip(QStringLiteral("Set the angle that puts celestial north up at the image centre"));
        connect(northBtn, &QPushButton::clicked, this,
                [this, northUpDeg] { m_spin->setValue(northUpDeg); });   // syncs knob+preview via valueChanged
        side->addWidget(northBtn);
    }

    auto* hint = new QLabel(QStringLiteral(
        "Total rotation of this image.\nPositive = counter-clockwise.\n"
        "Drag the dial (Shift = fine), scroll,\nor type. Double-click resets to 0\u00b0.\n"
        "Re-rotation always resamples once\nfrom the original data."));
    hint->setStyleSheet(QStringLiteral("color:#5b6876;font-size:11px;"));
    side->addWidget(hint);
    side->addStretch();

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            [this] { emit applyRequested(m_spin->value()); });

    connect(m_knob, &AngleKnob::angleChanged, this, [this](double a) { setAngle(a, true); });
    connect(m_spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double a) { setAngle(a, false); });

    m_knob->setAngle(currentDeg);
    updatePreview();
}

double RotateDialog::angle() const { return m_spin->value(); }

void RotateDialog::setAngle(double deg, bool fromKnob) {
    if (fromKnob) { QSignalBlocker b(m_spin); m_spin->setValue(deg); }
    else          { m_knob->setAngle(deg); }
    updatePreview();
}

void RotateDialog::updatePreview() {
    if (m_thumb.isNull()) return;
    // The thumb already carries m_baseDeg of rotation; preview the delta.
    // QTransform::rotate is clockwise-positive on screen, ours is CCW-positive.
    QTransform t;
    t.rotate(-(m_spin->value() - m_baseDeg));
    const QImage r = m_thumb.transformed(t, Qt::SmoothTransformation);
    m_preview->setPixmap(QPixmap::fromImage(
        r.scaled(m_preview->width() - 8, m_preview->height() - 8,
                 Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

} // namespace astro

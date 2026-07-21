#pragma once
//
// RotateDialog — interactive arbitrary rotation: a circular knob for fast
// coarse setting, a spinbox for precision, and a live thumbnail preview.
// Angles are ABSOLUTE (total rotation of this image), so re-invoking the
// dialog shows the current rotation, and MainWindow always re-rotates from a
// stashed unrotated base — hunting for the right angle never accumulates
// resampling blur.
//
#include <QDialog>
#include <QWidget>
#include <QImage>

class QDoubleSpinBox;
class QLabel;

namespace astro {

// A round dial: drag to set the angle (0° = up, positive = counter-clockwise,
// matching the image rotation convention). Snaps to 0.5°; hold Shift for fine
// control; scroll wheel = ±1° (Shift: ±0.1°); double-click resets to 0°.
class AngleKnob : public QWidget {
    Q_OBJECT
public:
    explicit AngleKnob(QWidget* parent = nullptr);
    void setAngle(double deg);                    // no signal re-emit
    double angle() const { return m_deg; }
    QSize sizeHint() const override { return QSize(190, 190); }
signals:
    void angleChanged(double deg);                // user interaction only
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    void setFromPos(const QPointF& p, bool fine);
    void nudge(double d);
    double m_deg = 0.0;
};

class RotateDialog : public QDialog {
    Q_OBJECT
public:
    // thumb: a small rendering of the CURRENT display (already rotated by
    // currentDeg); the preview rotates it by the delta to the knob angle.
    // northUpDeg: absolute angle that puts celestial north up (NAN = no WCS).
    RotateDialog(const QImage& thumb, double currentDeg, double northUpDeg,
                 QWidget* parent = nullptr);
    double angle() const;
signals:
    void applyRequested(double totalDeg);         // Apply — dialog stays open
private:
    void setAngle(double deg, bool fromKnob);
    void updatePreview();
    QImage m_thumb;
    double m_baseDeg;                             // rotation baked into m_thumb
    AngleKnob* m_knob = nullptr;
    QDoubleSpinBox* m_spin = nullptr;
    QLabel* m_preview = nullptr;
};

} // namespace astro

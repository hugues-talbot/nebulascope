#include "ui/PreferencesDialog.h"
#include "core/Preferences.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QIcon>
#include <QPixmap>

namespace astro {

PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Preferences");
    Preferences& p = Preferences::get();

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    root->addLayout(form);

    auto* grid = new QSpinBox();
    grid->setRange(3, 20);
    grid->setValue(p.gridTargetLines);
    grid->setToolTip("Approximate number of RA/Dec grid lines across the frame");
    form->addRow("Grid line density:", grid);

    // The picked colour lives as a property on the button — the dialog outlives
    // this constructor, so no local may be captured by reference.
    auto* colorBtn = new QPushButton();
    auto setSwatch = [colorBtn](const QColor& c) {
        QPixmap pm(16, 16); pm.fill(c);
        colorBtn->setIcon(QIcon(pm));
        colorBtn->setText(c.name());
        colorBtn->setProperty("color", c);
    };
    setSwatch(p.annColor);
    connect(colorBtn, &QPushButton::clicked, this, [this, colorBtn, setSwatch] {
        const QColor c = QColorDialog::getColor(
            colorBtn->property("color").value<QColor>(), this, "Default annotation colour");
        if (c.isValid()) setSwatch(c);
    });
    form->addRow("Default annotation colour:", colorBtn);

    auto* textSize = new QDoubleSpinBox();
    textSize->setRange(5.0, 72.0);
    textSize->setDecimals(1);
    textSize->setValue(p.annTextSize);
    textSize->setSuffix(" pt");
    form->addRow("Annotation text size:", textSize);

    auto* lineW = new QDoubleSpinBox();
    lineW->setRange(0.0, 8.0);
    lineW->setDecimals(1);
    lineW->setSingleStep(0.5);
    lineW->setValue(p.annLineWidth);
    lineW->setSuffix(" px");
    lineW->setToolTip("Stroke width of ellipses and lines, in screen pixels (0 = hairline)");
    form->addRow("Annotation line width:", lineW);

    auto* marker = new QDoubleSpinBox();
    marker->setRange(5.0, 200.0);
    marker->setDecimals(0);
    marker->setValue(p.markerFrac);
    marker->setToolTip("\u201cAnnotate Here\u201d marker radius = image width \u00f7 this value");
    form->addRow("Marker size (width \u00f7 n):", marker);

    auto* sidecar = new QCheckBox("Auto-load <image>_annotation.json on open");
    sidecar->setChecked(p.autoLoadSidecar);
    form->addRow(QString(), sidecar);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                    | QDialogButtonBox::RestoreDefaults);
    root->addWidget(bb);
    connect(bb->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            [=]() {
        const Preferences d;                      // struct defaults
        grid->setValue(d.gridTargetLines);
        setSwatch(d.annColor);
        textSize->setValue(d.annTextSize);
        lineW->setValue(d.annLineWidth);
        marker->setValue(d.markerFrac);
        sidecar->setChecked(d.autoLoadSidecar);
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this, [=, this, &p] {
        p.gridTargetLines = grid->value();
        p.annColor        = colorBtn->property("color").value<QColor>();
        p.annTextSize     = textSize->value();
        p.annLineWidth    = lineW->value();
        p.markerFrac      = marker->value();
        p.autoLoadSidecar = sidecar->isChecked();
        p.save();
        accept();
    });
}

} // namespace astro

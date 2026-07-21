#pragma once
//
// PreferencesDialog — Edit ▸ Preferences…: edits the Preferences defaults.
// exec() == Accepted means the values were applied and saved; the caller
// refreshes anything visible (annotation layer, default colour).
//
#include <QDialog>

namespace astro {

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget* parent = nullptr);
};

} // namespace astro

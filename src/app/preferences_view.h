#pragma once

#include "app/tab_content.h"

class QCheckBox;
class QLineEdit;

namespace trowel {

class PreferencesView : public TabContent {
    Q_OBJECT
public:
    explicit PreferencesView(QWidget* parent = nullptr);

    Kind kind() const override { return Kind::Preferences; }
    QString displayName() const override { return QStringLiteral("Trowel Settings"); }

signals:
    // Emitted when the user toggles rainbow brackets so open editors can update.
    void rainbowBracketsChanged(bool enabled);

private slots:
    void commitTurmericPath();
    void commitRainbowBrackets(bool enabled);
    void restoreDefaults();

private:
    QLineEdit* turPathEdit_ = nullptr;
    QCheckBox* rainbowCheck_ = nullptr;
};

}

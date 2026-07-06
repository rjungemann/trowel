#pragma once

#include "app/tab_content.h"

class QLineEdit;

namespace trowel {

class PreferencesView : public TabContent {
    Q_OBJECT
public:
    explicit PreferencesView(QWidget* parent = nullptr);

    Kind kind() const override { return Kind::Preferences; }
    QString displayName() const override { return QStringLiteral("Trowel Settings"); }

private slots:
    void commitTurmericPath();
    void restoreDefaults();

private:
    QLineEdit* turPathEdit_ = nullptr;
};

}

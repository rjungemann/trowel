#include "app/preferences_view.h"

#include "editor/editor_view.h"
#include "editor/theme_loader.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace trowel {

PreferencesView::PreferencesView(QWidget* parent)
    : TabContent(parent)
{
    const Theme theme = LoadBuiltinDarkTheme();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Trowel Settings"), this);
    QFont titleFont = title->font();
    const int basePt = titleFont.pointSize();
    if (basePt > 0) titleFont.setPointSize(basePt + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    root->addWidget(title);

    auto* pathLabel = new QLabel(QStringLiteral("Turmeric path"), this);
    root->addWidget(pathLabel);

    turPathEdit_ = new QLineEdit(this);
    turPathEdit_->setPlaceholderText(QStringLiteral("Path to the `tur` executable (leave blank to auto-detect)"));
    turPathEdit_->setText(QSettings().value("repl/turBinary").toString());
    turPathEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(turPathEdit_, &QLineEdit::editingFinished,
            this, &PreferencesView::commitTurmericPath);
    root->addWidget(turPathEdit_);

    rainbowCheck_ = new QCheckBox(QStringLiteral("Rainbow brackets"), this);
    rainbowCheck_->setToolTip(QStringLiteral(
        "Color matching parentheses, brackets, and braces by nesting depth."));
    rainbowCheck_->setChecked(EditorView::rainbowBracketsDefault());
    connect(rainbowCheck_, &QCheckBox::toggled,
            this, &PreferencesView::commitRainbowBrackets);
    root->addWidget(rainbowCheck_);

    root->addStretch(1);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addStretch(1);
    auto* restoreButton = new QPushButton(QStringLiteral("Restore Defaults"), this);
    connect(restoreButton, &QPushButton::clicked,
            this, &PreferencesView::restoreDefaults);
    buttonRow->addWidget(restoreButton);
    root->addLayout(buttonRow);

    setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"
        "QLabel { background: transparent; }"
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; padding: 4px; }"
        "QPushButton { background: %1; color: %2; border: 1px solid %3; padding: 4px 12px; }"
        "QPushButton:hover { background: %3; }"
    ).arg(theme.editorBg.name(),
          theme.editorFg.name(),
          theme.lineNumberFg.name()));
}

void PreferencesView::commitTurmericPath() {
    if (!turPathEdit_) return;
    const QString value = turPathEdit_->text().trimmed();
    QSettings settings;
    if (value.isEmpty()) {
        settings.remove("repl/turBinary");
    } else {
        settings.setValue("repl/turBinary", value);
    }
}

void PreferencesView::commitRainbowBrackets(bool enabled) {
    QSettings().setValue("editor/rainbowBrackets", enabled);
    emit rainbowBracketsChanged(enabled);
}

void PreferencesView::restoreDefaults() {
    QSettings().remove("repl/turBinary");
    if (turPathEdit_) turPathEdit_->clear();
    QSettings().remove("editor/rainbowBrackets");
    if (rainbowCheck_) rainbowCheck_->setChecked(true);
}

}

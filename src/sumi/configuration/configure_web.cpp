// SPDX-FileCopyrightText: 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QIcon>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrentRun>
#include "common/settings.h"
#include "common/uuid.h"
#include "ui_configure_web.h"
#include "sumi/configuration/configure_web.h"
#include "sumi/uisettings.h"

ConfigureWeb::ConfigureWeb(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureWeb>()) {
    ui->setupUi(this);
    connect(ui->button_reset_token, &QPushButton::clicked, this, &ConfigureWeb::ResetToken);

#ifndef USE_DISCORD_PRESENCE
    ui->discord_group->setVisible(false);
#endif

    SetConfiguration();
    RetranslateUI();
}

ConfigureWeb::~ConfigureWeb() = default;

void ConfigureWeb::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureWeb::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureWeb::SetConfiguration() {
    ui->web_credentials_disclaimer->setWordWrap(true);

    if (Settings::values.sumi_username.GetValue().empty()) {
        ui->username->setText(tr("Unspecified"));
    } else {
        ui->username->setText(QString::fromStdString(Settings::values.sumi_username.GetValue()));
    }

    ui->edit_token->setText(QString::fromStdString(Settings::values.sumi_token.GetValue()));

    ui->toggle_discordrpc->setChecked(UISettings::values.enable_discord_presence.GetValue());
}

void ConfigureWeb::ApplyConfiguration() {
    UISettings::values.enable_discord_presence = ui->toggle_discordrpc->isChecked();
    if (Settings::values.sumi_username.GetValue().empty()) {
        // Backup: default name should already be set by ConfigureProfileManager::UpdateCurrentUser()
        Settings::values.sumi_username = "torzu";
    } else {
        // If a name already exist, reassign it to itself (needed for change set with a profile switch)
        Settings::values.sumi_username = Settings::values.sumi_username.GetValue();
    }

    if (ui->edit_token->text().isEmpty()) {
        // If no token specified, automatically generate one
        Settings::values.sumi_token = Common::UUID::MakeRandom().FormattedString();
    } else {
        // Otherwise use user-specified value
        Settings::values.sumi_token = ui->edit_token->text().toStdString();
    }
}

void ConfigureWeb::ResetToken() {
    // Generate and set token
    const auto token = Common::UUID::MakeRandom().FormattedString();
    Settings::values.sumi_token = token;
    // Just to display the label_token_icon pic and tooltip for visual confirmation
    ui->label_token_icon->setPixmap(QIcon::fromTheme(QStringLiteral("checked")).pixmap(16));
    ui->label_token_icon->setToolTip(tr("Token Changed", "Tooltip"));
    ui->username->setText(QString::fromStdString(token));
    // Apply the changes
    SetConfiguration();
}

void ConfigureWeb::SetWebServiceConfigEnabled(bool enabled) {
    ui->label_disable_info->setVisible(!enabled);
    ui->groupBoxWebConfig->setEnabled(enabled);
}

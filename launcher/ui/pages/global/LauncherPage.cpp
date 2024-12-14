// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
 *  Copyright (c) 2022 dada513 <dada513@protonmail.com>
 *  Copyright (C) 2022 Tayou <git@tayou.org>
 *  Copyright (C) 2024 TheKodeToad <TheKodeToad@proton.me>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "LauncherPage.h"
#include "ui_LauncherPage.h"

#include <QDir>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QTextCharFormat>

#include <FileSystem.h>
#include "Application.h"
#include "BuildConfig.h"
#include "DesktopServices.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"
#include "ui/themes/ITheme.h"
#include "ui/themes/ThemeManager.h"
#include "updater/ExternalUpdater.h"

#include <QApplication>

#if defined(Q_OS_MACOS) && defined(SANDBOX_ENABLED)
#include "macsandbox/DynamicSandboxException.h"
#endif

// FIXME: possibly move elsewhere
enum InstSortMode {
    // Sort alphabetically by name.
    Sort_Name,
    // Sort by which instance was launched most recently.
    Sort_LastLaunch
};

LauncherPage::LauncherPage(QWidget* parent) : QWidget(parent), ui(new Ui::LauncherPage)
{
    ui->setupUi(this);

    ui->sortingModeGroup->setId(ui->sortByNameBtn, Sort_Name);
    ui->sortingModeGroup->setId(ui->sortLastLaunchedBtn, Sort_LastLaunch);

    defaultFormat = new QTextCharFormat(ui->fontPreview->currentCharFormat());

    m_languageModel = APPLICATION->translations();
    loadSettings();

    ui->updateSettingsBox->setHidden(!APPLICATION->updater());

#if defined(Q_OS_MACOS) && defined(SANDBOX_ENABLED)
    ui->instDirTextBox->setReadOnly(true);
    ui->modsDirTextBox->setReadOnly(true);
    ui->iconsDirTextBox->setReadOnly(true);
    ui->downloadsDirTextBox->setReadOnly(true);
    ui->javaDirTextBox->setReadOnly(true);
    ui->skinsDirTextBox->setReadOnly(true);
#else
    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->macSandboxTab));
#endif

    connect(ui->fontSizeBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &LauncherPage::refreshFontPreview);
    connect(ui->consoleFont, &QFontComboBox::currentFontChanged, this, &LauncherPage::refreshFontPreview);
    connect(ui->themeCustomizationWidget, &ThemeCustomizationWidget::currentWidgetThemeChanged, this, &LauncherPage::refreshFontPreview);

    connect(ui->themeCustomizationWidget, &ThemeCustomizationWidget::currentCatChanged, APPLICATION, &Application::currentCatChanged);

#if defined(Q_OS_MACOS) && defined(SANDBOX_ENABLED)
    connect(ui->readWriteList, &DropList::droppedURLs, APPLICATION->m_dynamicSandboxExceptions.get(), &DynamicSandboxException::addReadWriteExceptions);
    connect(ui->readOnlyList, &DropList::droppedURLs, APPLICATION->m_dynamicSandboxExceptions.get(), &DynamicSandboxException::addReadOnlyExceptions);
    connect(ui->readWriteList, &DropList::droppedURLs, this, &LauncherPage::loadSettings);
    connect(ui->readOnlyList, &DropList::droppedURLs, this, &LauncherPage::loadSettings);

    connect(ui->readWriteList, &DropList::deleteKeyPressed, this, &LauncherPage::on_readWriteRemoveBtn_clicked);
    connect(ui->readOnlyList, &DropList::deleteKeyPressed, this, &LauncherPage::on_readOnlyRemoveBtn_clicked);
#endif
}

LauncherPage::~LauncherPage()
{
    delete ui;
    delete defaultFormat;
}

bool LauncherPage::apply()
{
    applySettings();
    return true;
}

void LauncherPage::on_instDirBrowseBtn_clicked()
{
    QString raw_dir = QFileDialog::getExistingDirectory(this, tr("Instance Folder"), ui->instDirTextBox->text());

    // do not allow current dir - it's dirty. Do not allow dirs that don't exist
    if (!raw_dir.isEmpty() && QDir(raw_dir).exists()) {
        QString cooked_dir = FS::NormalizePath(raw_dir);
        if (FS::checkProblemticPathJava(QDir(cooked_dir))) {
            QMessageBox warning;
            warning.setText(
                tr("You're trying to specify an instance folder which\'s path "
                   "contains at least one \'!\'. "
                   "Java is known to cause problems if that is the case, your "
                   "instances (probably) won't start!"));
            warning.setInformativeText(
                tr("Do you really want to use this path? "
                   "Selecting \"No\" will close this and not alter your instance path."));
            warning.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            int result = warning.exec();
            if (result == QMessageBox::Ok) {
                ui->instDirTextBox->setText(cooked_dir);
            }
        } else if (DesktopServices::isFlatpak() && raw_dir.startsWith("/run/user")) {
            QMessageBox warning;
            warning.setText(tr("You're trying to specify an instance folder "
                               "which was granted temporarily via Flatpak.\n"
                               "This is known to cause problems. "
                               "After a restart the launcher might break, "
                               "because it will no longer have access to that directory.\n\n"
                               "Granting %1 access to it via Flatseal is recommended.")
                                .arg(BuildConfig.LAUNCHER_DISPLAYNAME));
            warning.setInformativeText(tr("Do you want to proceed anyway?"));
            warning.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            int result = warning.exec();
            if (result == QMessageBox::Ok) {
                ui->instDirTextBox->setText(cooked_dir);
            }
        } else {
            ui->instDirTextBox->setText(cooked_dir);
        }
    }
}

void LauncherPage::on_instDirResetBtn_clicked()
{
    auto defValue = APPLICATION->settings()->getSetting("InstanceDir")->defValue().toString();
    ui->instDirTextBox->setText(defValue);
}

void LauncherPage::on_iconsDirBrowseBtn_clicked()
{
    QString raw_dir = QFileDialog::getExistingDirectory(this, tr("Icons Folder"), ui->iconsDirTextBox->text());

    // do not allow current dir - it's dirty. Do not allow dirs that don't exist
    if (!raw_dir.isEmpty() && QDir(raw_dir).exists()) {
        QString cooked_dir = FS::NormalizePath(raw_dir);
        ui->iconsDirTextBox->setText(cooked_dir);
    }
}

void LauncherPage::on_iconsDirResetBtn_clicked()
{
    auto defValue = APPLICATION->settings()->getSetting("IconsDir")->defValue().toString();
    ui->iconsDirTextBox->setText(defValue);
}

void LauncherPage::on_modsDirBrowseBtn_clicked()
{
    QString raw_dir = QFileDialog::getExistingDirectory(this, tr("Mods Folder"), ui->modsDirTextBox->text());

    // do not allow current dir - it's dirty. Do not allow dirs that don't exist
    if (!raw_dir.isEmpty() && QDir(raw_dir).exists()) {
        QString cooked_dir = FS::NormalizePath(raw_dir);
        ui->modsDirTextBox->setText(cooked_dir);
    }
}

void LauncherPage::on_modsDirResetBtn_clicked()
{
    auto defValue = APPLICATION->settings()->getSetting("CentralModsDir")->defValue().toString();
    ui->modsDirTextBox->setText(defValue);
}

void LauncherPage::on_downloadsDirBrowseBtn_clicked()
{
    QString raw_dir = QFileDialog::getExistingDirectory(this, tr("Downloads Folder"), ui->downloadsDirTextBox->text());

    if (!raw_dir.isEmpty() && QDir(raw_dir).exists()) {
        QString cooked_dir = FS::NormalizePath(raw_dir);
        ui->downloadsDirTextBox->setText(cooked_dir);
    }
}

void LauncherPage::on_downloadsDirResetBtn_clicked()
{
    auto defValue = APPLICATION->settings()->getSetting("DownloadsDir")->defValue().toString();
    ui->downloadsDirTextBox->setText(defValue);
}

void LauncherPage::on_javaDirBrowseBtn_clicked()
{
    QString raw_dir = QFileDialog::getExistingDirectory(this, tr("Java Folder"), ui->javaDirTextBox->text());

    if (!raw_dir.isEmpty() && QDir(raw_dir).exists()) {
        QString cooked_dir = FS::NormalizePath(raw_dir);
        ui->javaDirTextBox->setText(cooked_dir);
    }
}

void LauncherPage::on_javaDirResetBtn_clicked()
{
    auto defValue = APPLICATION->settings()->getSetting("JavaDir")->defValue().toString();
    ui->javaDirTextBox->setText(defValue);
}

void LauncherPage::on_skinsDirBrowseBtn_clicked()
{
    QString raw_dir = QFileDialog::getExistingDirectory(this, tr("Skins Folder"), ui->skinsDirTextBox->text());

    // do not allow current dir - it's dirty. Do not allow dirs that don't exist
    if (!raw_dir.isEmpty() && QDir(raw_dir).exists()) {
        QString cooked_dir = FS::NormalizePath(raw_dir);
        ui->skinsDirTextBox->setText(cooked_dir);
    }
}

void LauncherPage::on_skinsDirResetBtn_clicked()
{
    auto defValue = APPLICATION->settings()->getSetting("SkinsDir")->defValue().toString();
    ui->skinsDirTextBox->setText(defValue);
}

void LauncherPage::on_metadataDisableBtn_clicked()
{
    ui->metadataWarningLabel->setHidden(!ui->metadataDisableBtn->isChecked());
}

void LauncherPage::on_readWriteAddBtn_clicked() {
#if defined(Q_OS_MACOS) && defined(SANDBOX_ENABLED)
    QString dir = QFileDialog::getExistingDirectory(this, tr("Add Read/Write Exception"), QDir::homePath());
    if (!dir.isEmpty()) {
        APPLICATION->m_dynamicSandboxExceptions->addReadWriteException(dir);
        loadSettings();
    }
#endif
}

void LauncherPage::on_readWriteRemoveBtn_clicked() {
#if defined(Q_OS_MACOS) && defined(SANDBOX_ENABLED)
    int row = ui->readWriteList->currentRow();
    if (row >= 0) {
        APPLICATION->m_dynamicSandboxExceptions->removeReadWriteException(row);
        loadSettings();
    }
#endif
}

void LauncherPage::on_readOnlyAddBtn_clicked() {
#if defined(Q_OS_MACOS) && defined(SANDBOX_ENABLED)
    QString dir = QFileDialog::getExistingDirectory(this, tr("Add Read Only Exception"), QDir::homePath());
    if (!dir.isEmpty()) {
        APPLICATION->m_dynamicSandboxExceptions->addReadOnlyException(dir);
        loadSettings();
    }
#endif
}

void LauncherPage::on_readOnlyRemoveBtn_clicked() {
#if defined(Q_OS_MACOS) && defined(SANDBOX_ENABLED)
    int row = ui->readOnlyList->currentRow();
    if (row >= 0) {
        APPLICATION->m_dynamicSandboxExceptions->removeReadOnlyException(row);
        loadSettings();
    }
#endif
}

void LauncherPage::applySettings()
{
    auto s = APPLICATION->settings();

    // Updates
    if (APPLICATION->updater()) {
        APPLICATION->updater()->setAutomaticallyChecksForUpdates(ui->autoUpdateCheckBox->isChecked());
        APPLICATION->updater()->setUpdateCheckInterval(ui->updateIntervalSpinBox->value() * 3600);
    }

    s->set("MenuBarInsteadOfToolBar", ui->preferMenuBarCheckBox->isChecked());

    s->set("NumberOfConcurrentTasks", ui->numberOfConcurrentTasksSpinBox->value());
    s->set("NumberOfConcurrentDownloads", ui->numberOfConcurrentDownloadsSpinBox->value());
    s->set("NumberOfManualRetries", ui->numberOfManualRetriesSpinBox->value());
    s->set("RequestTimeout", ui->timeoutSecondsSpinBox->value());

    // Console settings
    s->set("ShowConsole", ui->showConsoleCheck->isChecked());
    s->set("AutoCloseConsole", ui->autoCloseConsoleCheck->isChecked());
    s->set("ShowConsoleOnError", ui->showConsoleErrorCheck->isChecked());
    QString consoleFontFamily = ui->consoleFont->currentFont().family();
    s->set("ConsoleFont", consoleFontFamily);
    s->set("ConsoleFontSize", ui->fontSizeBox->value());
    s->set("ConsoleMaxLines", ui->lineLimitSpinBox->value());
    s->set("ConsoleOverflowStop", ui->checkStopLogging->checkState() != Qt::Unchecked);

    // Folders
    // TODO: Offer to move instances to new instance folder.
    s->set("InstanceDir", ui->instDirTextBox->text());
    s->set("CentralModsDir", ui->modsDirTextBox->text());
    s->set("IconsDir", ui->iconsDirTextBox->text());
    s->set("DownloadsDir", ui->downloadsDirTextBox->text());
    s->set("SkinsDir", ui->skinsDirTextBox->text());
    s->set("JavaDir", ui->javaDirTextBox->text());
    s->set("DownloadsDirWatchRecursive", ui->downloadsDirWatchRecursiveCheckBox->isChecked());

    auto sortMode = (InstSortMode)ui->sortingModeGroup->checkedId();
    switch (sortMode) {
        case Sort_LastLaunch:
            s->set("InstSortMode", "LastLaunch");
            break;
        case Sort_Name:
        default:
            s->set("InstSortMode", "Name");
            break;
    }

    // Cat
    s->set("CatOpacity", ui->catOpacitySpinBox->value());

    // Mods
    s->set("ModMetadataDisabled", ui->metadataDisableBtn->isChecked());
    s->set("ModDependenciesDisabled", ui->dependenciesDisableBtn->isChecked());
    s->set("SkipModpackUpdatePrompt", ui->skipModpackUpdatePromptBtn->isChecked());
}
void LauncherPage::loadSettings()
{
    auto s = APPLICATION->settings();
    // Updates
    if (APPLICATION->updater()) {
        ui->autoUpdateCheckBox->setChecked(APPLICATION->updater()->getAutomaticallyChecksForUpdates());
        ui->updateIntervalSpinBox->setValue(APPLICATION->updater()->getUpdateCheckInterval() / 3600);
    }

    // Toolbar/menu bar settings (not applicable if native menu bar is present)
    ui->toolsBox->setEnabled(!QMenuBar().isNativeMenuBar());
#ifdef Q_OS_MACOS
    ui->toolsBox->setVisible(!QMenuBar().isNativeMenuBar());
#endif
    ui->preferMenuBarCheckBox->setChecked(s->get("MenuBarInsteadOfToolBar").toBool());

    ui->numberOfConcurrentTasksSpinBox->setValue(s->get("NumberOfConcurrentTasks").toInt());
    ui->numberOfConcurrentDownloadsSpinBox->setValue(s->get("NumberOfConcurrentDownloads").toInt());
    ui->numberOfManualRetriesSpinBox->setValue(s->get("NumberOfManualRetries").toInt());
    ui->timeoutSecondsSpinBox->setValue(s->get("RequestTimeout").toInt());

    // Console settings
    ui->showConsoleCheck->setChecked(s->get("ShowConsole").toBool());
    ui->autoCloseConsoleCheck->setChecked(s->get("AutoCloseConsole").toBool());
    ui->showConsoleErrorCheck->setChecked(s->get("ShowConsoleOnError").toBool());
    QString fontFamily = APPLICATION->settings()->get("ConsoleFont").toString();
    QFont consoleFont(fontFamily);
    ui->consoleFont->setCurrentFont(consoleFont);

    bool conversionOk = true;
    int fontSize = APPLICATION->settings()->get("ConsoleFontSize").toInt(&conversionOk);
    if (!conversionOk) {
        fontSize = 11;
    }
    ui->fontSizeBox->setValue(fontSize);
    refreshFontPreview();
    ui->lineLimitSpinBox->setValue(s->get("ConsoleMaxLines").toInt());
    ui->checkStopLogging->setChecked(s->get("ConsoleOverflowStop").toBool());

    // Folders
    ui->instDirTextBox->setText(s->get("InstanceDir").toString());
    ui->modsDirTextBox->setText(s->get("CentralModsDir").toString());
    ui->iconsDirTextBox->setText(s->get("IconsDir").toString());
    ui->downloadsDirTextBox->setText(s->get("DownloadsDir").toString());
    ui->skinsDirTextBox->setText(s->get("SkinsDir").toString());
    ui->javaDirTextBox->setText(s->get("JavaDir").toString());
    ui->downloadsDirWatchRecursiveCheckBox->setChecked(s->get("DownloadsDirWatchRecursive").toBool());

    QString sortMode = s->get("InstSortMode").toString();

    if (sortMode == "LastLaunch") {
        ui->sortLastLaunchedBtn->setChecked(true);
    } else {
        ui->sortByNameBtn->setChecked(true);
    }

    // Cat
    ui->catOpacitySpinBox->setValue(s->get("CatOpacity").toInt());

    // Mods
    ui->metadataDisableBtn->setChecked(s->get("ModMetadataDisabled").toBool());
    ui->metadataWarningLabel->setHidden(!ui->metadataDisableBtn->isChecked());
    ui->dependenciesDisableBtn->setChecked(s->get("ModDependenciesDisabled").toBool());
    ui->skipModpackUpdatePromptBtn->setChecked(s->get("SkipModpackUpdatePrompt").toBool());

#if defined(Q_OS_MACOS) && defined(SANDBOX_ENABLED)
    // macOS sandbox user-selected dynamic exceptions
    QList<QUrl> readWriteURLs = APPLICATION->m_dynamicSandboxExceptions->readWriteExceptionURLs();
    QList<QUrl> readOnlyURLs = APPLICATION->m_dynamicSandboxExceptions->readOnlyExceptionURLs();

    QFileIconProvider iconProvider;
    ui->readWriteList->clear();
    for (const QUrl& url : readWriteURLs) {
        if (url.isEmpty())
            continue;
        if (url.scheme() == "file") {
            QIcon fileIcon = iconProvider.icon(QFileInfo(url.toLocalFile()));
            auto item = new QListWidgetItem(fileIcon, url.toLocalFile());
            ui->readWriteList->addItem(item);
        }
    }
    ui->readOnlyList->clear();
    for (const QUrl& url : readOnlyURLs) {
        if (url.isEmpty())
            continue;
        if (url.scheme() == "file") {
            QIcon fileIcon = iconProvider.icon(QFileInfo(url.toLocalFile()));
            auto item = new QListWidgetItem(fileIcon, url.toLocalFile());
            ui->readOnlyList->addItem(item);
        }
    }
#endif
}

void LauncherPage::refreshFontPreview()
{
    const LogColors& colors = APPLICATION->themeManager()->getLogColors();

    int fontSize = ui->fontSizeBox->value();
    QString fontFamily = ui->consoleFont->currentFont().family();
    ui->fontPreview->clear();
    defaultFormat->setFont(QFont(fontFamily, fontSize));

    auto print = [this, colors](const QString& message, MessageLevel::Enum level) {
        QTextCharFormat format(*defaultFormat);

        QColor bg = colors.background.value(level);
        QColor fg = colors.foreground.value(level);

        if (bg.isValid())
            format.setBackground(bg);

        if (fg.isValid())
            format.setForeground(fg);

        // append a paragraph/line
        auto workCursor = ui->fontPreview->textCursor();
        workCursor.movePosition(QTextCursor::End);
        workCursor.insertText(message, format);
        workCursor.insertBlock();
    };

    print(QString("%1 version: %2 (%3)\n")
              .arg(BuildConfig.LAUNCHER_DISPLAYNAME, BuildConfig.printableVersionString(), BuildConfig.BUILD_PLATFORM),
          MessageLevel::Launcher);

    QDate today = QDate::currentDate();

    if (today.month() == 10 && today.day() == 31)
        print(tr("[Test/ERROR] OOoooOOOoooo! A spooky error!"), MessageLevel::Error);
    else
        print(tr("[Test/ERROR] A spooky error!"), MessageLevel::Error);

    print(tr("[Test/INFO] A harmless message..."), MessageLevel::Info);
    print(tr("[Test/WARN] A not so spooky warning."), MessageLevel::Warning);
    print(tr("[Test/DEBUG] A secret debugging message..."), MessageLevel::Debug);
    print(tr("[Test/FATAL] A terrifying fatal error!"), MessageLevel::Fatal);
}

void LauncherPage::retranslate()
{
    ui->retranslateUi(this);
}

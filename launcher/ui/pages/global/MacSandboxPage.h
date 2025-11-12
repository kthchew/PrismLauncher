// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2025 Kenneth Chew <79120643+kthchew@users.noreply.github.com>
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
 */

#ifndef LAUNCHER_MACSANDBOXPAGE_H
#define LAUNCHER_MACSANDBOXPAGE_H

#include <QMainWindow>
#include "ui/pages/BasePage.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MacSandboxPage;
}
QT_END_NAMESPACE

class MacSandboxPage : public QMainWindow, public BasePage {
    Q_OBJECT

public:
    explicit MacSandboxPage(QWidget* parent = nullptr);
    ~MacSandboxPage() override;
    void loadSettings();

    QString displayName() const override { return tr("Sandbox"); }
    QIcon icon() const override { return QIcon::fromTheme("settings"); }
    QString id() const override { return "launcher-mac-sandbox"; }
    QString helpPage() const override { return "Launcher-mac-sandbox"; }

private:
    Ui::MacSandboxPage* ui;

private slots:
    void on_readWriteAddBtn_clicked();
    void on_readWriteRemoveBtn_clicked();
    void on_readOnlyAddBtn_clicked();
    void on_readOnlyRemoveBtn_clicked();
};

#endif  // LAUNCHER_MACSANDBOXPAGE_H

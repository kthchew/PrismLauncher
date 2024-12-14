// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2024 Kenneth Chew <79120643+kthchew@users.noreply.github.com>
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

#include <qglobal.h>
#ifdef Q_OS_MACOS
#include "Application.h"
#include "XPCBridge.h"
#include "XPCManager.h"
#include "ui/dialogs/CustomMessageBox.h"

#include <stdio.h>
#include <sys/syslimits.h>
#include <sys/un.h>
#include <unistd.h>

#include <QLocalSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QtCore/QEventLoop>

XPCBridge::XPCBridge(QObject* parent) : QObject(parent)
{
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
        qWarning() << "Failed to create socket pair for XPC bridge";
        return;
    }
    m_launcherSocket = sockets[0];
    m_gameSocket = sockets[1];

    m_launcherNotifier.reset(new QSocketNotifier(m_launcherSocket, QSocketNotifier::Read, this));
    connect(m_launcherNotifier.get(), &QSocketNotifier::activated, this, &XPCBridge::onReadyRead);
}
XPCBridge::~XPCBridge()
{
    close(m_launcherSocket);
    close(m_gameSocket);
}

int XPCBridge::getGameSocketDescriptor() const
{
    return m_gameSocket;
}

void XPCBridge::onReadyRead() const
{
    // get path from client, a char* array that ends with a null byte
    char path[PATH_MAX];
    ssize_t bytesRead = read(m_launcherSocket, path, sizeof(path) - 1);
    if (bytesRead == -1) {
        qWarning() << "Failed to read path from XPC bridge";
        return;
    }
    path[bytesRead] = '\0';
    std::pair<bool, std::string> res = APPLICATION->m_xpcManager->askToRemoveQuarantine(path);
    qDebug() << "Got response from XPC:" << (res.first ? "Quarantine removed for" : "Quarantine not removed for") << res.second.c_str();
    send(m_launcherSocket, &res.first, sizeof(res.first), 0);
    send(m_launcherSocket, res.second.c_str(), res.second.size() + 1, 0);
}
#endif

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

#include "XPCBridge.h"
#include "XPCManager.h"
#include "ui/dialogs/CustomMessageBox.h"

#include <sys/syslimits.h>
#include <sys/un.h>

#include <QLocalSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QtCore/QEventLoop>

XPCBridge::XPCBridge()
{
    server = new QLocalServer();
    startListening();
}
XPCBridge::~XPCBridge()
{
    server->close();
    delete server;
}

QString XPCBridge::getSocketPath() const
{
    return server->fullServerName();
}

void XPCBridge::onNewConnection() const
{
    QLocalSocket* clientConnection = server->nextPendingConnection();
    if (!clientConnection) {
        return;
    }

    // get path from client, a char* array that ends with a null byte
    char path[PATH_MAX];
    // FIXME: can cause a freeze on the main UI thread
    clientConnection->waitForReadyRead();
    clientConnection->read(path, sizeof(path));
    path[sizeof(path) - 1] = '\0';
    std::pair<bool, std::string> res = askToRemoveQuarantine(path);
    qDebug() << "Got response from XPC:" << (res.first ? "Quarantine removed for" : "Quarantine not removed for") << res.second.c_str();

    clientConnection->write(reinterpret_cast<const char*>(&res.first), sizeof(res.first));
    clientConnection->write(res.second.c_str(), res.second.size() + 1);
    clientConnection->flush();
    clientConnection->close();
    clientConnection->deleteLater();
}
void XPCBridge::startListening()
{
    bool pathTooLong = qEnvironmentVariable("TMPDIR").length() + 1 >= sizeof(sockaddr_un::sun_path);
    if (pathTooLong) {
        auto sandboxFailStr = tr("Failed to start services required for sandboxing. Minecraft may fail to start.\n\n"
            "The data directory path is too long and is currently unsupported by the sandboxed version, which usually results from a long computer username.\n\n"
            "Please download the unsandboxed version of Prism Launcher.");
        auto dialog = CustomMessageBox::selectable(nullptr, "Initialization Error", sandboxFailStr, QMessageBox::Critical);
        dialog->exec();
        return;
    }

    int maxSocketRange = 9;
    bool success = false;
    for (int i = 0; i <= maxSocketRange; i++) {
        QString socketPath = QString::number(i);
        QLocalServer::removeServer(socketPath);
        server->listen(socketPath);
        if (!server->isListening()) {
            qWarning() << "XPC Bridge failed to listen on socket at " << socketPath;
        } else {
            qDebug() << "XPC Bridge listening on socket at " << server->fullServerName();
            connect(server, &QLocalServer::newConnection, this, &XPCBridge::onNewConnection);
            success = true;
            break;
        }
    }

    if (!success) {
        auto sandboxFailStr = tr("Failed to start services required for sandboxing. Minecraft may fail to start.\n\n"
            "Please close and reopen the launcher. If this issue persists, please try the unsandboxed version of Prism Launcher.");
        auto dialog = CustomMessageBox::selectable(nullptr, "Initialization Error", sandboxFailStr, QMessageBox::Critical);
        dialog->exec();
    }
}
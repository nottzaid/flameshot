// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "hyprland.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QPixmap>
#include <QProcess>
#include <QScreen>

QScreen* Hyprland::focusedScreen()
{
    const QString signature =
      qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (signature.isEmpty() || runtimeDir.isEmpty()) {
        return nullptr;
    }

    QLocalSocket socket;
    socket.connectToServer(runtimeDir + QStringLiteral("/hypr/") + signature +
                           QStringLiteral("/.socket.sock"));
    if (!socket.waitForConnected(500)) {
        return nullptr;
    }
    socket.write("j/monitors");
    socket.waitForBytesWritten(500);
    // Hyprland closes the connection once it has replied.
    QByteArray reply;
    while (socket.waitForReadyRead(500)) {
        reply += socket.readAll();
    }
    reply += socket.readAll();

    const QJsonArray monitors = QJsonDocument::fromJson(reply).array();
    for (const QJsonValue& monitor : monitors) {
        if (!monitor[QStringLiteral("focused")].toBool()) {
            continue;
        }
        const QString name = monitor[QStringLiteral("name")].toString();
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screen->name() == name) {
                return screen;
            }
        }
    }
    return nullptr;
}

bool Hyprland::grabDesktop(QPixmap& screenshot)
{
    if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")) {
        return false;
    }
    QProcess grim;
    grim.start(
      QStringLiteral("grim"),
      { QStringLiteral("-t"), QStringLiteral("ppm"), QStringLiteral("-") });
    if (!grim.waitForFinished(10000) ||
        grim.exitStatus() != QProcess::NormalExit || grim.exitCode() != 0) {
        return false;
    }
    return screenshot.loadFromData(grim.readAllStandardOutput(), "PPM");
}

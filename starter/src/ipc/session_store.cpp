#include "session_store.h"
#include "platform/contracts/session_identity.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace pet {
static QString identity(const Session &session) {
    return platform::sessionProcessIdentity(session.provider, session.host.pids);
}
bool saveSessions(const QString &path, const Sessions &sessions) {
    QJsonObject processes;
    for (auto it = sessions.records().begin(); it != sessions.records().end(); ++it) {
        const auto process = identity(it.value());
        if (!process.isEmpty()) processes[it.key()] = process;
    }
    const auto data = QJsonDocument(QJsonObject{{"format", 1}, {"processes", processes},
        {"state", QString::fromLatin1(sessions.checkpoint().toBase64())}}).toJson(QJsonDocument::Compact);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return file.write(data) == data.size() && file.commit();
}
bool loadSessions(const QString &path, Sessions &sessions, qint64 now) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 12 * 1024 * 1024) return false;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (object["format"].toInt() != 1) return false;
    const auto processes = object["processes"].toObject();
    return sessions.restore(QByteArray::fromBase64(object["state"].toString().toLatin1()), now,
        [&](const Session &session) {
            const auto saved = processes[session.provider + QChar(0x1f) + session.id].toString();
            return !saved.isEmpty() && saved == identity(session);
        });
}
}

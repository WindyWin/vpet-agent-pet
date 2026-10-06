#include "session_store.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace pet {
// A PID alone can be reused. Include the boot ID and kernel process start time;
// checking the agent executable avoids mistaking a surviving terminal for an agent.
static QString identity(const Session &session) {
#ifdef Q_OS_LINUX
    QFile boot("/proc/sys/kernel/random/boot_id");
    if (!boot.open(QIODevice::ReadOnly)) return {};
    const auto bootId = QString::fromLatin1(boot.readAll()).trimmed();
    for (const auto pid : session.host.pids) {
        const auto base = QString("/proc/%1/").arg(pid);
        QFile comm(base + "comm"), stat(base + "stat");
        if (!comm.open(QIODevice::ReadOnly) || !stat.open(QIODevice::ReadOnly)) continue;
        const auto name = QString::fromUtf8(comm.readAll()).trimmed();
        const auto executable = QFileInfo(QFileInfo(base + "exe").symLinkTarget()).fileName();
        if (name != session.provider && executable != session.provider) continue;
        const auto line = stat.readAll();
        const auto fields = line.mid(line.lastIndexOf(')') + 2).split(' ');
        if (fields.size() <= 19 || fields[0] == "Z" || fields[0] == "X") continue;
        return bootId + ':' + QString::number(pid) + ':' + QString::fromLatin1(fields[19]);
    }
#endif
    return {};
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

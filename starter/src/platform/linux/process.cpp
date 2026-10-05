#include "process.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace pet::platform {
QVector<qint64> processAncestors(qint64 pid, int limit) {
    QVector<qint64> chain;
    while (pid > 1 && chain.size() < limit && !chain.contains(pid)) {
        chain.append(pid);
        QFile stat(QString("/proc/%1/stat").arg(pid));
        if (!stat.open(QIODevice::ReadOnly)) break;
        // "pid (comm) state ppid ..."; comm may contain spaces and parentheses.
        const auto line = QString::fromUtf8(stat.read(4096));
        const auto fields = line.mid(line.lastIndexOf(')') + 2).split(' ');
        pid = fields.value(1).toLongLong();
    }
    return chain;
}
QVector<ProcessInfo> LinuxProcesses::terminalClients(const QString &executable) const {
    QVector<ProcessInfo> clients;
    for (const auto &entry : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto pid = entry.toLongLong();
        if (pid <= 1) continue;
        const auto base = "/proc/" + entry;
        if (QFileInfo(QFileInfo(base + "/exe").symLinkTarget()).fileName() != executable) continue;
        if (!QFileInfo(base + "/fd/0").symLinkTarget().startsWith("/dev/pts/")) continue;
        QFile cmdline(base + "/cmdline"), environ(base + "/environ");
        if (!cmdline.open(QIODevice::ReadOnly) || !environ.open(QIODevice::ReadOnly)) continue;
        auto args = QString::fromLocal8Bit(cmdline.readAll()).split(QChar(0), Qt::SkipEmptyParts);
        if (args.isEmpty()) continue;
        args.removeFirst();
        QProcessEnvironment env;
        for (const auto &pair : environ.readAll().split('\0')) {
            const auto equals = pair.indexOf('=');
            if (equals > 0) env.insert(QString::fromLocal8Bit(pair.left(equals)), QString::fromLocal8Bit(pair.mid(equals + 1)));
        }
        clients.append({pid, args, env});
    }
    return clients;
}
}

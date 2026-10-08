#include "platform/contracts/session_identity.h"
#include <QFile>
#include <QFileInfo>
namespace pet::platform {
// A PID can be reused; match the agent name, boot ID and kernel creation time.
QString sessionProcessIdentity(const QString &provider, const QVector<qint64> &pids) {
    QFile boot("/proc/sys/kernel/random/boot_id");
    if (!boot.open(QIODevice::ReadOnly)) return {};
    const auto bootId = QString::fromLatin1(boot.readAll()).trimmed();
    for (const auto pid : pids) {
        const auto base = QString("/proc/%1/").arg(pid);
        QFile comm(base + "comm"), stat(base + "stat");
        if (!comm.open(QIODevice::ReadOnly) || !stat.open(QIODevice::ReadOnly)) continue;
        const auto name = QString::fromUtf8(comm.readAll()).trimmed();
        const auto executable = QFileInfo(QFileInfo(base + "exe").symLinkTarget()).fileName();
        if (name != provider && executable != provider) continue;
        const auto line = stat.readAll();
        const auto fields = line.mid(line.lastIndexOf(')') + 2).split(' ');
        if (fields.size() <= 19 || fields[0] == "Z" || fields[0] == "X") continue;
        return bootId + ':' + QString::number(pid) + ':' + QString::fromLatin1(fields[19]);
    }
    return {};
}
}

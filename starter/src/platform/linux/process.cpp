#include "process.h"
#include <QFile>
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
}

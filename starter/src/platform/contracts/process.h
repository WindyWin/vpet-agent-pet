#pragma once
#include <QProcessEnvironment>
#include <QStringList>
#include <QVector>

namespace pet::platform {
struct ProcessInfo {
    qint64 pid = 0;
    QStringList arguments; // Without the program itself.
    QProcessEnvironment environment;
};
// The limited process inspection host adapters need.
class ProcessServices {
public:
    virtual ~ProcessServices() = default;
    // Parent chain, nearest first, excluding the init process.
    virtual QVector<qint64> ancestors(qint64 pid, int limit = 16) const = 0;
    // Kernel command names of the processes, in order; empty where unreadable.
    virtual QStringList names(const QVector<qint64> &pids) const = 0;
    // Running instances of an executable attached to a terminal: interactive clients, not servers.
    virtual QVector<ProcessInfo> terminalClients(const QString &executable) const = 0;
};
}

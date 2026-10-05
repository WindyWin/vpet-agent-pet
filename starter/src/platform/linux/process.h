#pragma once
#include "platform/contracts/process.h"

namespace pet::platform {
// Parent chain of a process from /proc, nearest first, excluding pid 1.
QVector<qint64> processAncestors(qint64 pid, int limit = 16);
// Kernel command names (/proc/<pid>/comm) of the processes, in order.
QStringList processNames(const QVector<qint64> &pids);
// Process services from /proc.
class LinuxProcesses : public ProcessServices {
public:
    QVector<qint64> ancestors(qint64 pid, int limit = 16) const override { return processAncestors(pid, limit); }
    QStringList names(const QVector<qint64> &pids) const override { return processNames(pids); }
    QVector<ProcessInfo> terminalClients(const QString &executable) const override;
};
}

#pragma once
#include "platform/contracts/process.h"

namespace pet::platform {
// Parent chain of a process from a Toolhelp snapshot, nearest first, excluding the
// System and Idle processes. A recorded parent ID can outlive its process and be
// reused, so the chain stops at a "parent" that started after its child.
QVector<qint64> processAncestors(qint64 pid, int limit = 16);
// Executable file names (such as "WindowsTerminal.exe") of the processes, in order.
QStringList processNames(const QVector<qint64> &pids);
// Process services from Toolhelp snapshots. tmux and herdr do not run natively on
// Windows, so there are no terminal clients to find.
class WindowsProcesses : public ProcessServices {
public:
    QVector<qint64> ancestors(qint64 pid, int limit = 16) const override { return processAncestors(pid, limit); }
    QStringList names(const QVector<qint64> &pids) const override { return processNames(pids); }
    QVector<ProcessInfo> terminalClients(const QString &) const override { return {}; }
};
}

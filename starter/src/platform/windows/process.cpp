#include "process.h"
#include <QHash>
#include <windows.h>
#include <tlhelp32.h>

namespace pet::platform {
namespace {
struct Entry { qint64 parent = 0; QString name; };
QHash<qint64, Entry> snapshot() {
    QHash<qint64, Entry> processes;
    const HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (handle == INVALID_HANDLE_VALUE) return processes;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(handle, &entry); more; more = Process32NextW(handle, &entry))
        processes.insert(entry.th32ProcessID, {qint64(entry.th32ParentProcessID), QString::fromWCharArray(entry.szExeFile)});
    CloseHandle(handle);
    return processes;
}
// Creation time in 100 ns units, or 0 when the process is gone or cannot be queried.
quint64 started(qint64 pid) {
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!process) return 0;
    FILETIME creation{}, exit{}, kernel{}, user{};
    const bool ok = GetProcessTimes(process, &creation, &exit, &kernel, &user);
    CloseHandle(process);
    return ok ? (quint64(creation.dwHighDateTime) << 32) | creation.dwLowDateTime : 0;
}
}
QVector<qint64> processAncestors(qint64 pid, int limit) {
    QVector<qint64> chain;
    const auto processes = snapshot();
    // PID 4 is System and 0 the Idle process: neither is anyone's host.
    while (pid > 4 && chain.size() < limit && !chain.contains(pid)) {
        chain.append(pid);
        const auto entry = processes.constFind(pid);
        if (entry == processes.constEnd()) break;
        const auto child = started(pid), parent = started(entry->parent);
        if (!child || !parent || parent > child) break;
        pid = entry->parent;
    }
    return chain;
}
QStringList processNames(const QVector<qint64> &pids) {
    const auto processes = snapshot();
    QStringList names;
    for (const auto pid : pids) names << processes.value(pid).name;
    return names;
}
}

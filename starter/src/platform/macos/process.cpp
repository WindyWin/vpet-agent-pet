#include "process.h"
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <libproc.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <cstring>
#include <vector>

namespace pet::platform {
static bool processInfo(qint64 pid, kinfo_proc &info) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, int(pid)};
    size_t size = sizeof(info);
    // A missing process succeeds with no data.
    return sysctl(mib, 4, &info, &size, nullptr, 0) == 0 && size == sizeof(info);
}
QVector<qint64> processAncestors(qint64 pid, int limit) {
    QVector<qint64> chain;
    while (pid > 1 && chain.size() < limit && !chain.contains(pid)) {
        chain.append(pid);
        kinfo_proc info{};
        if (!processInfo(pid, info)) break;
        pid = info.kp_eproc.e_ppid;
    }
    return chain;
}
QStringList processNames(const QVector<qint64> &pids) {
    QStringList names;
    for (const auto pid : pids) {
        kinfo_proc info{};
        names << (processInfo(pid, info) ? QString::fromUtf8(info.kp_proc.p_comm, int(strnlen(info.kp_proc.p_comm, sizeof(info.kp_proc.p_comm))))
                                         : QString());
    }
    return names;
}
// KERN_PROCARGS2: argc, the executable path, padding, argv strings, then environment strings.
// Readable for the user's own processes only.
static bool processArguments(pid_t pid, QStringList &arguments, QProcessEnvironment &environment) {
    int argmax = 0; size_t size = sizeof(argmax);
    int mibArgmax[2] = {CTL_KERN, KERN_ARGMAX};
    if (sysctl(mibArgmax, 2, &argmax, &size, nullptr, 0) || argmax <= int(sizeof(int))) return false;
    std::vector<char> buffer(size_t(argmax));
    size = buffer.size();
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, int(pid)};
    if (sysctl(mib, 3, buffer.data(), &size, nullptr, 0) || size <= sizeof(int)) return false;
    int argc = 0; std::memcpy(&argc, buffer.data(), sizeof(argc));
    size_t position = sizeof(argc);
    while (position < size && buffer[position] != '\0') ++position; // Executable path.
    while (position < size && buffer[position] == '\0') ++position; // Padding.
    auto next = [&]() {
        const auto start = position;
        while (position < size && buffer[position] != '\0') ++position;
        const auto text = QString::fromLocal8Bit(buffer.data() + start, int(position - start));
        if (position < size) ++position;
        return text;
    };
    QStringList argv;
    for (int i = 0; i < argc && position < size; ++i) argv << next();
    if (argv.isEmpty()) return false;
    argv.removeFirst();
    arguments = argv;
    while (position < size) {
        const auto pair = next();
        if (pair.isEmpty()) break;
        const auto equals = pair.indexOf('=');
        if (equals > 0) environment.insert(pair.left(equals), pair.mid(equals + 1));
    }
    return true;
}
QVector<ProcessInfo> MacProcesses::terminalClients(const QString &executable) const {
    QVector<ProcessInfo> clients;
    const int count = proc_listallpids(nullptr, 0);
    if (count <= 0) return clients;
    std::vector<pid_t> pids(size_t(count) + 64);
    const int listed = proc_listallpids(pids.data(), int(pids.size() * sizeof(pid_t)));
    for (int i = 0; i < listed && i < int(pids.size()); ++i) {
        const pid_t pid = pids[size_t(i)];
        if (pid <= 1) continue;
        char path[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_pidpath(pid, path, sizeof(path)) <= 0 || QFileInfo(QFile::decodeName(path)).fileName() != executable) continue;
        // Interactive clients have a controlling terminal; daemonized servers do not.
        kinfo_proc info{};
        if (!processInfo(pid, info) || info.kp_eproc.e_tdev == NODEV) continue;
        ProcessInfo client; client.pid = pid;
        if (!processArguments(pid, client.arguments, client.environment)) continue;
        clients.append(client);
    }
    return clients;
}
}

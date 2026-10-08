#include "platform/contracts/session_identity.h"
#include <QFileInfo>
#include <windows.h>
#include <limits>
#include <iterator>
namespace pet::platform {
QString sessionProcessIdentity(const QString &provider, const QVector<qint64> &pids) {
    for (const auto pid : pids) {
        if (pid <= 0 || quint64(pid) > std::numeric_limits<DWORD>::max()) continue;
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, DWORD(pid));
        if (!process) continue;
        FILETIME creation{}, exit{}, kernel{}, user{};
        wchar_t image[32768]; DWORD length = DWORD(std::size(image));
        const bool valid = WaitForSingleObject(process, 0) == WAIT_TIMEOUT
            && GetProcessTimes(process, &creation, &exit, &kernel, &user)
            && QueryFullProcessImageNameW(process, 0, image, &length);
        CloseHandle(process);
        if (!valid) continue;
        const QString executable = QFileInfo(QString::fromWCharArray(image, int(length))).completeBaseName();
        if (QString::compare(executable, provider, Qt::CaseInsensitive) != 0) continue;
        const quint64 started = (quint64(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
        if (started) return "windows:" + QString::number(pid) + ':' + QString::number(started);
    }
    return {};
}
}

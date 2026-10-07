#include "platform/contracts/hook_input.h"
#include "process.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <windows.h>

namespace pet::platform {
qint64 parentProcessId() {
    const auto chain = processAncestors(QCoreApplication::applicationPid(), 2);
    return chain.value(1);
}
// Agents write the event to an anonymous pipe and close it. Pipes cannot be waited on, so the
// deadline is kept by peeking; a redirected file never blocks.
bool readHookInput(int inputLimit, QByteArray &data, QString &error) {
    data.clear();
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (!input || input == INVALID_HANDLE_VALUE) { error = "Cannot read event"; return false; }
    const auto type = GetFileType(input);
    if (type != FILE_TYPE_PIPE && type != FILE_TYPE_DISK) { error = "Cannot read event"; return false; }
    QElapsedTimer deadline; deadline.start();
    while (deadline.elapsed() < 150) {
        DWORD available = 0;
        if (type == FILE_TYPE_PIPE && !PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) return true; // The writer closed: end of input.
            error = "Cannot read event"; return false;
        }
        if (type == FILE_TYPE_PIPE && !available) { Sleep(1); continue; }
        char buffer[8193];
        DWORD size = 0;
        if (!ReadFile(input, buffer, sizeof(buffer), &size, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) return true;
            error = "Cannot read event"; return false;
        }
        if (size == 0) return true;
        data.append(buffer, int(size));
        if (data.size() > inputLimit) { error = "Input exceeds size limit"; return false; }
    }
    error = "Timed out reading event";
    return false;
}
}

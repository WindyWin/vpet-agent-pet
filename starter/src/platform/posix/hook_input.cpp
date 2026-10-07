#include "platform/contracts/hook_input.h"
#include <QElapsedTimer>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
namespace pet::platform {
qint64 parentProcessId() { return getppid(); }
WindowRef agentConsoleWindow(const QVector<qint64> &) { return {}; }
bool readHookInput(int inputLimit, QByteArray &data, QString &error) {
    data.clear();
    QElapsedTimer deadline; deadline.start();
    while (deadline.elapsed() < 150) {
        pollfd input{STDIN_FILENO, POLLIN, 0};
        const int result = poll(&input, 1, int(150 - deadline.elapsed()));
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) { error = "Timed out reading event"; return false; }
        char buffer[8193];
        const auto size = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (size == 0) break;
        if (size < 0) { error = "Cannot read event"; return false; }
        data.append(buffer, size);
        if (data.size() > inputLimit) { error = "Input exceeds size limit"; return false; }
    }
    if (deadline.elapsed() >= 150) { error = "Timed out reading event"; return false; }
    return true;
}
}

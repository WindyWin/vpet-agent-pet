#pragma once
#include "window.h"
#include <QByteArray>
#include <QString>
#include <QVector>
namespace pet::platform {
// Read one EOF-terminated input, bounded by inputLimit and a 150 ms deadline.
// On failure, error describes the problem; hook orchestration owns fail-open policy.
bool readHookInput(int inputLimit, QByteArray &data, QString &error);
qint64 parentProcessId();
// The visible window of the console the agent runs in, found from the hook's ancestors
// (nearest first). Null where consoles have no windows of their own (POSIX terminals
// export $WINDOWID instead) or none is visible. Call it after reading the input.
WindowRef agentConsoleWindow(const QVector<qint64> &ancestors);
}

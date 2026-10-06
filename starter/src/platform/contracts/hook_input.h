#pragma once
#include <QByteArray>
#include <QString>
namespace pet::platform {
// Read one EOF-terminated input, bounded by inputLimit and a 150 ms deadline.
// On failure, error describes the problem; hook orchestration owns fail-open policy.
bool readHookInput(int inputLimit, QByteArray &data, QString &error);
qint64 parentProcessId();
}

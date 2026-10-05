#pragma once
#include <QVector>

namespace pet::platform {
// Parent chain of a process from /proc, nearest first, excluding pid 1.
QVector<qint64> processAncestors(qint64 pid, int limit = 16);
}

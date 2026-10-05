#pragma once
#include <QString>
#include <QVector>

namespace pet::platform {
// A top-level window as a desktop backend lists it; the ID is backend-owned.
struct WindowInfo { QString id; qint64 pid = 0; QString title; };
// The window to raise: the preferred one when listed, else the windows of the
// nearest process hint, preferring the one whose title names the project (VS Code
// runs all of its windows from one process). Empty when nothing matches.
QString matchWindow(const QString &preferred, const QVector<qint64> &pids, const QString &project,
                    const QVector<WindowInfo> &windows);
}

#pragma once
#include <QString>
#include <QVector>
namespace pet::platform {
// Stable identity of a live agent process; empty if no candidate can be verified.
QString sessionProcessIdentity(const QString &provider, const QVector<qint64> &pids);
}

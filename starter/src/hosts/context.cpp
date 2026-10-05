#include "context.h"
#include <QStringList>

namespace pet::hosts {
QString joinPids(const QVector<qint64> &pids) {
    QStringList list;
    for (const auto pid : pids) list << QString::number(pid);
    return list.join(',');
}
QVector<qint64> splitPids(const QString &pids) {
    QVector<qint64> list;
    for (const auto &pid : pids.split(',', Qt::SkipEmptyParts)) list.append(pid.toLongLong());
    return list;
}
HostContext fromV1(const QString &host, const QString &pids, const QString &window, const QString &target) {
    HostContext context{host, splitPids(pids), {}, target};
    if (!window.isEmpty()) context.window = {x11Backend, window};
    return context;
}
QJsonObject toV1(const HostContext &context) {
    QJsonObject out;
    if (context.isNull()) return out;
    out["host"] = context.adapter;
    if (!context.target.isEmpty() && context.target.size() <= maxTarget) out["host_target"] = context.target;
    if (!context.pids.isEmpty()) out["host_pids"] = joinPids(context.pids.mid(0, maxPids));
    // v1 has no field for another backend's window; it is left out, never reinterpreted.
    if (context.window.backend == x11Backend && !context.window.id.isEmpty()) out["host_window"] = context.window.id;
    return out;
}
}

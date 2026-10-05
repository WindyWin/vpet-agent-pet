#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace pet::hosts {
// A native window owned by one desktop backend, such as {"x11", "<decimal id>"}.
// Shared code stores and forwards it; only the named backend interprets the ID.
struct WindowRef {
    QString backend, id;
    bool isNull() const { return backend.isEmpty() || id.isEmpty(); }
    bool operator==(const WindowRef &other) const { return backend == other.backend && id == other.id; }
};
// Where an agent session runs: the host adapter that owns it, process hints
// (nearest ancestor first), an optional window, and target data that only the
// adapter interprets. Identifiers only: never titles, commands or output.
struct HostContext {
    QString adapter;
    QVector<qint64> pids;
    WindowRef window;
    QString target;
    bool isNull() const { return adapter.isEmpty(); }
};

// Event protocol v1 carries the context as four strings (see docs/events.md):
//   host        adapter ID
//   host_pids   comma-separated process hints, at most 16
//   host_window decimal X11 window ($WINDOWID)
//   host_target adapter-owned target, at most 256 characters
constexpr int maxPids = 16, maxTarget = 256;
constexpr auto x11Backend = "x11";
// Converts already validated v1 fields (see Registry::validV1).
HostContext fromV1(const QString &host, const QString &pids, const QString &window, const QString &target);
QJsonObject toV1(const HostContext &context);
QString joinPids(const QVector<qint64> &pids);
QVector<qint64> splitPids(const QString &pids);
}

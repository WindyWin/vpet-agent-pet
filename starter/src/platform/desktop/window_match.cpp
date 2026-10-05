#include "window_match.h"
#include <QDir>
#include <QFileInfo>

namespace pet::platform {
QString matchWindow(const QString &preferred, const QVector<qint64> &pids, const QString &project,
                    const QVector<WindowInfo> &windows) {
    if (!preferred.isEmpty()) for (const auto &w : windows) if (w.id == preferred) return w.id;
    const auto name = QFileInfo(QDir::cleanPath(project)).fileName();
    for (const auto pid : pids) {
        QVector<WindowInfo> owned;
        for (const auto &w : windows) if (w.pid == pid) owned.append(w);
        if (owned.isEmpty()) continue;
        if (!name.isEmpty())
            for (const auto &w : owned) if (w.title.contains(name, Qt::CaseInsensitive)) return w.id;
        return owned.first().id;
    }
    return {};
}
}

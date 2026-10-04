#pragma once
#include "sessions/presence.h"
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

namespace pet {
struct Preferences {
    static constexpr int defaultSize = 240;
    static constexpr int recoveryMs = 15000;
    int size = defaultSize;
    QPoint position;
    bool hasPosition = false;
    bool onTop = true;
    bool muted = false; // Hides alert bubbles; the attention badge stays visible.
    bool sound = false; // Optional system beep for newly raised alerts.
    // Which alerts pop a bubble. Pet animation and badge react to everything regardless.
    enum Bubbles { RequestsOnly = 0, RequestsAndErrors = 1, AllAlerts = 2 };
    int bubbles = RequestsAndErrors;
    // Startup keys. `agent-pet autostart` edits them without a display, possibly
    // while a pet runs, so the pet re-reads them before each save.
    bool autostart = false; // Hook launches the pet on a session start when none is running.
    IdlePolicy whenIdle = IdlePolicy::Keep;
    static QPoint visiblePosition(QPoint position, QSize size, const QVector<QRect> &screens);
};
class PreferencesStore {
public:
    explicit PreferencesStore(QString path = {});
    Preferences load();
    bool save(const Preferences &preferences);
    QString error() const { return error_; }
    QString path() const { return path_; }
private:
    QString path_, error_;
    bool writable_ = true;
};
}

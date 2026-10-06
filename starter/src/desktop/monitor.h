#pragma once
#include "alert_bubble.h"
#include "hosts/focus_service.h"
#include "ipc/local.h"
#include "pet_window.h"
#include "session_list.h"
#include "sessions/alerts.h"
#include "sessions/recap.h"
#include <QTimer>
#include <functional>
#include <memory>

namespace pet {
// Connects received events to session state, aggregate playback, the attention
// badge, the alert bubble and the running-sessions list. It lives as long as the
// pet, so closing settings or preview never interrupts monitoring; Quit stops it.
class Monitor : public QObject {
    Q_OBJECT
public:
    // Focus is optional: without it, sessions cannot be brought forward or checked for being in view.
    explicit Monitor(PetWindow &window, std::shared_ptr<hosts::FocusService> focus = nullptr);
    // Takes a receiver that already holds the single-instance lock.
    void listen(std::unique_ptr<Receiver> receiver);
    bool apply(const Event &event, qint64 now);
    void update(qint64 now);
    void stop();
    void restoreSessions(const QString &path);
    bool checkpointSessions();
    bool active() const { return active_; }
    Sessions &sessions() { return sessions_; }
    AlertQueue &queue() { return queue_; }
    AlertBubble &bubble() { return bubble_; }
    NoteBubble &note() { return note_; }
    SessionList &sessionList() { return list_; }
    Recap &recap() { return recap_; }
    // Says today's recap in the speech bubble; a click on it shows the per-project breakdown.
    void showRecap();
    void dismiss();
    void toggleSessions();
    // Brings the session's terminal or editor forward; false when its window was not raised.
    bool focusSession(const QString &key);
    void focusCurrent();
    // Replaceable for tests. Defaults use the focus service given at construction.
    // Is the user already looking at this session? Only Active suppresses its new alerts.
    std::function<platform::ActiveState(const Session &)> hostActive;
    std::function<hosts::FocusResult(const Session &)> bringForward;
    // Where the pointer is; moving it counts as activity for wellness reminders.
    std::function<QPoint()> pointer;
    // Whether the screen is locked; while it is, nothing counts as activity and no reminder shows. Unset: never.
    std::function<bool()> locked;
    static QString bedtimeNote(); // Shown once a night when a turn finishes late.
    // The wellness reminder whose note is showing, until it is answered or another note replaces it.
    QString reminder() const { return reminder_; }
    int restLeft() const { return restLeft_; } // Seconds left of an eye break the user took; 0 for none.
    void setRestTickMs(int ms) { rest_.setInterval(ms); } // One countdown second; tests shorten it.
private:
    void refreshAlerts();
    void remind();
    void remindWellness(qint64 now);
    bool calm(qint64 now) const;
    void answered();
    void rest();
    void dropReminder(); // Hides a shown reminder or countdown without counting it as answered.
    void say(const QString &text, const QString &details = {}, int ms = NoteBubble::defaultMs);
    bool shown(const Alert &alert) const;
    PetWindow &window_;
    std::shared_ptr<hosts::FocusService> focus_;
    Sessions sessions_;
    AlertQueue queue_;
    AlertBubble bubble_;
    NoteBubble note_; // The pet's own remarks: easter-egg reminders and the bedtime note.
    SessionList list_;
    RecapStore recapStore_;
    Recap recap_; // Persisted shortly after each change, and when monitoring stops.
    std::unique_ptr<Receiver> receiver_;
    QTimer timer_, rest_, recapTimer_;
    QString lastAggregate_, reminder_, sessionPath_;
    QPoint lastPointer_;
    int restLeft_ = 0;
    qint64 lastTurnMs_ = 0; // How long the latest finished turn ran, for a long-turn celebration.
    quint64 heard_ = 0;
    bool observed_ = false, active_ = true;
};
}

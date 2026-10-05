#pragma once
#include "alert_bubble.h"
#include "hosts/focus_service.h"
#include "ipc/local.h"
#include "pet_window.h"
#include "session_list.h"
#include "sessions/alerts.h"
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
    bool active() const { return active_; }
    Sessions &sessions() { return sessions_; }
    AlertQueue &queue() { return queue_; }
    AlertBubble &bubble() { return bubble_; }
    SessionList &sessionList() { return list_; }
    void dismiss();
    void toggleSessions();
    // Brings the session's terminal or editor forward; false when its window was not raised.
    bool focusSession(const QString &key);
    void focusCurrent();
    // Replaceable for tests. Defaults use the focus service given at construction.
    // Is the user already looking at this session? Only Active suppresses its new alerts.
    std::function<platform::ActiveState(const Session &)> hostActive;
    std::function<hosts::FocusResult(const Session &)> bringForward;
    static const QString bedtimeNote; // Shown once a night when a turn finishes late.
private:
    void refreshAlerts();
    void remind();
    bool shown(const Alert &alert) const;
    PetWindow &window_;
    std::shared_ptr<hosts::FocusService> focus_;
    Sessions sessions_;
    AlertQueue queue_;
    AlertBubble bubble_;
    SessionList list_;
    std::unique_ptr<Receiver> receiver_;
    QTimer timer_;
    QString lastAggregate_;
    qint64 lastTurnMs_ = 0; // How long the latest finished turn ran, for a long-turn celebration.
    quint64 heard_ = 0;
    bool observed_ = false, active_ = true;
};
}

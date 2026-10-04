#pragma once
#include "alert_bubble.h"
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
    explicit Monitor(PetWindow &window);
    bool listen(QString &error);
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
    // Brings the session's terminal or editor forward; false when it cannot be found.
    bool focusSession(const QString &key);
    void focusCurrent();
    // Replaceable for tests: is the user already looking at this session's window?
    std::function<bool(const Session &)> hostActive; // default: hostFocus::active
    std::function<bool(const Session &)> bringForward; // default: hostFocus::focus
private:
    void refreshAlerts();
    bool shown(const Alert &alert) const;
    PetWindow &window_;
    Sessions sessions_;
    AlertQueue queue_;
    AlertBubble bubble_;
    SessionList list_;
    std::unique_ptr<Receiver> receiver_;
    QTimer timer_;
    QString lastAggregate_;
    quint64 heard_ = 0;
    bool observed_ = false, active_ = true;
};
}

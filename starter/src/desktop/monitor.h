#pragma once
#include "alert_bubble.h"
#include "ipc/local.h"
#include "pet_window.h"
#include "sessions/alerts.h"
#include <QTimer>
#include <memory>

namespace pet {
// Connects received events to session state, aggregate playback, the attention
// badge and the alert bubble. It lives as long as the pet, so closing settings or
// preview never interrupts monitoring; the pet's Quit stops it.
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
    void next();
    void dismiss();
private:
    void refreshAlerts();
    PetWindow &window_;
    Sessions sessions_;
    AlertQueue queue_;
    AlertBubble bubble_;
    std::unique_ptr<Receiver> receiver_;
    QTimer timer_;
    QString lastAggregate_;
    quint64 heard_ = 0;
    bool observed_ = false, active_ = true;
};
}

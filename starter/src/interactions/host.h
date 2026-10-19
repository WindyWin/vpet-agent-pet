#pragma once
#include <QObject>
#include <QSet>
#include <QTimer>
#include <memory>
#include "interactions/service.h"
#include "platform/contracts/interaction_transport.h"

namespace pet::interactions {

// Connects a Service to a transport and drives its timeouts. Initially hosted in the desktop process;
// a headless host can own the same pair later. Exactly one host may own an endpoint.
class Host : public QObject {
public:
    Host(Service &service, std::unique_ptr<platform::InteractionTransport> transport, QObject *parent = nullptr);
    ~Host() override;
    bool start(QString &error);
private:
    Service &service_;
    std::unique_ptr<platform::InteractionTransport> transport_;
    QTimer timer_;
    QSet<int> attached_;
};

}

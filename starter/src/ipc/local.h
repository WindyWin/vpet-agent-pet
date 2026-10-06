#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include "sessions/state.h"
#include "platform/contracts/event_transport.h"
namespace pet {
class Receiver : public QObject {
public:
    explicit Receiver(QObject *parent = nullptr);
    explicit Receiver(std::unique_ptr<platform::EventTransport> transport, QObject *parent = nullptr);
    ~Receiver() override;
    bool start(QString &error);
    std::function<void(const Event &)> received;
private:
    std::unique_ptr<platform::EventTransport> transport_;
};
// Sends one normalized event datagram to the running pet.
bool sendEvent(const QByteArray &data, QString &error);
// These commands use QCoreApplication only and require no display server.
int eventCommand(const QStringList &args);
}

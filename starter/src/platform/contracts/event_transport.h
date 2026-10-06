#pragma once
#include <QByteArray>
#include <QString>
#include <functional>
#include <memory>

namespace pet::platform {
class EventTransport {
public:
    virtual ~EventTransport() = default;
    virtual bool start(QString &error) = 0;
    std::function<void(const QByteArray &)> received;
};
// Selected by the application build. Implementations own endpoint permissions,
// instance ownership and bounded delivery; they never parse session events.
std::unique_ptr<EventTransport> createEventTransport();
bool sendDatagram(const QByteArray &data, QString &error);
}

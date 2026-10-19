#pragma once
#include <QByteArray>
#include <QString>
#include <functional>
#include <memory>

namespace pet::platform {
// A private, bidirectional, same-user stream for the interaction protocol, kept apart from the
// content-free status datagrams (EventTransport). Implementations own the endpoint's permissions,
// single-instance ownership, framing and its 64 KiB frame limit, connection lifecycle and bounded
// output queues; they deliver whole JSON payloads and never interpret them.
class InteractionTransport {
public:
    virtual ~InteractionTransport() = default;
    virtual bool start(QString &error) = 0;
    // False when the connection is gone or its output queue is full (the connection is then closed).
    virtual bool send(int connection, const QByteArray &payload) = 0;
    virtual void close(int connection) = 0;
    std::function<void(int)> connected;
    std::function<void(int)> disconnected;
    std::function<void(int, const QByteArray &)> received;
};

// The endpoint a name maps to: a socket path in the private runtime directory on POSIX, a pipe name on
// Windows. An empty name is the application's endpoint; tests pass their own.
QString interactionEndpoint(const QString &name = {});
std::unique_ptr<InteractionTransport> createInteractionTransport(const QString &name = {});
}

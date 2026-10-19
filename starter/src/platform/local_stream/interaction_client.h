#pragma once
#include <QByteArray>
#include <QLocalSocket>
#include <QObject>
#include <functional>
#include "interactions/frame.h"

namespace pet::platform {
// The connecting side of the interaction stream, for provider bridges written in C++ and for tests.
class InteractionClient : public QObject {
public:
    explicit InteractionClient(QObject *parent = nullptr);
    // Blocks up to timeoutMs for the connection; the event loop must run afterwards for frames to arrive.
    bool connectTo(const QString &name, QString &error, int timeoutMs = 2000);
    bool send(const QByteArray &payload);
    // Writes raw bytes without framing, for protocol tests.
    bool sendRaw(const QByteArray &bytes);
    void close();
    bool connected() const;
    std::function<void(const QByteArray &)> received;
    std::function<void()> disconnected;
private:
    QLocalSocket socket_;
    interactions::FrameDecoder decoder_;
};
}

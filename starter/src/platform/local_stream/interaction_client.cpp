#include "platform/local_stream/interaction_client.h"
#include "platform/contracts/interaction_transport.h"

namespace pet::platform {

InteractionClient::InteractionClient(QObject *parent) : QObject(parent) {
    connect(&socket_, &QLocalSocket::readyRead, this, [this] {
        if (!decoder_.feed(socket_.readAll())) { socket_.abort(); return; }
        QByteArray payload;
        while (decoder_.next(payload))
            if (received) received(payload);
    });
    connect(&socket_, &QLocalSocket::disconnected, this, [this] {
        if (disconnected) disconnected();
    });
}

bool InteractionClient::connectTo(const QString &name, QString &error, int timeoutMs) {
    socket_.connectToServer(interactionEndpoint(name));
    if (!socket_.waitForConnected(timeoutMs)) { error = socket_.errorString(); return false; }
    return true;
}

bool InteractionClient::send(const QByteArray &payload) { return sendRaw(interactions::encodeFrame(payload)); }

bool InteractionClient::sendRaw(const QByteArray &bytes) {
    return socket_.state() == QLocalSocket::ConnectedState && socket_.write(bytes) == bytes.size();
}

void InteractionClient::close() { socket_.abort(); }

bool InteractionClient::connected() const { return socket_.state() == QLocalSocket::ConnectedState; }

}

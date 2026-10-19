#include "interactions/host.h"

namespace pet::interactions {

Host::Host(Service &service, std::unique_ptr<platform::InteractionTransport> transport, QObject *parent)
    : QObject(parent), service_(service), transport_(std::move(transport)) {
    auto *transport_ptr = transport_.get();
    transport_->connected = [this, transport_ptr](int id) {
        attached_.insert(id);
        service_.attach(id, [transport_ptr, id](const QByteArray &payload) { return transport_ptr->send(id, payload); });
    };
    transport_->received = [this](int id, const QByteArray &payload) { service_.receive(id, payload); };
    transport_->disconnected = [this](int id) { attached_.remove(id); service_.detach(id); };
    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, [this] { service_.expire(); });
}

Host::~Host() {
    // The service outlives the host: leave no send callback pointing at the transport.
    for (const int id : std::as_const(attached_)) service_.detach(id);
}

bool Host::start(QString &error) {
    if (!transport_->start(error)) return false;
    timer_.start();
    return true;
}

}

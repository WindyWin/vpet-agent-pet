#include "screen_lock.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace pet::platform {
ScreenLock::ScreenLock(QObject *parent) : QObject(parent) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;
    for (const auto *name : {"org.freedesktop.ScreenSaver", "org.gnome.ScreenSaver"}) {
        const QString service(name), path = "/" + QString(name).replace('.', '/');
        bus.connect(service, path, service, "ActiveChanged", this, SLOT(changed(bool)));
        // Asynchronous, so a missing or slow service never stalls the pet.
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(QDBusMessage::createMethodCall(service, path, service, "GetActive")), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
            const QDBusPendingReply<bool> reply = *call;
            if (reply.isValid() && reply.value()) locked_ = true;
            call->deleteLater();
        });
    }
}
}

#include "screen_lock.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace pet::platform {
ScreenLock::ScreenLock(QObject *parent) : QObject(parent) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;
    const char *names[] = {"org.freedesktop.ScreenSaver", "org.gnome.ScreenSaver"};
    const char *handlers[] = {SLOT(freedesktopChanged(bool)), SLOT(gnomeChanged(bool))};
    for (int i = 0; i < 2; ++i) {
        const QString service(names[i]), path = "/" + QString(names[i]).replace('.', '/');
        bus.connect(service, path, service, "ActiveChanged", this, handlers[i]);
        // Asynchronous, so a missing or slow service never stalls the pet.
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(QDBusMessage::createMethodCall(service, path, service, "GetActive")), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, i](QDBusPendingCallWatcher *call) {
            const QDBusPendingReply<bool> reply = *call;
            if (!signalled_[i] && reply.isValid()) active_[i] = reply.value();
            call->deleteLater();
        });
    }
}
}

#pragma once
#include <QObject>

namespace pet::platform {
// Whether the session's screen is locked (the screen saver is active), from the desktop's D-Bus screen
// saver: org.freedesktop.ScreenSaver (KDE and others) and org.gnome.ScreenSaver. It asks each once at
// start and then follows its ActiveChanged signal; an answer that arrives after a signal from the same
// service is stale and ignored. Locked while either service says so; without either it stays unlocked.
class ScreenLock : public QObject {
    Q_OBJECT
public:
    explicit ScreenLock(QObject *parent = nullptr);
    bool locked() const { return active_[0] || active_[1]; }
private slots:
    void freedesktopChanged(bool active) { signalled_[0] = true; active_[0] = active; }
    void gnomeChanged(bool active) { signalled_[1] = true; active_[1] = active; }
private:
    bool active_[2] = {false, false}, signalled_[2] = {false, false}; // Per service, in constructor order.
};
}

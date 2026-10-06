#pragma once
#include <QObject>

namespace pet::platform {
// Whether the session's screen is locked (the screen saver is active), from the desktop's D-Bus screen
// saver: org.freedesktop.ScreenSaver (KDE and others) and org.gnome.ScreenSaver. It asks once at start
// and then follows their ActiveChanged signals. Without either service it stays unlocked.
class ScreenLock : public QObject {
    Q_OBJECT
public:
    explicit ScreenLock(QObject *parent = nullptr);
    bool locked() const { return locked_; }
private slots:
    void changed(bool active) { locked_ = active; }
private:
    bool locked_ = false;
};
}

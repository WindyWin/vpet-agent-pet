#pragma once
#include <QString>
#include <QtGlobal>

namespace pet {
// What the pet does once no sessions remain (preference "when_idle").
enum class IdlePolicy { Keep, Hide, Quit };
QString idlePolicyName(IdlePolicy policy);           // "keep", "hide", "quit"
bool parseIdlePolicy(const QString &name, IdlePolicy &policy);

// Decides when the pet window is shown, hidden or quit. It holds no widgets: the
// window applies the returned action, so the rules are testable headlessly.
class Presence {
public:
    enum class Visibility { Shown, UserHidden, AutoHidden };
    enum class Action { None, Show, Hide, Quit };
    static constexpr qint64 idleGraceMs = 2 * 60 * 1000;
    // Hiding needs a tray icon to bring the pet back; without one the pet stays visible.
    void setTrayAvailable(bool available) { tray_ = available; }
    bool canHide() const { return tray_; }
    void setPolicy(IdlePolicy policy) { policy_ = policy; }
    IdlePolicy policy() const { return policy_; }
    // Every launch starts shown, so the pet cannot get stuck invisible.
    Visibility visibility() const { return visibility_; }
    bool hidden() const { return visibility_ != Visibility::Shown; }
    // Tray click or menu. A user-hidden pet stays hidden until shown again.
    Action setUserHidden(bool hidden);
    // Called with the number of tracked sessions on every monitor update.
    Action update(int sessions, qint64 now);
    qint64 idleDeadline() const { return deadline_; } // 0 when not armed.
private:
    Visibility visibility_ = Visibility::Shown;
    IdlePolicy policy_ = IdlePolicy::Keep;
    qint64 deadline_ = 0;
    bool tray_ = false, observed_ = false, idle_ = false;
};
}

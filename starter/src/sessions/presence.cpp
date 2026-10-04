#include "presence.h"

namespace pet {
QString idlePolicyName(IdlePolicy policy) {
    return policy == IdlePolicy::Hide ? "hide" : policy == IdlePolicy::Quit ? "quit" : "keep";
}
bool parseIdlePolicy(const QString &name, IdlePolicy &policy) {
    if (name == "keep") policy = IdlePolicy::Keep;
    else if (name == "hide") policy = IdlePolicy::Hide;
    else if (name == "quit") policy = IdlePolicy::Quit;
    else return false;
    return true;
}
Presence::Action Presence::setUserHidden(bool hidden) {
    if (!hidden) {
        if (visibility_ == Visibility::Shown) return Action::None;
        visibility_ = Visibility::Shown; return Action::Show;
    }
    if (!tray_ || visibility_ == Visibility::UserHidden) return Action::None;
    const bool wasShown = visibility_ == Visibility::Shown;
    visibility_ = Visibility::UserHidden; // An auto-hidden pet now waits for the user, not a session.
    return wasShown ? Action::Hide : Action::None;
}
Presence::Action Presence::update(int sessions, qint64 now) {
    if (sessions > 0) {
        observed_ = true; idle_ = false; deadline_ = 0;
        if (visibility_ != Visibility::AutoHidden) return Action::None;
        visibility_ = Visibility::Shown; return Action::Show;
    }
    // Only a pet that has seen a session goes idle: a freshly started one waits for work.
    if (!observed_ || idle_) return Action::None;
    if (!deadline_) { deadline_ = now + idleGraceMs; return Action::None; }
    if (now < deadline_) return Action::None;
    deadline_ = 0; idle_ = true;
    if (policy_ == IdlePolicy::Quit) return Action::Quit;
    if (policy_ == IdlePolicy::Hide && tray_ && visibility_ == Visibility::Shown) {
        visibility_ = Visibility::AutoHidden; return Action::Hide;
    }
    return Action::None;
}
}

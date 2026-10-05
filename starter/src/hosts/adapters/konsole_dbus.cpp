#include "konsole.h"
#include <QDBusInterface>

namespace pet::hosts::konsole {
namespace {
class KonsoleActivation : public Activation {
public:
    QString id() const override { return konsole::id; }
    Outcome select(const HostContext &host) override {
        Target target;
        if (!decode(host.target, target)) return Outcome::MissingTarget;
        QDBusInterface window(target.service, target.window, "org.kde.konsole.Window", QDBusConnection::sessionBus());
        window.setTimeout(1000);
        if (!window.isValid()) return Outcome::TargetNotFound;
        return window.call("setCurrentSession", target.session).type() != QDBusMessage::ErrorMessage ? Outcome::Confirmed
                                                                                                       : Outcome::Failed;
    }
};
}
std::unique_ptr<Activation> activation() { return std::make_unique<KonsoleActivation>(); }
}

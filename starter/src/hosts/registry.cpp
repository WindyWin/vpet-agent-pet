#include "registry.h"
#include "adapters/generic.h"
#include "adapters/herdr.h"
#include "adapters/konsole.h"
#include "adapters/pattern.h"
#include "adapters/tmux.h"
#include <algorithm>

namespace pet::hosts {
void Registry::add(Capture capture) { captures_.append(std::move(capture)); }
const Capture *Registry::find(const QString &id) const {
    for (const auto &capture : captures_) if (capture.id == id) return &capture;
    return nullptr;
}
QStringList Registry::ids() const {
    QStringList ids;
    for (const auto &capture : captures_) ids << capture.id;
    return ids;
}
QString Registry::label(const QString &id) const {
    const auto *capture = find(id);
    return capture ? capture->label : QString();
}
bool Registry::validTarget(const HostContext &context) const {
    const auto *capture = find(context.adapter);
    return capture && (!capture->validTarget || capture->validTarget(context.target));
}
HostContext Registry::capture(const QProcessEnvironment &env, const QVector<qint64> &ancestors, const QStringList &names) const {
    const auto under = [&](const QString &program) {
        return program.isEmpty() || names.isEmpty() || std::any_of(names.begin(), names.end(), [&](const QString &name) {
            return name == program || name.startsWith(program + ":");
        });
    };
    HostContext context;
    const Capture *owner = nullptr;
    for (const auto &capture : captures_) {
        QString target;
        if (!capture.detect || !under(capture.program) || !capture.detect(env, ancestors, target)) continue;
        owner = &capture;
        context.adapter = capture.id;
        // An oversized target is dropped; the host and its other hints still identify the window.
        if (target.size() <= maxTarget) context.target = target;
        break;
    }
    if (context.isNull()) return context;
    context.pids = ancestors.mid(0, maxPids);
    // Terminals on X11 export their window; it is the most precise hint there is.
    if (owner->windowId && fullMatch("[1-9][0-9]{0,19}", env.value("WINDOWID"))) context.window = {x11Backend, env.value("WINDOWID")};
    return context;
}
bool Registry::validV1(const QString &host, const QString &pids, const QString &window) const {
    static const QRegularExpression pidList("^[1-9][0-9]{0,9}(,[1-9][0-9]{0,9}){0,15}$"), x11Window("^[1-9][0-9]{0,19}$");
    return (host.isEmpty() || find(host)) && (pids.isEmpty() || pidList.match(pids).hasMatch()) &&
           (window.isEmpty() || x11Window.match(window).hasMatch());
}
const Registry &Registry::builtin() {
    static const Registry registry = [] {
        Registry r;
        r.add(herdr::capture());
        r.add(tmux::capture());
        r.add(konsole::capture());
        r.add(vscode::capture());
        r.add(terminal::capture());
        return r;
    }();
    return registry;
}
}

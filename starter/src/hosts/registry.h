#pragma once
#include "context.h"
#include <QProcessEnvironment>
#include <functional>

namespace pet::hosts {
// The headless half of a host adapter: it recognizes the host in a hook's
// environment, validates its target and names it. Selection and window
// activation live in focus_service.h, so capture links without a display,
// D-Bus or X11.
struct Capture {
    QString id;    // Protocol v1 `host` value.
    QString label; // Shown in the running-sessions list.
    // True when this host owns the environment; fills its target, possibly empty.
    std::function<bool(const QProcessEnvironment &, const QVector<qint64> &ancestors, QString &target)> detect;
    // Decodes the target before any side effect. Unset: the host has no target.
    std::function<bool(const QString &target)> validTarget;
    // Programs started from a terminal inherit its variables (VS Code launched from a
    // herdr pane hands HERDR_PANE_ID to its own terminals), so a host with a program
    // name is detected only when an ancestor has that kernel name (or "<name>:...",
    // as tmux renames itself "tmux: server"). Empty: no such check.
    QString program;
    // Whether $WINDOWID belongs to this host. VS Code sets none; one in its terminals
    // belongs to the terminal that launched it.
    bool windowId = true;
};

// Built-in host adapters, registered explicitly in registry.cpp. A new host adds
// its Capture there and its Activation, if any, in platform::createFocusService();
// session state, alert policy and presentation stay untouched.
class Registry {
public:
    // Registration order is detection precedence: the first host that recognizes
    // an environment owns it, so nested multiplexers come before terminals.
    void add(Capture capture);
    const Capture *find(const QString &id) const;
    QStringList ids() const;
    QString label(const QString &id) const;
    bool validTarget(const HostContext &context) const;
    // Detects the host of a hook from its environment, its ancestors (nearest first) and
    // their kernel names. Without names, as when /proc is unreadable, inherited variables
    // are taken as they are. $WINDOWID becomes an X11 window reference. Null when no
    // host is recognized.
    HostContext capture(const QProcessEnvironment &environment, const QVector<qint64> &ancestors,
                        const QStringList &names = {}) const;
    // Validates v1 wire fields: a registered host and well-formed process and window hints.
    bool validV1(const QString &host, const QString &pids, const QString &window) const;
    // konsole, herdr, tmux, vscode and terminal; detected as herdr → tmux → Konsole → VS Code → terminal.
    static const Registry &builtin();
private:
    QVector<Capture> captures_;
};
}

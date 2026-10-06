#pragma once
#include "registry.h"
#include "platform/contracts/desktop.h"
#include <map>
#include <memory>
#include <vector>

namespace pet::hosts {
using platform::ActiveState;
using platform::Outcome;

// The activation half of a host adapter: selects the session's tab or pane and
// names the windows that show it. Registered with a FocusService by adapter ID.
class Activation {
public:
    virtual ~Activation() = default;
    virtual QString id() const = 0;
    // Selects the session's tab or pane; the service checks the target's codec first.
    virtual Outcome select(const HostContext &host) = 0;
    // Whether the window is still raised after selection failed, timed out or was
    // unavailable. Konsole tries anyway; multiplexers stop.
    virtual bool raiseAfterFailedSelection() const { return true; }
    // Windows to raise, in order; the first one a backend raises wins. A window that
    // names another adapter is selected through that adapter first (for example
    // the Konsole tab running a herdr client). Default: the session's own hints.
    virtual QVector<HostContext> windows(const HostContext &host) { return {host}; }
    // Whether its host window being active means the session is in view. Not for
    // multiplexers, whose window may show another pane.
    virtual bool windowShowsSession() const { return true; }
};

// Finds the real host of a session whose hook could not see it. Codex runs hooks
// from a shared daemon outside the terminal, so the hook only captured a generic
// terminal; a locator names the multiplexer pane from the provider and project.
class Locator {
public:
    virtual ~Locator() = default;
    // The host to use instead, or a null context to keep the captured one.
    virtual HostContext locate(const HostContext &host, const QString &provider, const QString &project) = 0;
};

// Selection and activation are reported separately: a selected tab in a window
// that could not be raised is not focus.
struct FocusResult {
    Outcome selection = Outcome::Skipped;
    Outcome activation = Outcome::Skipped;
    QString backend;          // The backend that raised the window.
    QStringList requirements; // What unavailable backends need, for guidance.
    // Compatibility with the former boolean: the window was raised.
    bool raised() const { return platform::succeeded(activation); }
};

// "Open" for a session: return to the application that hosts it. It never starts
// a terminal or an agent.
//   1. Copy the host context (the caller copies the session).
//   2. Resolve the adapter and check its target before any side effect.
//   3. Select its tab or pane; a multiplexer stops here when that fails.
//   4. Raise each window the adapter names through each backend in order.
class FocusService {
public:
    explicit FocusService(const Registry &registry = Registry::builtin()) : registry_(registry) {}
    void addActivation(std::unique_ptr<Activation> activation);
    // Backends are tried in registration order.
    void addBackend(std::unique_ptr<platform::DesktopBackend> backend);
    // Locators are tried in order, and only for a host that is a bare terminal.
    void addLocator(std::unique_ptr<Locator> locator);
    FocusResult focus(HostContext host, const QString &project, const QString &provider = {});
    // Active only when a backend sees the host's window active and that shows the session.
    ActiveState active(const HostContext &host, const QString &project);
private:
    Activation *find(const QString &id) const;
    const Registry &registry_;
    std::map<QString, std::unique_ptr<Activation>> activations_;
    std::vector<std::unique_ptr<platform::DesktopBackend>> backends_;
    std::vector<std::unique_ptr<Locator>> locators_;
};
}

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

// Who the focus request is for: the provider's own session, in its project. A
// locator matches on these; the captured host only hints where the session was.
struct FocusSubject {
    QString provider, project, session;
};

// How the host to select was established. Only `Captured` and `Resolved` go on to
// select and raise; the others stop before any side effect, so a known-wrong or
// ambiguous identity never falls through to inherited hints or the focused pane.
enum class Resolution {
    Captured,    // No locator applied: the host the hook captured is used as it is.
    Resolved,    // A locator verified the captured host or found the session's current one.
    Unavailable, // The host could not be asked, and the captured host cannot be trusted alone.
    Stale,       // The captured host is known to belong to another session or to be gone.
    Ambiguous,   // Several hosts could be the session's and nothing tells them apart.
};
inline bool proceeds(Resolution resolution) { return resolution == Resolution::Captured || resolution == Resolution::Resolved; }

// A locator's answer: `Captured` keeps the host (the locator does not apply, or
// could not check and the captured host stays trustworthy); `Resolved` replaces it
// with `host`; anything else stops the request.
struct Located {
    Resolution resolution = Resolution::Captured;
    HostContext host;
};

// Verifies or finds the real host of a session at Open time. Codex runs hooks from
// a shared daemon outside the terminal, which may carry the environment of the
// pane that started it: the captured host is then a bare terminal or another
// session's pane. A locator asks the host which pane runs the session now.
class Locator {
public:
    virtual ~Locator() = default;
    virtual Located locate(const HostContext &host, const FocusSubject &subject) = 0;
};

// Selection and activation are reported separately: a selected tab in a window
// that could not be raised is not focus.
struct FocusResult {
    Outcome selection = Outcome::Skipped;
    Outcome activation = Outcome::Skipped;
    QString backend;          // The backend that raised the window.
    QStringList requirements; // What unavailable backends need, for guidance.
    Resolution resolution = Resolution::Captured; // Anything but Captured or Resolved selected nothing.
    // Compatibility with the former boolean: the window was raised.
    bool raised() const { return platform::succeeded(activation); }
};

// "Open" for a session: return to the application that hosts it. It never starts
// a terminal or an agent.
//   1. Copy the host context (the caller copies the session).
//   2. Let the locators verify or replace it; stop when they report it stale,
//      ambiguous or unverifiable.
//   3. Resolve the adapter and check its target before any side effect.
//   4. Select its tab or pane; a multiplexer stops here when that fails.
//   5. Raise each window the adapter names through each backend in order.
class FocusService {
public:
    explicit FocusService(const Registry &registry = Registry::builtin()) : registry_(registry) {}
    void addActivation(std::unique_ptr<Activation> activation);
    // Backends are tried in registration order.
    void addBackend(std::unique_ptr<platform::DesktopBackend> backend);
    // Locators are tried in order; the first that does not answer Captured decides.
    void addLocator(std::unique_ptr<Locator> locator);
    FocusResult focus(HostContext host, const QString &project, const QString &provider = {}, const QString &session = {});
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

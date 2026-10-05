#include "hosts/adapters/generic.h"
#include "hosts/adapters/herdr.h"
#include "hosts/adapters/konsole.h"
#include "hosts/adapters/tmux.h"
#include "hosts/focus_service.h"
#include "platform/linux/commands.h"
#include "platform/unsupported/unsupported.h"
#include <QElapsedTimer>
#include <QTest>

// Session focus contracts with fake adapters, backends and services: no desktop,
// D-Bus session or installed terminal is needed.
using pet::hosts::FocusService;
using pet::hosts::HostContext;
using pet::platform::ActiveState;
using pet::platform::Outcome;
using pet::platform::WindowRequest;

namespace {
struct Backend : pet::platform::DesktopBackend {
    QString name, need;
    Outcome outcome = Outcome::TargetNotFound;
    ActiveState state = ActiveState::Unknown;
    QVector<WindowRequest> requests;
    QStringList *log = nullptr;
    Backend(QString name, Outcome outcome, QString need = {}) : name(std::move(name)), need(std::move(need)), outcome(outcome) {}
    QString id() const override { return name; }
    QString requirement() const override { return need; }
    Outcome activate(const WindowRequest &request) override {
        requests.append(request);
        if (log) *log << name + ":" + pet::hosts::joinPids(request.pids);
        return outcome;
    }
    ActiveState active(const WindowRequest &request) override { requests.append(request); return state; }
};
struct Selector : pet::hosts::Activation {
    QString name;
    Outcome outcome = Outcome::Confirmed;
    bool raiseAnyway = true;
    QStringList selected;
    QStringList *log = nullptr;
    explicit Selector(QString name) : name(std::move(name)) {}
    QString id() const override { return name; }
    Outcome select(const HostContext &host) override {
        selected << host.target;
        if (log) *log << name + " select " + host.target;
        return outcome;
    }
    bool raiseAfterFailedSelection() const override { return raiseAnyway; }
};
struct Runner : pet::platform::CommandRunner {
    QVector<pet::platform::Command> commands;
    QMap<QString, Outcome> outcomes; // By first non-socket argument.
    QByteArray clients;
    QStringList *log = nullptr;
    Outcome run(const pet::platform::Command &command, QByteArray *output) override {
        commands.append(command);
        const auto verb = command.arguments.value(command.arguments.indexOf("-S") == 0 ? 2 : 0);
        if (log) *log << command.program + " " + verb;
        if (output) *output = clients;
        return outcomes.value(verb, Outcome::Confirmed);
    }
};
struct Processes : pet::platform::ProcessServices {
    QMap<qint64, QVector<qint64>> parents;
    QVector<pet::platform::ProcessInfo> clients;
    QVector<qint64> ancestors(qint64 pid, int) const override { return parents.value(pid); }
    QVector<pet::platform::ProcessInfo> terminalClients(const QString &executable) const override {
        return executable == "herdr" ? clients : QVector<pet::platform::ProcessInfo>{};
    }
};
// A service with two fake backends, standing in for X11 then KWin.
struct Fixture {
    FocusService service;
    Backend *first, *second;
    explicit Fixture(Outcome a = Outcome::TargetNotFound, Outcome b = Outcome::Unsupported) {
        auto x = std::make_unique<Backend>("x11", a);
        auto k = std::make_unique<Backend>("kwin", b, "Wayland focus requires KDE Plasma 6.");
        first = x.get(); second = k.get();
        service.addBackend(std::move(x)); service.addBackend(std::move(k));
    }
};
}

class FocusTests : public QObject {
    Q_OBJECT
private slots:
    void backendFallbackAndRequests() {
        // X11 cannot find the window; KWin raises it and confirms.
        Fixture f(Outcome::TargetNotFound, Outcome::Confirmed);
        const HostContext host{"terminal", {30, 20}, {"x11", "77"}, {}};
        auto result = f.service.focus(host, "/work/web");
        QVERIFY(result.raised()); QCOMPARE(result.activation, Outcome::Confirmed); QCOMPARE(result.backend, QString("kwin"));
        QCOMPARE(result.selection, Outcome::Unsupported); // A generic terminal has no tab selection.
        QCOMPARE(f.first->requests.size(), 1); QCOMPARE(f.second->requests.size(), 1);
        QCOMPARE(f.first->requests[0].window, (pet::platform::WindowRef{"x11", "77"}));
        QCOMPARE(f.first->requests[0].pids, (QVector<qint64>{30, 20})); QCOMPARE(f.first->requests[0].project, QString("/work/web"));
        // A request X11 sends is Requested, and KWin is not consulted.
        Fixture x11(Outcome::Requested, Outcome::Confirmed);
        result = x11.service.focus(host, "/work/web");
        QCOMPARE(result.activation, Outcome::Requested); QCOMPARE(result.backend, QString("x11"));
        QVERIFY(x11.second->requests.isEmpty());
    }
    void activationFailures() {
        const HostContext host{"vscode", {30}, {}, {}};
        // Every backend unavailable: Unsupported, with what the user would need.
        Fixture none(Outcome::Unsupported, Outcome::Unsupported);
        auto result = none.service.focus(host, {});
        QVERIFY(!result.raised()); QCOMPARE(result.activation, Outcome::Unsupported);
        QCOMPARE(result.requirements, QStringList{"Wayland focus requires KDE Plasma 6."});
        // The most telling failure is reported; available backends' requirements are not.
        Fixture failed(Outcome::TargetNotFound, Outcome::Failed);
        result = failed.service.focus(host, {});
        QCOMPARE(result.activation, Outcome::Failed); QVERIFY(result.requirements.isEmpty());
        Fixture timeout(Outcome::TargetNotFound, Outcome::TimedOut);
        QCOMPARE(timeout.service.focus(host, {}).activation, Outcome::TimedOut);
        // No backend registered at all.
        FocusService empty;
        QCOMPARE(empty.focus(host, {}).activation, Outcome::Unsupported);
        // The explicit unsupported backend reports what it was given.
        FocusService unsupported;
        unsupported.addBackend(std::make_unique<pet::platform::unsupported::Desktop>("test", "Needs a test desktop."));
        result = unsupported.focus(host, {});
        QCOMPARE(result.activation, Outcome::Unsupported); QCOMPARE(result.requirements, QStringList{"Needs a test desktop."});
        QCOMPARE(unsupported.active(host, {}), ActiveState::Unknown);
        // No host, or a host this build does not know: nothing is attempted.
        Fixture f;
        QCOMPARE(f.service.focus({}, {}).activation, Outcome::MissingTarget);
        result = f.service.focus({"xterm", {30}, {}, {}}, {});
        QCOMPARE(result.selection, Outcome::Unsupported); QCOMPARE(result.activation, Outcome::Unsupported);
        QVERIFY(f.first->requests.isEmpty());
    }
    void selectionPolicies() {
        const QString konsoleTarget = "org.kde.konsole-1|/Windows/1|/Sessions/2";
        // Konsole: a failed tab selection still raises the window.
        Fixture f(Outcome::Requested);
        auto konsole = std::make_unique<Selector>("konsole");
        auto *selector = konsole.get(); selector->outcome = Outcome::Failed;
        f.service.addActivation(std::move(konsole));
        auto result = f.service.focus({"konsole", {30}, {}, konsoleTarget}, {});
        QCOMPARE(selector->selected, QStringList{konsoleTarget});
        QCOMPARE(result.selection, Outcome::Failed); QVERIFY(result.raised());
        // Selection-only success: the tab was selected, but no window was raised.
        selector->outcome = Outcome::Confirmed; f.first->outcome = Outcome::TargetNotFound;
        result = f.service.focus({"konsole", {30}, {}, konsoleTarget}, {});
        QCOMPARE(result.selection, Outcome::Confirmed); QVERIFY(!result.raised());
        QCOMPARE(result.activation, Outcome::TargetNotFound);
        // A malformed target is never handed to the adapter; the window is still raised from its hints.
        selector->selected.clear(); f.first->outcome = Outcome::Requested;
        result = f.service.focus({"konsole", {30}, {}, "org.kde.konsole-1|/Windows/1"}, {});
        QVERIFY(selector->selected.isEmpty()); QCOMPARE(result.selection, Outcome::MissingTarget); QVERIFY(result.raised());
        // An adapter that stops on failure: no window is tried.
        selector->raiseAnyway = false;
        for (const auto outcome : {Outcome::Failed, Outcome::TimedOut, Outcome::Unsupported}) {
            selector->outcome = outcome; f.first->requests.clear();
            result = f.service.focus({"konsole", {30}, {}, konsoleTarget}, {});
            QCOMPARE(result.selection, outcome); QCOMPARE(result.activation, Outcome::Skipped);
            QVERIFY(f.first->requests.isEmpty());
        }
        // ... but a missing or unfound target is not a failure to stop for.
        selector->outcome = Outcome::TargetNotFound;
        QVERIFY(f.service.focus({"konsole", {30}, {}, konsoleTarget}, {}).raised());
    }
    void tmuxSelectionAndClients() {
        const auto runner = std::make_shared<Runner>();
        const auto processes = std::make_shared<Processes>();
        processes->parents = {{100, {100, 50}}, {200, {200}}};
        Fixture f(Outcome::Requested);
        f.service.addActivation(pet::hosts::tmux::activation(runner, processes));
        const HostContext pane{"tmux", {900, 800}, {"x11", "5"}, "/tmp/tmux-1/default|%7"};
        // Window, then pane; the detached server's clients lead to the terminal windows.
        runner->clients = "100\n200\n\n";
        auto result = f.service.focus(pane, "/work/api");
        QCOMPARE(result.selection, Outcome::Confirmed); QVERIFY(result.raised());
        QCOMPARE(runner->commands.size(), 3);
        QCOMPARE(runner->commands[0].arguments, (QStringList{"-S", "/tmp/tmux-1/default", "select-window", "-t", "%7"}));
        QCOMPARE(runner->commands[1].arguments, (QStringList{"-S", "/tmp/tmux-1/default", "select-pane", "-t", "%7"}));
        QCOMPARE(runner->commands[2].arguments,
                 (QStringList{"-S", "/tmp/tmux-1/default", "list-clients", "-t", "%7", "-F", "#{client_pid}"}));
        QCOMPARE(f.first->requests[0].pids, (QVector<qint64>{100, 50, 200, 900, 800}));
        QCOMPARE(f.first->requests[0].window, pane.window);
        // A failed selection stops before later commands and any window.
        runner->commands.clear(); f.first->requests.clear();
        runner->outcomes["select-window"] = Outcome::Failed;
        result = f.service.focus(pane, {});
        QCOMPARE(runner->commands.size(), 1); QCOMPARE(result.selection, Outcome::Failed);
        QCOMPARE(result.activation, Outcome::Skipped); QVERIFY(f.first->requests.isEmpty());
        // tmux not installed stops too.
        runner->outcomes["select-window"] = Outcome::Unsupported;
        QCOMPARE(f.service.focus(pane, {}).activation, Outcome::Skipped);
        // Without clients, the pane's own hints remain.
        runner->outcomes.clear(); runner->commands.clear(); runner->clients.clear();
        runner->outcomes["list-clients"] = Outcome::Failed;
        QVERIFY(f.service.focus(pane, {}).raised()); QCOMPARE(f.first->requests.last().pids, pane.pids);
        // A malformed target runs nothing and is still raised from its hints.
        runner->commands.clear();
        result = f.service.focus({"tmux", {900}, {}, "/tmp/s|%7; rm"}, {});
        QVERIFY(runner->commands.isEmpty()); QCOMPARE(result.selection, Outcome::MissingTarget); QVERIFY(result.raised());
        QCOMPARE(f.first->requests.last().pids, QVector<qint64>{900});
    }
    void herdrClientsFirst() {
        QStringList log;
        const auto runner = std::make_shared<Runner>(); runner->log = &log;
        const auto processes = std::make_shared<Processes>();
        auto client = [](qint64 pid, QString socket, bool konsole) {
            QProcessEnvironment env; env.insert("HERDR_SOCKET_PATH", socket);
            if (konsole) {
                env.insert("KONSOLE_DBUS_SERVICE", "org.kde.konsole-9"); env.insert("KONSOLE_DBUS_WINDOW", "/Windows/1");
                env.insert("KONSOLE_DBUS_SESSION", QString("/Sessions/%1").arg(pid));
            }
            return pet::platform::ProcessInfo{pid, {}, env};
        };
        processes->clients = {client(40, "/run/other.sock", true), client(41, "/run/h.sock", true), client(42, "/run/./h.sock", false)};
        processes->parents = {{41, {41, 4}}, {42, {42, 5}}};
        Fixture f(Outcome::TargetNotFound, Outcome::Failed);
        f.first->log = &log; f.second->log = &log;
        auto konsole = std::make_unique<Selector>("konsole"); konsole->log = &log;
        f.service.addActivation(std::move(konsole));
        f.service.addActivation(pet::hosts::herdr::activation(runner, processes));
        const HostContext pane{"herdr", {700}, {}, "t_2|p_3|/run/h.sock"};
        auto result = f.service.focus(pane, {});
        // Tab and pane first; then each client of this socket in its Konsole tab, then the pane's own hints.
        QCOMPARE(log, (QStringList{"herdr tab", "herdr agent",
                                   "konsole select org.kde.konsole-9|/Windows/1|/Sessions/41", "x11:41,4", "kwin:41,4",
                                   "x11:42,5", "kwin:42,5",
                                   "x11:700", "kwin:700"}));
        QCOMPARE(runner->commands[0].environment.value("HERDR_SOCKET_PATH"), QString("/run/h.sock"));
        QCOMPARE(result.activation, Outcome::Failed); QVERIFY(!result.raised());
        // The first client raised wins.
        log.clear(); f.second->outcome = Outcome::Confirmed;
        result = f.service.focus(pane, {});
        QVERIFY(result.raised()); QCOMPARE(log.last(), QString("kwin:41,4"));
        // A target without a socket cannot identify its clients: only the pane's own hints.
        log.clear(); f.second->outcome = Outcome::Failed;
        f.service.focus({"herdr", {700}, {}, "|p_3|"}, {});
        QCOMPARE(log, (QStringList{"herdr agent", "x11:700", "kwin:700"}));
    }
    void activeObservation() {
        Fixture f;
        f.first->state = ActiveState::Unknown; f.second->state = ActiveState::Active;
        // A backend that cannot observe defers to the next.
        QCOMPARE(f.service.active({"vscode", {30}, {}, {}}, "/work/web"), ActiveState::Active);
        QCOMPARE(f.first->requests.last().project, QString("/work/web"));
        f.first->state = ActiveState::Inactive;
        QCOMPARE(f.service.active({"vscode", {30}, {}, {}}, {}), ActiveState::Inactive);
        f.first->state = f.second->state = ActiveState::Unknown;
        QCOMPARE(f.service.active({"terminal", {30}, {}, {}}, {}), ActiveState::Unknown);
        // A multiplexer's window may show another pane: never evidence the session is in view.
        f.first->state = ActiveState::Active;
        const auto runner = std::make_shared<Runner>(); const auto processes = std::make_shared<Processes>();
        f.service.addActivation(pet::hosts::tmux::activation(runner, processes));
        f.service.addActivation(pet::hosts::herdr::activation(runner, processes));
        QCOMPARE(f.service.active({"tmux", {30}, {}, "/s|%1"}, {}), ActiveState::Unknown);
        QCOMPARE(f.service.active({"herdr", {30}, {}, "|p|"}, {}), ActiveState::Unknown);
        QVERIFY(runner->commands.isEmpty()); // Observation never selects anything.
        QCOMPARE(f.service.active({}, {}), ActiveState::Unknown);
        QCOMPARE(f.service.active({"konsole", {30}, {}, {}}, {}), ActiveState::Active);
    }
    void testAdapterRegistration() {
        // A new host registers its capture and activation; the focus service needs no change.
        pet::hosts::Registry registry;
        registry.add({"test-term", "Test Term",
                      [](const QProcessEnvironment &env, const QVector<qint64> &, QString &target) {
                          target = env.value("TEST_TERM_TAB"); return !target.isEmpty();
                      },
                      [](const QString &target) { return target.startsWith("tab-"); }});
        FocusService service(registry);
        auto adapter = std::make_unique<Selector>("test-term");
        auto *selector = adapter.get();
        service.addActivation(std::move(adapter));
        service.addBackend(std::make_unique<Backend>("test-desktop", Outcome::Confirmed));
        QProcessEnvironment env; env.insert("TEST_TERM_TAB", "tab-4");
        const auto result = service.focus(registry.capture(env, {12}), {});
        QCOMPARE(selector->selected, QStringList{"tab-4"}); QVERIFY(result.raised());
        QCOMPARE(result.backend, QString("test-desktop"));
        // Built-in hosts are unknown to this registry.
        QCOMPARE(service.focus({"konsole", {12}, {}, {}}, {}).activation, Outcome::Unsupported);
    }
    void linuxCommandRunner() {
        pet::platform::LinuxCommandRunner runner;
        QCOMPARE(runner.run({"agent-pet-no-such-program", {}, {}}), Outcome::Unsupported);
        QCOMPARE(runner.run({"true", {}, {}}), Outcome::Confirmed);
        QCOMPARE(runner.run({"false", {}, {}}), Outcome::Failed);
        QByteArray output;
        QCOMPARE(runner.run({"sh", {"-c", "printf '%s' \"$AGENT_PET_TEST\""}, {{"AGENT_PET_TEST", "41\n42"}}}, &output),
                 Outcome::Confirmed);
        QCOMPARE(output, QByteArray("41\n42"));
        QElapsedTimer elapsed; elapsed.start();
        QCOMPARE(runner.run({"sleep", {"10"}, {}}), Outcome::TimedOut);
        QVERIFY(elapsed.elapsed() < 5000);
    }
};
QTEST_GUILESS_MAIN(FocusTests)
#include "focus_tests.moc"

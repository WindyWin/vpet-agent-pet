#include "generic.h"
#include "herdr.h"
#include "konsole.h"
#include "multiplexer.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace pet::hosts::herdr {
namespace {
class HerdrActivation : public Activation {
public:
    HerdrActivation(std::shared_ptr<platform::CommandRunner> commands, std::shared_ptr<const platform::ProcessServices> processes,
                    const Registry &registry)
        : commands_(std::move(commands)), processes_(std::move(processes)), registry_(registry) {}
    QString id() const override { return herdr::id; }
    Outcome select(const HostContext &host) override {
        Target target;
        return decode(host.target, target) ? runSelection(*commands_, selectCommands(target)) : Outcome::MissingTarget;
    }
    bool raiseAfterFailedSelection() const override { return false; }
    bool windowShowsSession() const override { return false; }
    QVector<HostContext> windows(const HostContext &host) override {
        QVector<HostContext> windows;
        Target target;
        if (decode(host.target, target) && !target.socket.isEmpty()) {
            // Clients are matched by their current metadata, so this also works after a reattach.
            for (const auto &client : processes_->terminalClients("herdr")) {
                if (clientSocket(client.environment, client.arguments) != QDir::cleanPath(target.socket)) continue;
                const auto ancestors = processes_->ancestors(client.pid);
                auto window = registry_.capture(client.environment, ancestors, processes_->names(ancestors));
                const bool inKonsole = !client.environment.value("KONSOLE_DBUS_SERVICE").isEmpty();
                window.adapter = inKonsole ? konsole::id : terminal::id;
                window.target = inKonsole ? konsole::targetOf(client.environment) : QString();
                windows.append(window);
            }
        }
        windows.append(host);
        return windows;
    }
private:
    std::shared_ptr<platform::CommandRunner> commands_;
    std::shared_ptr<const platform::ProcessServices> processes_;
    const Registry &registry_;
};
// One entry of `herdr agent list`.
struct Agent {
    QString provider, cwd, tab, pane, session;
};
// The provider's own session ID, when herdr reports one: a string, or an object naming it.
QString agentSession(const QJsonValue &value) {
    if (value.isString()) return value.toString();
    const auto object = value.toObject();
    for (const auto *key : {"id", "session_id"})
        if (object.value(key).isString()) return object.value(key).toString();
    return {};
}
class HerdrLocator : public Locator {
public:
    HerdrLocator(std::shared_ptr<platform::CommandRunner> commands, QString socket, QStringList sharedDaemonProviders)
        : commands_(std::move(commands)), socket_(std::move(socket)), sharedDaemon_(std::move(sharedDaemonProviders)) {}
    Located locate(const HostContext &host, const FocusSubject &subject) override {
        const bool captured = host.adapter == id;
        if ((!captured && host.adapter != terminal::id) || subject.provider.isEmpty()) return {};
        Target target;
        const bool decoded = captured && decode(host.target, target);
        // A pane captured from a shared daemon's environment names whichever pane started the daemon.
        const bool trusted = !sharedDaemon_.contains(subject.provider);
        if (captured && !decoded) return {}; // Malformed: the service reports MissingTarget as before.
        // The captured server is asked about its own panes; a bare terminal asks the pet's own server.
        const auto socket = decoded && !target.socket.isEmpty() ? QDir::cleanPath(target.socket) : socket_;
        QVector<Agent> agents;
        if (!list(socket, agents))
            // Without the server's word, only a pane captured inside it is trusted; a bare terminal stays as it is.
            return captured && !trusted ? Located{Resolution::Unavailable, {}} : Located{};
        // A session herdr names natively is authoritative, wherever the hook ran.
        if (!subject.session.isEmpty()) {
            QVector<Agent> named;
            for (const auto &agent : agents)
                if (agent.provider == subject.provider && agent.session == subject.session) named.append(agent);
            if (named.size() == 1) return resolved(named.first(), socket, host, captured && trusted && named.first().pane == target.pane);
            if (named.size() > 1) return {Resolution::Ambiguous, {}}; // Duplicate registrations: guess nothing.
        }
        // An agent that names another session, or runs another provider or project, is not this one.
        const auto mayBeOurs = [&](const Agent &agent) {
            return agent.provider == subject.provider && !subject.project.isEmpty() && agent.cwd == subject.project &&
                   (agent.session.isEmpty() || subject.session.isEmpty());
        };
        bool stale = false;
        if (captured) {
            const auto it = std::find_if(agents.begin(), agents.end(), [&](const Agent &agent) { return agent.pane == target.pane; });
            if (it == agents.end()) {
                // Herdr lists only the agents it detects: a pane captured inside it may just be unlisted.
                if (trusted) return {};
                stale = true;
            } else if (trusted) {
                // The hook ran in this pane, so only another session named there now makes it stale: its cwd
                // may have moved on, and herdr's agent names need not match provider IDs. Refresh its tab,
                // in case the pane moved.
                if (it->session.isEmpty() || subject.session.isEmpty() || it->session == subject.session)
                    return resolved(*it, socket, host, true);
                stale = true;
            } else if (!mayBeOurs(*it)) {
                stale = true; // Another session's pane, or a reused one.
            }
        }
        QVector<Agent> candidates;
        for (const auto &agent : agents)
            if (mayBeOurs(agent)) candidates.append(agent);
        // Focus never breaks a tie: an unfocused session is as likely to be the one asked for.
        if (candidates.size() > 1) return {Resolution::Ambiguous, {}};
        if (candidates.size() == 1) return resolved(candidates.first(), socket, host, false);
        if (stale) return {Resolution::Stale, {}};
        return {}; // A bare terminal whose session herdr does not run: keep it.
    }
private:
    bool list(const QString &socket, QVector<Agent> &agents) const {
        QMap<QString, QString> env;
        if (!socket.isEmpty()) env["HERDR_SOCKET_PATH"] = socket;
        QByteArray output;
        if (!platform::succeeded(commands_->run({"herdr", {"agent", "list"}, env}, &output))) return false;
        const auto result = QJsonDocument::fromJson(output).object().value("result").toObject();
        if (!result.value("agents").isArray()) return false;
        for (const auto &value : result.value("agents").toArray()) {
            const auto agent = value.toObject();
            agents.append({agent.value("agent").toString(), agent.value("cwd").toString(), agent.value("tab_id").toString(),
                           agent.value("pane_id").toString(), agentSession(agent.value("agent_session"))});
        }
        return true;
    }
    // The pane as herdr reports it now. The captured process hints stay only when the hook
    // ran in that very pane; a daemon's hints lead to the terminal that started it.
    static Located resolved(const Agent &agent, const QString &socket, const HostContext &host, bool keepHints) {
        HostContext located{id, keepHints ? host.pids : QVector<qint64>{}, keepHints ? host.window : platform::WindowRef{},
                            agent.tab + "|" + agent.pane + "|" + socket};
        Target decoded;
        if (!decode(located.target, decoded)) return {Resolution::Stale, {}};
        return {Resolution::Resolved, located};
    }
    std::shared_ptr<platform::CommandRunner> commands_;
    QString socket_;
    QStringList sharedDaemon_;
};
}
std::unique_ptr<Locator> locator(std::shared_ptr<platform::CommandRunner> commands, QString socket,
                                 QStringList sharedDaemonProviders) {
    return std::make_unique<HerdrLocator>(std::move(commands), std::move(socket), std::move(sharedDaemonProviders));
}
std::unique_ptr<Activation> activation(std::shared_ptr<platform::CommandRunner> commands,
                                       std::shared_ptr<const platform::ProcessServices> processes, const Registry &registry) {
    return std::make_unique<HerdrActivation>(std::move(commands), std::move(processes), registry);
}
}

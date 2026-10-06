#include "generic.h"
#include "herdr.h"
#include "konsole.h"
#include "multiplexer.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
class HerdrLocator : public Locator {
public:
    HerdrLocator(std::shared_ptr<platform::CommandRunner> commands, QString socket)
        : commands_(std::move(commands)), socket_(std::move(socket)) {}
    HostContext locate(const HostContext &host, const QString &provider, const QString &project) override {
        if (project.isEmpty()) return {};
        QMap<QString, QString> env;
        if (!socket_.isEmpty()) env["HERDR_SOCKET_PATH"] = socket_;
        QByteArray output;
        if (!platform::succeeded(commands_->run({"herdr", {"agent", "list"}, env}, &output))) return {};
        const auto agents = QJsonDocument::fromJson(output).object().value("result").toObject().value("agents").toArray();
        QJsonObject match;
        int found = 0, focused = 0;
        QJsonObject focusedMatch;
        for (const auto &value : agents) {
            const auto agent = value.toObject();
            if (agent.value("agent").toString() != provider || agent.value("cwd").toString() != project) continue;
            match = agent; ++found;
            if (agent.value("focused").toBool()) { focusedMatch = agent; ++focused; }
        }
        if (found > 1) { match = focusedMatch; found = focused; }
        if (found != 1) return {};
        HostContext located{id, host.pids, host.window, match.value("tab_id").toString() + "|" + match.value("pane_id").toString() + "|" + socket_};
        Target decoded;
        return decode(located.target, decoded) ? located : HostContext{};
    }
private:
    std::shared_ptr<platform::CommandRunner> commands_;
    QString socket_;
};
}
std::unique_ptr<Locator> locator(std::shared_ptr<platform::CommandRunner> commands, QString socket) {
    return std::make_unique<HerdrLocator>(std::move(commands), std::move(socket));
}
std::unique_ptr<Activation> activation(std::shared_ptr<platform::CommandRunner> commands,
                                       std::shared_ptr<const platform::ProcessServices> processes, const Registry &registry) {
    return std::make_unique<HerdrActivation>(std::move(commands), std::move(processes), registry);
}
}

#include "generic.h"
#include "herdr.h"
#include "konsole.h"
#include "multiplexer.h"
#include <QDir>

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
                auto window = registry_.capture(client.environment, processes_->ancestors(client.pid));
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
}
std::unique_ptr<Activation> activation(std::shared_ptr<platform::CommandRunner> commands,
                                       std::shared_ptr<const platform::ProcessServices> processes, const Registry &registry) {
    return std::make_unique<HerdrActivation>(std::move(commands), std::move(processes), registry);
}
}

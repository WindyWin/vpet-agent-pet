#include "multiplexer.h"
#include "tmux.h"

namespace pet::hosts::tmux {
namespace {
class TmuxActivation : public Activation {
public:
    TmuxActivation(std::shared_ptr<platform::CommandRunner> commands, std::shared_ptr<const platform::ProcessServices> processes)
        : commands_(std::move(commands)), processes_(std::move(processes)) {}
    QString id() const override { return tmux::id; }
    Outcome select(const HostContext &host) override {
        Target target;
        return decode(host.target, target) ? runSelection(*commands_, selectCommands(target)) : Outcome::MissingTarget;
    }
    bool raiseAfterFailedSelection() const override { return false; }
    bool windowShowsSession() const override { return false; }
    QVector<HostContext> windows(const HostContext &host) override {
        auto window = host;
        if (Target target; decode(host.target, target)) {
            QVector<qint64> clients;
            QByteArray output;
            if (platform::succeeded(commands_->run(listClients(target), &output)))
                for (const auto &line : output.split('\n')) clients += processes_->ancestors(line.trimmed().toLongLong());
            window.pids = clients + host.pids;
        }
        return {window};
    }
private:
    std::shared_ptr<platform::CommandRunner> commands_;
    std::shared_ptr<const platform::ProcessServices> processes_;
};
}
std::unique_ptr<Activation> activation(std::shared_ptr<platform::CommandRunner> commands,
                                       std::shared_ptr<const platform::ProcessServices> processes) {
    return std::make_unique<TmuxActivation>(std::move(commands), std::move(processes));
}
}

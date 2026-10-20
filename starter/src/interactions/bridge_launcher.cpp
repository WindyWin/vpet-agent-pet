#include "bridge_launcher.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace pet::interactions {
namespace {
constexpr int maxDelayMs = 60000;
}
BridgeLauncher::BridgeLauncher(QObject *parent) : QObject(parent) {}
BridgeLauncher::~BridgeLauncher() { stop(); }

QStringList BridgeLauncher::scripts() {
#ifdef Q_OS_WIN
    return {"opencode-bridge.mjs"}; // The Codex daemon's control channel on Windows is unverified.
#else
    return {"opencode-bridge.mjs", "codex-bridge.mjs"};
#endif
}
QString BridgeLauncher::bridgeFolder(const QString &applicationDir) {
    const QDir app(applicationDir);
    const QStringList candidates = {
        app.filePath("bridges"),                              // Windows install folder
        app.filePath("../share/agent-pet/bridges"),           // Linux release
        app.filePath("../Resources/bridges"),                 // macOS bundle
        app.filePath("../src/providers"),                     // A build tree beside the sources (starter/build)
    };
    for (const auto &candidate : candidates)
        if (QFileInfo::exists(QDir(candidate).filePath("opencode-bridge.mjs"))) return QDir::cleanPath(candidate);
    return {};
}
bool BridgeLauncher::running() const {
    for (const auto &[name, bridge] : bridges_)
        if (bridge->process && bridge->process->state() != QProcess::NotRunning) return true;
    return false;
}
bool BridgeLauncher::start() {
    if (!stopped_) return true;
    if (node.isEmpty()) node = QStandardPaths::findExecutable("node");
    if (folder.isEmpty()) folder = bridgeFolder(QCoreApplication::applicationDirPath());
    if (!node.isEmpty() && !QFileInfo(node).isExecutable()) node.clear();
    if (node.isEmpty()) { status_ = "Node 20 or newer is not installed; the OpenCode and Codex bridges are not started"; return false; }
    if (folder.isEmpty()) { status_ = "The bridge scripts were not found next to the application"; return false; }
    stopped_ = false; status_.clear();
    for (const auto &script : scripts()) {
        if (!QFileInfo::exists(QDir(folder).filePath(script))) continue;
        auto bridge = std::make_unique<Bridge>();
        bridge->script = script; bridge->delayMs = retryMs;
        bridge->retry = new QTimer(this); bridge->retry->setSingleShot(true);
        auto *raw = bridge.get();
        connect(bridge->retry, &QTimer::timeout, this, [this, raw] { launch(*raw); });
        bridges_[script] = std::move(bridge);
        launch(*raw);
    }
    return true;
}
void BridgeLauncher::launch(Bridge &bridge) {
    if (stopped_) return;
    bridge.process = std::make_unique<QProcess>();
    auto *process = bridge.process.get();
    QStringList arguments{QDir(folder).filePath(bridge.script)};
    if (!endpoint.isEmpty()) arguments << "--endpoint" << endpoint;
    process->setProgram(node); process->setArguments(arguments);
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel); // The bridges log to stderr, never message text.
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardOutputFile(QProcess::nullDevice());
    bridge.clock.start();
    connect(process, &QProcess::finished, this, [this, &bridge] { exited(bridge); });
    connect(process, &QProcess::errorOccurred, this, [this, &bridge](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) exited(bridge);
    });
    process->start();
}
void BridgeLauncher::exited(Bridge &bridge) {
    if (stopped_ || bridge.retry->isActive()) return;
    // A bridge that ran for a while earns a short pause again; one that dies at once backs off.
    if (bridge.clock.isValid() && bridge.clock.elapsed() > 30000) bridge.delayMs = retryMs;
    bridge.retry->start(bridge.delayMs);
    bridge.delayMs = qMin(bridge.delayMs * 2, maxDelayMs);
}
void BridgeLauncher::stop() {
    stopped_ = true;
    for (auto &[name, bridge] : bridges_) {
        bridge->retry->stop();
        if (bridge->process && bridge->process->state() != QProcess::NotRunning) {
            bridge->process->disconnect(this);
            bridge->process->terminate();
            if (!bridge->process->waitForFinished(1500)) { bridge->process->kill(); bridge->process->waitForFinished(1000); }
        }
    }
    bridges_.clear();
}
}

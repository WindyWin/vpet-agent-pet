#pragma once
#include <QElapsedTimer>
#include <map>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <memory>

namespace pet::interactions {
// Runs the bundled Node bridges (OpenCode, Codex) while the interaction service is up, and ends them with it. A bridge that
// exits is started again after a growing pause. Needs Node 20 or newer on PATH; without it, or without the bridge files, it
// does nothing and says why once (see status()).
class BridgeLauncher : public QObject {
    Q_OBJECT
public:
    explicit BridgeLauncher(QObject *parent = nullptr);
    ~BridgeLauncher() override;
    // Where the packaged bridges live next to `applicationDir`, or an empty string.
    static QString bridgeFolder(const QString &applicationDir);
    // The bridge scripts this platform runs, by file name.
    static QStringList scripts();
    // Overridable for tests: the Node executable, the bridge folder, and the endpoint passed to each bridge.
    QString node, folder, endpoint;
    int retryMs = 2000; // First pause before a bridge that exited is started again; doubles up to a minute.
    bool start();
    void stop();
    QString status() const { return status_; }
    bool running() const;
private:
    struct Bridge {
        QString script;
        std::unique_ptr<QProcess> process;
        QTimer *retry = nullptr;
        int delayMs = 2000;
        QElapsedTimer clock;
    };
    void launch(Bridge &bridge);
    void exited(Bridge &bridge);
    std::map<QString, std::unique_ptr<Bridge>> bridges_;
    QString status_;
    bool stopped_ = true;
};
}

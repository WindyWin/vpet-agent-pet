#include "commands.h"
#include <QDir>
#include <QProcess>
#include <QStandardPaths>

namespace pet::platform {
static QString executable(const QString &program) {
    auto path = QStandardPaths::findExecutable(program);
    if (path.isEmpty())
        path = QStandardPaths::findExecutable(program, {QDir::homePath() + "/.local/bin", QDir::homePath() + "/.cargo/bin",
                                                        QDir::homePath() + "/.linuxbrew/bin", "/home/linuxbrew/.linuxbrew/bin",
                                                        "/usr/local/bin", "/opt/homebrew/bin"});
    return path;
}
Outcome LinuxCommandRunner::run(const Command &command, QByteArray *output) {
    const auto program = executable(command.program);
    if (program.isEmpty()) return Outcome::Unsupported;
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    for (auto it = command.environment.begin(); it != command.environment.end(); ++it) env.insert(it.key(), it.value());
    process.setProcessEnvironment(env);
    process.setStandardInputFile(QProcess::nullDevice());
    process.start(program, command.arguments);
    if (!process.waitForFinished(timeoutMs)) {
        const bool started = process.error() != QProcess::FailedToStart;
        process.kill(); process.waitForFinished(200);
        return started ? Outcome::TimedOut : Outcome::Failed;
    }
    if (output) *output = process.readAllStandardOutput();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 ? Outcome::Confirmed : Outcome::Failed;
}
}

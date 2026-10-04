#include "installer.h"
#include "release.h"
#include "ipc/local.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <memory>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <signal.h>
#include <unistd.h>
#include <cstdio>
using namespace pet::updates;
static bool write(const QString &path, const QByteArray &data) {
    QSaveFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); app.setApplicationName("agent-pet");
    const auto args = app.arguments();
    if (args.size() < 3) return 2;
    const QString prefix = args[2];
    if (QFileInfo(prefix).canonicalFilePath() != prefix || prefix == "/") return 2;
    QDir().mkpath(dataDirectory());
    auto report = [&](const QString &message) { write(dataDirectory() + "/result.txt", message.toUtf8()); std::fprintf(stderr, "%s\n", qPrintable(message)); return 1; };
    QLockFile lock(prefix + ".update-lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) return report("Another update is already running.");
    if (args[1] == "--recover") {
        QString error; pet::Receiver receiver;
        if (!receiver.start(error)) return report("Close the running pet before update recovery.");
        return recoverInstallation(prefix, error) ? 0 : report(error);
    }
    if (args[1] != "--apply" || args.size() < 7) return 2;
    bool validPid = false; const qint64 parent = args[6].toLongLong(&validPid);
    if (!validPid || parent < 0) return 2;
    QElapsedTimer wait; wait.start();
    while (parent > 0 && kill(pid_t(parent), 0) == 0 && wait.elapsed() < 30000) QThread::msleep(100);
    if (parent > 0 && kill(pid_t(parent), 0) == 0) return report("The pet is still running. Update postponed.");
    bool relaunch = true;
    auto resumePrevious = qScopeGuard([&] {
        if (!relaunch) return;
        QFile::remove(dataDirectory() + "/pending.json");
        lock.unlock();
        QProcess::startDetached(prefix + "/bin/agent-pet", args.mid(7));
    });
    // Exclude other monitors during extraction and replacement. Hooks cannot start
    // a second GUI while the update lock is held.
    auto receiver = std::make_unique<pet::Receiver>(); QString error;
    if (!receiver->start(error)) return report("Close the running pet before installing an update.");
    if (!recoverInstallation(prefix, error)) { relaunch = false; return report(error); }
    const QString archive = args[3], digest = args[4], version = args[5];
    QFile receipt(prefix + "/.agent-pet-install");
    if (!receipt.open(QIODevice::ReadOnly) || receipt.size() > 65536) return report("This is not a managed installation.");
    QByteArray receiptData = receipt.readAll(); receipt.close();
    if (!receiptData.split('\n').contains(("prefix=" + prefix).toUtf8())) return report("Installation path does not match its receipt.");
    if (!verifiedArchive(archive, digest, error)) return report(error);
    QTemporaryDir staging(prefix + ".update-XXXXXX");
    if (!staging.isValid()) return report("Cannot stage the update beside the installation.");
    if (!extractArchive(archive, staging.path(), error)) return report(error);
    QProcess probe;
    probe.start(staging.path() + "/bin/agent-pet", {"--version"});
    if (!probe.waitForFinished(10000) || probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0
        || !probe.readAllStandardOutput().startsWith(("agent-pet " + version + " (").toUtf8())) {
        probe.kill(); probe.waitForFinished(); return report("The downloaded app cannot run on this system.");
    }
    auto lines = receiptData.split('\n');
    for (auto &line : lines) if (line.startsWith("version=")) line = "version=agent-pet " + version.toUtf8();
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!write(staging.path() + "/.agent-pet-install", lines.join('\n'))
        || !write(staging.path() + "/.agent-pet-update-id", token.toUtf8())) return report("Cannot write update metadata.");
    const QString journal = prefix + ".update-transaction.json";
    if (!write(journal, QJsonDocument(QJsonObject{{"stage", staging.path()}, {"token", token}}).toJson())) return report("Cannot write recovery record.");
    staging.setAutoRemove(false);
    if (!exchangeDirectories(prefix, staging.path(), error)) { recoverInstallation(prefix, error); return report(error); }
    receiver.reset();
    const QString health = dataDirectory() + "/health-" + token;
    QStringList launch = args.mid(7); launch << "--update-health" << token;
    QProcess child;
    child.setProcessChannelMode(QProcess::ForwardedChannels);
    child.start(prefix + "/bin/agent-pet", launch);
    bool healthy = false;
    if (child.waitForStarted(10000)) {
        wait.restart();
        while (wait.elapsed() < 30000) {
            if (QFile::exists(health)) { healthy = true; break; }
            if (child.waitForFinished(100)) break;
        }
    }
    if (!healthy) {
        child.terminate(); if (!child.waitForFinished(3000)) { child.kill(); child.waitForFinished(3000); }
        if (!recoverInstallation(prefix, error)) { relaunch = false; return report("Update failed; recovery needs attention: " + error); }
        report("Update failed to start. The previous version was restored.");
        // Do not automatically retry the failed package on the next launch.
        QFile::remove(dataDirectory() + "/pending.json");
        return 1;
    }
    // Removing the journal commits the new version. A crash before this point
    // rolls back on the next launch; the previous version remains until commit.
    if (!QFile::remove(journal)) {
        child.terminate(); if (!child.waitForFinished(3000)) { child.kill(); child.waitForFinished(3000); }
        relaunch = false;
        return report("Could not commit update; recovery will run at next launch.");
    }
    relaunch = false;
    QDir(staging.path()).removeRecursively(); QFile::remove(health);
    QFile::remove(dataDirectory() + "/pending.json"); QFile::remove(archive);
    write(dataDirectory() + "/result.txt", ("Updated to " + version + '.').toUtf8());
    lock.unlock();
    // Keep the child process supervised without polling or terminating it when
    // this helper leaves scope. The helper exits when the pet exits.
    child.waitForFinished(-1); return 0;
}

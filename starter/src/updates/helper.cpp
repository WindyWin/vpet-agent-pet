#include "platform/contracts/update_layout.h"
#include "i18n/contexts.h"
#include "installer.h"
#include "release.h"
#include "components.h"
#include "i18n/language.h"
#include "ipc/local.h"
#include "settings/preferences.h"
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
#include <cstdio>
using namespace pet::updates;
static bool write(const QString &path, const QByteArray &data) {
    QSaveFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); app.setApplicationName("agent-pet");
    // The pet shows result.txt later, so it is written in the pet's language.
    pet::i18n::install(pet::i18n::fromName(pet::PreferencesStore().load().language));
    const auto args = app.arguments();
    if (args.size() < 3) return 2;
    const QString prefix = args[2];
    if (QFileInfo(prefix).canonicalFilePath() != prefix || prefix == "/") return 2;
    QDir().mkpath(dataDirectory());
    auto report = [&](const QString &message) { write(dataDirectory() + "/result.txt", message.toUtf8()); std::fprintf(stderr, "%s\n", qPrintable(message)); return 1; };
    QLockFile lock(prefix + ".update-lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) return report(Updater::tr("Another update is already running."));
    if (args[1] == "--recover") {
        QString error; pet::Receiver receiver;
        if (!receiver.start(error)) return report(Updater::tr("Close the running pet before update recovery."));
        return recoverInstallation(prefix, error) ? 0 : report(error);
    }
    const bool componentUpdate = args[1] == "--apply-components";
    if ((!componentUpdate && args[1] != "--apply") || args.size() < 7) return 2;
    bool validPid = false; const qint64 parent = args[6].toLongLong(&validPid);
    if (!validPid || parent < 0) return 2;
    QElapsedTimer wait; wait.start();
    while (pet::platform::processRunning(parent) && wait.elapsed() < 30000) QThread::msleep(100);
    if (pet::platform::processRunning(parent)) return report(Updater::tr("The pet is still running. Update postponed."));
    bool relaunch = true;
    auto resumePrevious = qScopeGuard([&] {
        if (!relaunch) return;
        QFile::remove(dataDirectory() + "/pending.json");
        lock.unlock();
        QProcess::startDetached(prefix + '/' + pet::platform::applicationRelativePath(), args.mid(7));
    });
    // Exclude other monitors during extraction and replacement. Hooks cannot start
    // a second GUI while the update lock is held.
    auto receiver = std::make_unique<pet::Receiver>(); QString error;
    if (!receiver->start(error)) return report(Updater::tr("Close the running pet before installing an update."));
    if (!recoverInstallation(prefix, error)) { relaunch = false; return report(error); }
    const QString archive = args[3], digest = args[4], version = args[5];
    QFile receipt(prefix + "/.agent-pet-install");
    if (!receipt.open(QIODevice::ReadOnly) || receipt.size() > 65536) return report(Updater::tr("This is not a managed installation."));
    QByteArray receiptData = receipt.readAll(); receipt.close();
    if (!receiptData.split('\n').contains(("prefix=" + prefix).toUtf8())) return report(Updater::tr("Installation path does not match its receipt."));
    Components components;
    if (componentUpdate) {
        if (!readComponents(archive, digest, version, pet::platform::updateArchitecture(), components, error)) return report(error);
    } else if (!verifiedArchive(archive, digest, error)) return report(error);
    QTemporaryDir staging(prefix + ".update-XXXXXX");
    if (!staging.isValid()) return report(Updater::tr("Cannot stage the update beside the installation."));
    if (componentUpdate) {
        if (!assembleComponents(components, prefix, QFileInfo(archive).absolutePath(), staging.path(), error)) return report(error);
    } else if (!extractArchive(archive, staging.path(), error)) return report(error);
    QProcess probe;
    probe.start(staging.path() + '/' + pet::platform::applicationRelativePath(), {"--version"});
    if (!probe.waitForFinished(10000) || probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0
        || !probe.readAllStandardOutput().startsWith(("agent-pet " + version + " (").toUtf8())) {
        probe.kill(); probe.waitForFinished(); return report(Updater::tr("The downloaded app cannot run on this system."));
    }
    auto lines = receiptData.split('\n');
    for (auto &line : lines) if (line.startsWith("version=")) line = "version=agent-pet " + version.toUtf8();
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!write(staging.path() + "/.agent-pet-install", lines.join('\n'))
        || !write(staging.path() + "/.agent-pet-update-id", token.toUtf8())) return report(Updater::tr("Cannot write update metadata."));
    const QString journal = prefix + ".update-transaction.json";
    if (!write(journal, QJsonDocument(QJsonObject{{"stage", staging.path()}, {"token", token}}).toJson())) return report(Updater::tr("Cannot write recovery record."));
    staging.setAutoRemove(false);
    if (!exchangeDirectories(prefix, staging.path(), error)) { recoverInstallation(prefix, error); return report(error); }
    receiver.reset();
    const QString health = dataDirectory() + "/health-" + token;
    QStringList launch = args.mid(7); launch << "--update-health" << token;
    QProcess child;
    child.setProcessChannelMode(QProcess::ForwardedChannels);
    child.start(prefix + '/' + pet::platform::applicationRelativePath(), launch);
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
        if (!recoverInstallation(prefix, error)) { relaunch = false; return report(Updater::tr("Update failed; recovery needs attention: %1").arg(error)); }
        report(Updater::tr("Update failed to start. The previous version was restored."));
        // Do not automatically retry the failed package on the next launch.
        QFile::remove(dataDirectory() + "/pending.json");
        return 1;
    }
    // Removing the journal commits the new version. A crash before this point
    // rolls back on the next launch; the previous version remains until commit.
    if (!QFile::remove(journal)) {
        child.terminate(); if (!child.waitForFinished(3000)) { child.kill(); child.waitForFinished(3000); }
        relaunch = false;
        return report(Updater::tr("Could not commit update; recovery will run at next launch."));
    }
    relaunch = false;
    QDir(staging.path()).removeRecursively(); QFile::remove(health);
    QFile::remove(dataDirectory() + "/pending.json"); QFile::remove(archive);
    for (const auto &component : components.entries)
        QFile::remove(QFileInfo(archive).absolutePath() + '/' + component.archive);
    write(dataDirectory() + "/result.txt", Updater::tr("Updated to %1.").arg(version).toUtf8());
    lock.unlock();
    // Keep the child process supervised without polling or terminating it when
    // this helper leaves scope. The helper exits when the pet exits.
    child.waitForFinished(-1); return 0;
}

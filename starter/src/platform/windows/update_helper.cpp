#include "installed_directory.h"
#include "platform/contracts/update_layout.h"
#include "updates/release.h"
#include "i18n/contexts.h"
#include "i18n/language.h"
#include "settings/preferences.h"
#include "ipc/local.h"
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
using namespace pet::updates;

static bool writeResult(const QString &message) {
    QSaveFile file(dataDirectory() + "/result.txt");
    const auto bytes = message.toUtf8();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("agent-pet");
    pet::i18n::install(pet::i18n::fromName(pet::PreferencesStore().load().language));
    auto args = app.arguments();
    if (args.size() < 7 || (args[1] != "--apply" && args[1] != "--run-setup")) return 2;
    const QString prefix = args[2], package = args[3], digest = args[4], version = args[5];
    bool validPid = false;
    const qint64 parent = args[6].toLongLong(&validPid);
    if (!validPid || parent < 0 || QFileInfo(prefix).canonicalFilePath() != prefix
        || !pet::platform::registeredInstallation(prefix)) return 2;
    const bool bootstrapMode = args[1] == "--apply";
    bool validBootstrap = false;
    const qint64 bootstrap = bootstrapMode ? 0 : args.value(7).toLongLong(&validBootstrap);
    if (!bootstrapMode && (!validBootstrap || bootstrap <= 0)) return 2;
    const QStringList restartArguments = args.mid(bootstrapMode ? 7 : 8);
    bool resume = true;
    auto relaunch = qScopeGuard([&] {
        if (!resume) return;
        QElapsedTimer wait; wait.start();
        while ((pet::platform::processRunning(parent) || pet::platform::processRunning(bootstrap)) && wait.elapsed() < 30000)
            QThread::msleep(100);
        if (!pet::platform::processRunning(parent) && !pet::platform::processRunning(bootstrap))
            QProcess::startDetached(prefix + '/' + pet::platform::applicationRelativePath(), restartArguments, prefix);
    });
    QDir().mkpath(dataDirectory());
    auto report = [&](const QString &message) {
        QFile::remove(dataDirectory() + "/pending.json");
        writeResult(message); return 1;
    };
    QString error;
    if (!verifiedArchive(package, digest, error)) return report(error);

    if (args[1] == "--apply") {
        // Windows cannot delete the executing helper. A later update reclaims inactive copies.
        const QDir updates(dataDirectory());
        for (const auto &name : updates.entryList({"installer-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString directory = updates.filePath(name);
            if (QFileInfo(directory).isSymLink()
                || QFileInfo(directory).lastModified().secsTo(QDateTime::currentDateTime()) < 86400) continue;
            QLockFile running(directory + "/.running-lock");
            running.setStaleLockTime(0);
            if (running.tryLock()) { running.unlock(); QDir(directory).removeRecursively(); }
        }
        // The installed helper and its loaded DLLs cannot be overwritten on Windows.
        // Run a private copy outside the installation, including a verified copy of Setup.
        QTemporaryDir runtime(dataDirectory() + "/installer-XXXXXX");
        if (!runtime.isValid()) return report(Updater::tr("Cannot stage the update beside the installation."));
        const QDir installed(prefix);
        for (const auto &name : installed.entryList({"*.dll", "agent-pet-updater.exe"}, QDir::Files)) {
            if (!QFile::copy(installed.filePath(name), runtime.filePath(name)))
                return report(Updater::tr("Cannot reuse installed files; check free disk space."));
        }
        const QString setup = runtime.filePath("setup.exe");
        if (!QFile::copy(package, setup) || !verifiedArchive(setup, digest, error))
            return report(error.isEmpty() ? Updater::tr("Cannot save the download. Check free disk space.") : error);
        args.removeFirst();
        args[0] = "--run-setup";
        args[2] = setup;
        args.insert(6, QString::number(QCoreApplication::applicationPid()));
        if (!QProcess::startDetached(runtime.filePath("agent-pet-updater.exe"), args, runtime.path()))
            return report(Updater::tr("Could not start the update installer."));
        runtime.setAutoRemove(false);
        resume = false; // The private helper now owns restart.
        return 0;
    }

    QLockFile running(QCoreApplication::applicationDirPath() + "/.running-lock");
    running.setStaleLockTime(0);
    if (!running.tryLock()) return report(Updater::tr("Another update is already running."));
    QLockFile lock(prefix + ".update-lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) { resume = false; return report(Updater::tr("Another update is already running.")); }
    QElapsedTimer wait;
    wait.start();
    while ((pet::platform::processRunning(parent) || pet::platform::processRunning(bootstrap)) && wait.elapsed() < 30000)
        QThread::msleep(100);
    if (pet::platform::processRunning(parent) || pet::platform::processRunning(bootstrap))
        return report(Updater::tr("The pet is still running. Update postponed."));

    // Consume a pending attempt before invoking Setup, including on a crash or failure.
    // The cached download remains available for a deliberate retry.
    QFile::remove(dataDirectory() + "/pending.json");
    pet::Receiver receiver;
    if (!receiver.start(error)) return report(Updater::tr("Close the running pet before installing an update."));
    QProcess setup;
    setup.setWorkingDirectory(QFileInfo(package).absolutePath());
    setup.start(package, {"/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOCLOSEAPPLICATIONS",
                          "/NORESTARTAPPLICATIONS", "/TASKS=", "/AGENTPETUPDATE=1", "/DIR=" + prefix,
                          "/LOG=" + dataDirectory() + "/setup.log"});
    // Wait for Setup to finish; terminating an installer midway can damage the installation.
    if (!setup.waitForStarted(10000) || !setup.waitForFinished(-1)
        || setup.exitStatus() != QProcess::NormalExit || setup.exitCode() != 0)
        return report(Updater::tr("Windows update installation failed. Run the setup program manually; details are in %1.")
                      .arg(dataDirectory() + "/setup.log"));
    QProcess probe;
    probe.start(prefix + "/agent-pet-cli.exe", {"--version"});
    if (!probe.waitForFinished(10000) || probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0
        || !probe.readAllStandardOutput().startsWith(("agent-pet " + version + " (").toUtf8())) {
        probe.kill(); probe.waitForFinished(3000);
        return report(Updater::tr("The downloaded app cannot run on this system."));
    }
    QFile::remove(dataDirectory() + "/package.exe");
    writeResult(Updater::tr("Updated to %1.").arg(version));
    return 0;
}

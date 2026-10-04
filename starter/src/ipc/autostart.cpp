#include "autostart.h"
#include "settings/preferences.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace pet {
static void closeRange(unsigned first, unsigned last) {
    if (first > last) return;
    // close_range needs Linux 5.9; older kernels fall back to closing each descriptor.
#ifdef SYS_close_range
    if (syscall(SYS_close_range, first, last, 0u) == 0) return;
#endif
    const long limit = sysconf(_SC_OPEN_MAX);
    const unsigned end = std::min<unsigned>(last, limit > 0 && limit < 65536 ? unsigned(limit) - 1 : 65535u);
    for (unsigned fd = first; fd <= end; ++fd) close(int(fd));
}
bool launchDetached(const QString &executable, const QStringList &arguments) {
    // Everything the children touch is prepared here: only async-signal-safe calls follow fork.
    QList<QByteArray> storage{QFile::encodeName(executable)};
    for (const auto &argument : arguments) storage.append(argument.toLocal8Bit());
    QVector<char *> argv;
    for (auto &item : storage) argv.append(item.data());
    argv.append(nullptr);
    int report[2];
    if (pipe2(report, O_CLOEXEC)) return false;
    const pid_t child = fork();
    if (child < 0) { close(report[0]); close(report[1]); return false; }
    if (child == 0) {
        close(report[0]);
        setsid();
        if (fork() != 0) _exit(0); // The grandchild is adopted by init or the session's subreaper.
        signal(SIGPIPE, SIG_DFL); signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL); signal(SIGHUP, SIG_DFL);
        sigset_t none; sigemptyset(&none); sigprocmask(SIG_SETMASK, &none, nullptr);
        // The report pipe closes on a successful exec; otherwise it carries errno.
        if (report[1] < 3) report[1] = fcntl(report[1], F_DUPFD_CLOEXEC, 3);
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); if (null > 2) close(null); }
        if (chdir("/")) {}
        if (report[1] >= 3) { closeRange(3, unsigned(report[1]) - 1); closeRange(unsigned(report[1]) + 1, ~0u); }
        else closeRange(3, ~0u);
        execv(argv[0], argv.data());
        const int error = errno;
        if (write(report[1], &error, sizeof(error))) {}
        _exit(127);
    }
    close(report[1]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    int error = 0;
    ssize_t size;
    while ((size = read(report[0], &error, sizeof(error))) < 0 && errno == EINTR) {}
    close(report[0]);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && size == 0;
}
bool autostartPet(const QByteArray &event, const QString &kind, const QString &preferencesPath,
                  const QProcessEnvironment &environment, const QString &executable, const Launcher &launch) {
    if (kind != "session_start" || !launch) return false;
    if (environment.value("DISPLAY").isEmpty() && environment.value("WAYLAND_DISPLAY").isEmpty()) return false;
    PreferencesStore store(preferencesPath);
    if (!store.load().autostart || !store.error().isEmpty()) return false;
    return launch(executable, {"--autostarted", "--launch-event", QString::fromUtf8(event)});
}
QString loginEntryPath(const QString &directory) {
    const auto base = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/autostart" : directory;
    return base + "/agent-pet.desktop";
}
bool loginStartEnabled(const QString &directory) { return QFile::exists(loginEntryPath(directory)); }
// Desktop Entry Exec quoting: double quotes, with \ " ` $ escaped, then each backslash doubled at string level.
static QString desktopExec(const QString &executable) {
    QString quoted;
    for (const QChar c : executable) {
        if (c == '%') quoted += "%%";
        else if (c == '\\' || c == '"' || c == '`' || c == '$') quoted += QString("\\\\") + c;
        else quoted += c;
    }
    return "\"" + quoted + "\"";
}
bool setLoginStart(bool enabled, const QString &executable, QString *error, const QString &directory) {
    const auto path = loginEntryPath(directory);
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!enabled) return !QFile::exists(path) || QFile::remove(path) || fail("Cannot remove " + path);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return fail("Cannot create " + QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    const auto bytes = ("[Desktop Entry]\nType=Application\nName=Agent Pet\nComment=Start the Agent Pet desktop companion at login\n"
                        "Exec=" + desktopExec(executable) + "\nIcon=agent-pet\nTerminal=false\nX-GNOME-Autostart-enabled=true\n").toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) return fail(file.errorString());
    return true;
}
static int loginCommand(const QStringList &args) {
    const auto operation = args.value(3);
    if (args.size() != 4 || (operation != "enable" && operation != "disable" && operation != "status")) {
        std::fprintf(stderr, "Usage: agent-pet autostart login enable|disable|status\n"); return 1;
    }
    QString error;
    if (operation != "status" && !setLoginStart(operation == "enable", QCoreApplication::applicationFilePath(), &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error)); return 1;
    }
    const QJsonObject report{{"start_at_login", loginStartEnabled()}, {"entry", loginEntryPath()}};
    const auto output = QJsonDocument(report).toJson();
    std::fwrite(output.constData(), 1, output.size(), stdout);
    return 0;
}
int autostartCommand(const QStringList &args) {
    if (args.value(2) == "login") return loginCommand(args);
    const auto operation = args.value(2);
    QString error;
    auto fail = [&] { std::fprintf(stderr, "%s\n", qPrintable(error)); return 1; };
    const QString usage = "Usage: agent-pet autostart enable|disable|status [--when-idle keep|hide|quit]\n"
                          "       agent-pet autostart login enable|disable|status";
    if (operation != "enable" && operation != "disable" && operation != "status") { error = usage; return fail(); }
    bool setPolicy = false;
    IdlePolicy policy = IdlePolicy::Keep;
    for (int i = 3; i < args.size(); ++i) {
        if (args[i] == "--when-idle" && i + 1 < args.size() && !setPolicy && operation != "status") {
            if (!parseIdlePolicy(args[++i], policy)) { error = "Expected --when-idle keep, hide or quit"; return fail(); }
            setPolicy = true;
        } else { error = usage; return fail(); }
    }
    PreferencesStore store;
    auto preferences = store.load();
    if (!store.error().isEmpty()) { error = store.error() + "\n" + store.path(); return fail(); }
    bool changed = false;
    if (operation != "status") {
        auto updated = preferences;
        updated.autostart = operation == "enable";
        if (setPolicy) updated.whenIdle = policy;
        // Saving only on a change: disabling what was never enabled creates no file.
        changed = updated.autostart != preferences.autostart || updated.whenIdle != preferences.whenIdle;
        if (changed && !store.save(updated)) { error = "Cannot save preferences: " + store.error() + "\n" + store.path(); return fail(); }
        preferences = updated;
    }
    const QJsonObject report{{"autostart", preferences.autostart}, {"when_idle", idlePolicyName(preferences.whenIdle)},
                             {"preferences", store.path()}, {"changed", changed}};
    const auto output = QJsonDocument(report).toJson();
    std::fwrite(output.constData(), 1, output.size(), stdout);
    return 0;
}
}

#include "ipc/autostart.h"
#include "ipc/local.h"
#include "sessions/presence.h"
#include "settings/preferences.h"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <csignal>
#include <unistd.h>

using Action = pet::Presence::Action;
using Visibility = pet::Presence::Visibility;

class StartupTests : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    QString runtime, data;
    QProcessEnvironment environment() const {
        auto env = QProcessEnvironment::systemEnvironment();
        env.remove("DISPLAY"); env.remove("WAYLAND_DISPLAY");
        env.insert("XDG_RUNTIME_DIR", runtime); env.insert("XDG_DATA_HOME", data);
        env.insert("QT_QPA_PLATFORM", "offscreen");
        return env;
    }
    QString preferences() const { return data + "/agent-pet/preferences.json"; }
    QByteArray launchEvent(const QString &kind = "session_start") const {
        return QJsonDocument(QJsonObject{{"version", 1}, {"provider", "claude"}, {"session_id", "launched"},
                                         {"event_id", QUuid::createUuid().toString()}, {"kind", kind},
                                         {"timestamp_ms", double(QDateTime::currentMSecsSinceEpoch())}})
            .toJson(QJsonDocument::Compact);
    }
    struct Result { int code = -99; QByteArray out, err; qint64 ms = 0; };
    Result run(const QStringList &args, const QProcessEnvironment &env, const QByteArray &input = {}, int timeout = 5000) {
        QProcess process; process.setProgram(APP_PATH); process.setArguments(args); process.setProcessEnvironment(env);
        QElapsedTimer timer; timer.start(); process.start();
        Result result;
        if (!process.waitForStarted(timeout)) return result;
        process.write(input); process.closeWriteChannel();
        if (!process.waitForFinished(timeout)) { process.kill(); process.waitForFinished(); result.code = -98; return result; }
        result = {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError(), timer.elapsed()};
        return result;
    }
    // Pets started detached by these tests, found by their private runtime directory.
    QList<pid_t> testPets() const {
        QList<pid_t> pids;
        for (const auto &entry : QDir("/proc").entryList(QDir::Dirs)) {
            bool number = false; const auto pid = entry.toInt(&number);
            if (!number || QFile::symLinkTarget("/proc/" + entry + "/exe") != QFileInfo(APP_PATH).canonicalFilePath()) continue;
            QFile environ("/proc/" + entry + "/environ");
            if (environ.open(QIODevice::ReadOnly) && environ.readAll().split('\0').contains("XDG_RUNTIME_DIR=" + runtime.toUtf8()))
                pids.append(pid);
        }
        return pids;
    }
    bool listening() { return run({"emit"}, environment(), launchEvent("prompt"), 2000).code == 0; }
private slots:
    void initTestCase() {
        QVERIFY(root.isValid());
        runtime = root.path() + "/runtime"; data = root.path() + "/data";
        QVERIFY(QDir().mkpath(runtime));
        QFile::setPermissions(runtime, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("XDG_RUNTIME_DIR", runtime.toUtf8());
    }
    void cleanup() {
        for (const auto pid : testPets()) kill(pid, SIGKILL);
        QFile::remove(preferences());
    }

    void presenceUserHiding() {
        pet::Presence presence;
        QCOMPARE(presence.visibility(), Visibility::Shown);
        QCOMPARE(presence.setUserHidden(true), Action::None); // No tray: hiding is disabled.
        QVERIFY(!presence.hidden());
        presence.setTrayAvailable(true);
        QCOMPARE(presence.setUserHidden(true), Action::Hide);
        QCOMPARE(presence.visibility(), Visibility::UserHidden);
        QCOMPARE(presence.setUserHidden(true), Action::None);
        // New sessions do not bring back a pet the user hid.
        QCOMPARE(presence.update(2, 1000), Action::None); QVERIFY(presence.hidden());
        QCOMPARE(presence.setUserHidden(false), Action::Show);
        QCOMPARE(presence.setUserHidden(false), Action::None);
    }
    void presenceIdlePolicies() {
        const qint64 grace = pet::Presence::idleGraceMs;
        pet::Presence keep; keep.setTrayAvailable(true);
        QCOMPARE(keep.update(0, 0), Action::None); QCOMPARE(keep.idleDeadline(), 0); // Never saw a session.
        QCOMPARE(keep.update(0, 10 * grace), Action::None);
        QCOMPARE(keep.update(1, 1000), Action::None);
        QCOMPARE(keep.update(0, 2000), Action::None); QCOMPARE(keep.idleDeadline(), 2000 + grace);
        QCOMPARE(keep.update(0, 2000 + grace), Action::None); QVERIFY(!keep.hidden());

        pet::Presence hide; hide.setTrayAvailable(true); hide.setPolicy(pet::IdlePolicy::Hide);
        hide.update(1, 0); hide.update(0, 1000);
        QCOMPARE(hide.update(0, 1000 + grace - 1), Action::None);
        QCOMPARE(hide.update(1, 1000 + grace - 1), Action::None); // A new session cancels the countdown.
        QCOMPARE(hide.idleDeadline(), 0);
        hide.update(0, 5000);
        QCOMPARE(hide.update(0, 5000 + grace), Action::Hide);
        QCOMPARE(hide.visibility(), Visibility::AutoHidden);
        QCOMPARE(hide.update(0, 5000 + 3 * grace), Action::None); // Fires once per idle stretch.
        QCOMPARE(hide.update(1, 5000 + 4 * grace), Action::Show); // Auto-hidden: back with the next session.
        // Auto-hidden then hidden by the user: stays hidden through new sessions.
        hide.update(0, 0); hide.update(0, grace); QCOMPARE(hide.visibility(), Visibility::AutoHidden);
        QCOMPARE(hide.setUserHidden(true), Action::None); QCOMPARE(hide.visibility(), Visibility::UserHidden);
        QCOMPARE(hide.update(1, 2 * grace), Action::None); QVERIFY(hide.hidden());

        pet::Presence noTray; noTray.setPolicy(pet::IdlePolicy::Hide); // Falls back to keep.
        noTray.update(1, 0); noTray.update(0, 0);
        QCOMPARE(noTray.update(0, grace), Action::None); QVERIFY(!noTray.hidden());

        pet::Presence quit; quit.setPolicy(pet::IdlePolicy::Quit);
        quit.update(1, 0); quit.update(0, 0);
        QCOMPARE(quit.update(0, grace), Action::Quit);

        pet::IdlePolicy policy;
        for (const auto *name : {"keep", "hide", "quit"}) {
            QVERIFY(pet::parseIdlePolicy(name, policy)); QCOMPARE(pet::idlePolicyName(policy), QString(name));
        }
        QVERIFY(!pet::parseIdlePolicy("Hide", policy)); QVERIFY(!pet::parseIdlePolicy({}, policy));
    }
    void preferenceFieldsRoundTrip() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        pet::PreferencesStore store(path); pet::Preferences prefs;
        prefs.autostart = true; prefs.whenIdle = pet::IdlePolicy::Quit;
        QVERIFY(store.save(prefs)); // No position yet: written without one.
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto object = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        QVERIFY(!object.contains("x")); QCOMPARE(object["when_idle"].toString(), "quit"); QVERIFY(object["autostart"].toBool());
        auto loaded = pet::PreferencesStore(path).load();
        QVERIFY(loaded.autostart); QCOMPARE(loaded.whenIdle, pet::IdlePolicy::Quit); QVERIFY(!loaded.hasPosition);
        loaded.hasPosition = true; loaded.position = {5, 6}; loaded.whenIdle = pet::IdlePolicy::Hide;
        QVERIFY(store.save(loaded));
        loaded = pet::PreferencesStore(path).load();
        QCOMPARE(loaded.position, QPoint(5, 6)); QCOMPARE(loaded.whenIdle, pet::IdlePolicy::Hide);
        // Files from earlier versions have no startup keys.
        const QByteArray legacy = R"({"version":1,"size":200,"on_top":true,"x":10,"y":20,"muted":true})";
        for (const auto &[text, valid] : QList<std::pair<QByteArray, bool>>{
                 {legacy, true},
                 {R"({"version":1,"size":200,"on_top":true,"when_idle":"keep","autostart":false})", true},
                 {R"({"version":1,"size":200,"on_top":true,"x":10})", false},
                 {R"({"version":1,"size":200,"on_top":true,"autostart":"yes"})", false},
                 {R"({"version":1,"size":200,"on_top":true,"when_idle":"sleep"})", false}}) {
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write(text); file.close();
            pet::PreferencesStore check(path); const auto result = check.load();
            QVERIFY2(check.error().isEmpty() == valid, text.constData());
            QVERIFY(!result.autostart); QCOMPARE(result.whenIdle, pet::IdlePolicy::Keep);
        }
    }
    void autostartCommand() {
        const auto env = environment();
        auto status = run({"autostart", "status"}, env);
        QCOMPARE(status.code, 0);
        auto report = QJsonDocument::fromJson(status.out).object();
        QVERIFY(!report["autostart"].toBool()); QCOMPARE(report["when_idle"].toString(), "keep");
        QCOMPARE(report["preferences"].toString(), preferences());
        QCOMPARE(run({"autostart", "disable"}, env).code, 0);
        QVERIFY(!QFile::exists(preferences())); // Nothing to record.
        auto enable = run({"autostart", "enable", "--when-idle", "hide"}, env);
        QCOMPARE(enable.code, 0); QVERIFY(QJsonDocument::fromJson(enable.out).object()["changed"].toBool());
        auto loaded = pet::PreferencesStore(preferences()).load();
        QVERIFY(loaded.autostart); QCOMPARE(loaded.whenIdle, pet::IdlePolicy::Hide);
        QVERIFY(!QJsonDocument::fromJson(run({"autostart", "enable"}, env).out).object()["changed"].toBool());
        // Disabling keeps the other preferences.
        QFile file(preferences()); QVERIFY(file.open(QIODevice::ReadOnly)); auto object = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        object["size"] = 300; object["x"] = 7; object["y"] = 8;
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(object).toJson()); file.close();
        QCOMPARE(run({"autostart", "disable"}, env).code, 0);
        loaded = pet::PreferencesStore(preferences()).load();
        QVERIFY(!loaded.autostart); QCOMPARE(loaded.size, 300); QCOMPARE(loaded.position, QPoint(7, 8));
        QCOMPARE(loaded.whenIdle, pet::IdlePolicy::Hide);
        report = QJsonDocument::fromJson(run({"autostart", "status"}, env).out).object();
        QVERIFY(!report["autostart"].toBool()); QCOMPARE(report["when_idle"].toString(), "hide");
        // Usage errors and invalid files fail without touching the file.
        QCOMPARE(run({"autostart"}, env).code, 1);
        QCOMPARE(run({"autostart", "enable", "--when-idle", "never"}, env).code, 1);
        QCOMPARE(run({"autostart", "status", "--when-idle", "hide"}, env).code, 1);
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("broken"); file.close();
        auto broken = run({"autostart", "enable"}, env);
        QCOMPARE(broken.code, 1); QVERIFY(broken.err.contains("Invalid preferences"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("broken"));
    }
    void hookLaunchDecision() {
        QTemporaryDir directory; const auto path = directory.path() + "/preferences.json";
        QStringList calls; QString program;
        const pet::Launcher fake = [&](const QString &executable, const QStringList &args) { program = executable; calls = args; return true; };
        QProcessEnvironment display; display.insert("WAYLAND_DISPLAY", "wayland-0");
        const auto event = launchEvent();
        auto decide = [&](const QString &kind, const QProcessEnvironment &env) {
            calls.clear(); return pet::autostartPet(event, kind, path, env, "/opt/agent pet/bin/agent-pet", fake);
        };
        QVERIFY(!decide("session_start", display)); QVERIFY(calls.isEmpty()); // No preferences: off by default.
        pet::Preferences prefs; prefs.autostart = true; QVERIFY(pet::PreferencesStore(path).save(prefs));
        QVERIFY(decide("session_start", display));
        QCOMPARE(program, "/opt/agent pet/bin/agent-pet");
        QCOMPARE(calls, (QStringList{"--autostarted", "--launch-event", QString::fromUtf8(event)}));
        QProcessEnvironment x11; x11.insert("DISPLAY", ":0");
        QVERIFY(decide("session_start", x11));
        QVERIFY(!decide("session_start", {})); // SSH or containers: no display, no launch.
        QVERIFY(!decide("prompt", display)); QVERIFY(calls.isEmpty());
        QVERIFY(!pet::autostartPet(event, "session_start", path, display, "x", [](auto &, auto &) { return false; }));
        prefs.autostart = false; QVERIFY(pet::PreferencesStore(path).save(prefs));
        QVERIFY(!decide("session_start", display));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(R"({"autostart":true})"); file.close();
        QVERIFY(!decide("session_start", display)); // Invalid file: never trusted.
    }
    void detachedLaunch() {
        QTemporaryDir directory; const auto marker = directory.path() + "/ran";
        // The child runs in its own session, in /, with stdin on /dev/null.
        QVERIFY(pet::launchDetached("/bin/sh", {"-c", "{ pwd; ps -o sid= -p $$; cat; } > \"$0\"", marker}));
        QTRY_VERIFY(QFileInfo(marker).size() > 0);
        QFile file(marker); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto lines = file.readAll().split('\n');
        QCOMPARE(lines.value(0), QByteArray("/"));
        if (!lines.value(1).trimmed().isEmpty()) QVERIFY(lines.value(1).trimmed().toLongLong() != getsid(0));
        QVERIFY(!pet::launchDetached(directory.path() + "/missing", {}));
    }
    void launchEventHandoffUnderLockContention() {
        const auto env = environment(); const auto event = launchEvent();
        pet::Receiver receiver; QString error; QVERIFY2(receiver.start(error), qPrintable(error));
        QList<pet::Event> received;
        receiver.received = [&](const pet::Event &e) { received.append(e); };
        // An autostarted pet that loses the lock forwards its event and exits silently.
        auto lost = run({"--autostarted", "--launch-event", QString::fromUtf8(event)}, env);
        QCOMPARE(lost.code, 0); QVERIFY(lost.out.isEmpty()); QVERIFY(lost.err.isEmpty());
        QTRY_COMPARE(received.size(), 1);
        QCOMPARE(received[0].session, "launched"); QCOMPARE(received[0].kind, "session_start");
        // An invalid launch event is dropped, never forwarded.
        QCOMPARE(run({"--autostarted", "--launch-event", "{\"kind\":\"prompt\"}"}, env).code, 0);
        QTest::qWait(200); QCOMPARE(received.size(), 1);
        // A manual start still reports the running pet.
        auto manual = run({"--no-persist"}, env);
        QCOMPARE(manual.code, 1); QVERIFY(manual.err.contains("Another Agent Pet monitor is running"));
    }
    void hookAutostartsDetachedPet() {
        QVERIFY(testPets().isEmpty());
        auto env = environment(); env.insert("WAYLAND_DISPLAY", "agent-pet-test");
        const QByteArray start = R"({"session_id":"hooked","hook_event_name":"SessionStart","cwd":"/work/hooked"})";
        // Autostart off: the event is dropped silently, as before.
        auto hook = run({"hook", "--provider", "claude"}, env, start);
        QCOMPARE(hook.code, 0); QTest::qWait(300); QVERIFY(testPets().isEmpty());
        QCOMPARE(run({"autostart", "enable"}, env).code, 0);
        // Not a session start: no launch.
        QCOMPARE(run({"hook", "--provider", "claude"}, env, R"({"session_id":"hooked","hook_event_name":"UserPromptSubmit"})").code, 0);
        QTest::qWait(300); QVERIFY(testPets().isEmpty());
        // The client reads hook output until EOF: a pet holding the pipe would stall this pipeline.
        QProcess shell; shell.setProcessEnvironment(env);
        shell.start("/bin/sh", {"-c", "printf '%s' \"$1\" | \"$0\" hook --provider claude | cat", APP_PATH, QString::fromUtf8(start)});
        QElapsedTimer timer; timer.start();
        QVERIFY(shell.waitForFinished(3000)); QCOMPARE(shell.exitCode(), 0);
        QVERIFY2(timer.elapsed() < 1000, "hook pipeline waited on the launched pet");
        QVERIFY(shell.readAllStandardOutput().isEmpty()); QVERIFY(shell.readAllStandardError().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(listening(), 15000);
        QCOMPARE(testPets().size(), 1);
        // A second session start reaches the running pet instead of launching another.
        QCOMPARE(run({"hook", "--provider", "claude"}, env, start).code, 0);
        QTest::qWait(500); QCOMPARE(testPets().size(), 1);
    }
    void loginEntryRoundTrip() {
        QTemporaryDir dir;
        const auto autostartDir = dir.path() + "/autostart";
        QVERIFY(!pet::loginStartEnabled(autostartDir));
        QVERIFY(pet::setLoginStart(true, "/opt/My Apps/agent-pet", nullptr, autostartDir));
        QVERIFY(pet::loginStartEnabled(autostartDir));
        QFile file(pet::loginEntryPath(autostartDir));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("Exec=\"/opt/My Apps/agent-pet\"\n"));
        QVERIFY(pet::setLoginStart(false, {}, nullptr, autostartDir));
        QVERIFY(!pet::loginStartEnabled(autostartDir));
        QVERIFY(pet::setLoginStart(false, {}, nullptr, autostartDir)); // Disabling twice is fine.
    }
};
QTEST_GUILESS_MAIN(StartupTests)
#include "startup_tests.moc"

#include "sessions/state.h"
#include "ipc/local.h"
#include <QTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QProcess>
#include <QDateTime>
#include <QElapsedTimer>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>

class EventTests : public QObject {
    Q_OBJECT
    const qint64 now = 1700000000000;
    pet::Event event(QString kind, int seq = 1) {
        return {"claude", "session", QString::number(seq), kind, "tool", {}, "/project", {}, now + seq};
    }
private slots:
    void replay() {
        QFile file(FIXTURE_PATH); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto rows = QJsonDocument::fromJson(file.readAll()).array(); QVERIFY(!rows.isEmpty());
        pet::Sessions state; int seq = 0;
        for (const auto &row : rows) {
            auto o = row.toObject(); const auto expected = o.take("expect").toString();
            o["version"] = 1; o["event_id"] = QString::number(++seq); o["timestamp_ms"] = now + seq;
            pet::Event e; QString error;
            QVERIFY2(pet::Event::parse(QJsonDocument(o).toJson(), e, error), qPrintable(error));
            QVERIFY(state.apply(e, now + seq)); QCOMPARE(state.aggregate(now + seq), expected);
        }
        QVERIFY(state.records().isEmpty());
    }
    void missedStartToolCompletion() {
        pet::Sessions state;
        QVERIFY(state.apply(event("tool_end"), now + 1));
        QCOMPARE(state.aggregate(now), "thinking");
    }
    void orderingAndRecovery() {
        pet::Sessions state;
        QVERIFY(state.apply(event("prompt", 3), now + 3));
        QVERIFY(!state.apply(event("prompt", 3), now + 4));
        QVERIFY(!state.apply(event("attention", 2), now + 4));
        QVERIFY(state.apply(event("turn_finished", 4), now + 4));
        auto late = event("tool_start", 5); late.timestamp = now + 4;
        QVERIFY(!state.apply(late, now + 5));
        QCOMPARE(state.aggregate(now + 5), "turn-finished");
        state.expire(now + 5000); QCOMPARE(state.aggregate(now + 5000), "idle");
        QVERIFY(state.apply(event("session_end", 6), now + 5000));
        QVERIFY(!state.apply(event("tool_end", 5), now + 5000));
        QVERIFY(state.apply(event("prompt", 7), now + 5000));
        state.expire(now + 5000 + pet::Sessions::expiryMs);
        QVERIFY(state.records().isEmpty()); QVERIFY(state.pending().isEmpty());
        QCOMPARE(state.aggregate(now), "idle");
        pet::Sessions restarted; QVERIFY(restarted.records().isEmpty()); QVERIFY(restarted.pending().isEmpty());
    }
    void attentionAndErrors() {
        pet::Sessions state;
        QVERIFY(state.apply(event("attention"), now + 1));
        auto alerts = state.pending(); QCOMPARE(alerts.size(), 1);
        state.dismiss(alerts[0].session, alerts[0].kind);
        QVERIFY(state.pending().isEmpty()); QCOMPARE(state.aggregate(now), "attention");
        QVERIFY(state.apply(event("attention", 2), now + 2));
        QVERIFY(state.apply(event("attention", 3), now + 3));
        QCOMPARE(state.pending()[0].count, 2);
        QVERIFY(state.apply(event("error", 4), now + 4));
        QCOMPARE(state.aggregate(now), "attention");
        QVERIFY(state.apply(event("tool_end", 5), now + 5));
        QCOMPARE(state.aggregate(now), "attention");
        QVERIFY(state.apply(event("prompt", 6), now + 6));
        QCOMPARE(state.aggregate(now), "thinking");
        QVERIFY(state.apply(event("error", 7), now + 7));
        QCOMPARE(state.aggregate(now), "error");
        state.expire(now + 5000); QCOMPARE(state.aggregate(now), "thinking");
    }
    void boundsAndValidation() {
        pet::Sessions state;
        for (int i = 0; i < 300; ++i) {
            auto e = event("attention", i + 1); e.session = QString::number(i);
            QCOMPARE(state.apply(e, now + 1000), i < pet::Sessions::maxSessions);
        }
        QCOMPARE(state.records().size(), pet::Sessions::maxSessions);
        QCOMPARE(state.pending().size(), pet::Sessions::maxAlerts);
        pet::Sessions tools;
        for (int i = 0; i < 150; ++i) {
            auto e = event("tool_start", i + 1); e.tool = QString::number(i);
            QCOMPARE(tools.apply(e, now + 1000), i < pet::Sessions::maxTools);
        }
        pet::Event e; QString error;
        QVERIFY(!pet::Event::parse(QByteArray(8193, 'x'), e, error));
        QVERIFY(!pet::Event::parse("[]", e, error));
        QVERIFY(!pet::Event::parse(R"({"version":2})", e, error));
        QVERIFY(!pet::Event::parse(R"({"version":1,"provider":"claude","session_id":"a","event_id":"b","kind":"prompt","timestamp_ms":1,"prompt":"private"})", e, error));
        auto future = event("prompt"); future.timestamp = now + 60001;
        QVERIFY(!tools.apply(future, now));
    }
    void transportAndCommands() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto previous = qgetenv("XDG_RUNTIME_DIR"); qputenv("XDG_RUNTIME_DIR", temp.path().toUtf8());
        auto run = [&](QStringList args, QByteArray data, bool closeInput = true) {
            QProcess process; process.setProgram(APP_PATH); process.setArguments(args);
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.remove("DISPLAY"); environment.remove("WAYLAND_DISPLAY");
            environment.insert("QT_QPA_PLATFORM", "nonexistent");
            process.setProcessEnvironment(environment);
            QElapsedTimer timer; timer.start(); process.start();
            if (!process.waitForStarted(1000)) return -99;
            process.write(data); if (closeInput) process.closeWriteChannel();
            if (!process.waitForFinished(1000)) { process.kill(); process.waitForFinished(); return -98; }
            if (!process.readAllStandardOutput().isEmpty() || timer.elapsed() >= 1000) return -97;
            if (args[0] == "hook" && !process.readAllStandardError().isEmpty()) return -96;
            return process.exitCode();
        };
        const QByteArray payload = R"({"version":1,"session_id":"s","kind":"prompt"})";
        QCOMPARE(run({"hook", "--provider", "claude"}, payload), 0);
        QCOMPARE(run({"emit", "--provider", "claude"}, payload), 1);
        QCOMPARE(run({"hook", "--provider", "claude"}, {}, false), 0);
        QCOMPARE(run({"hook", "--provider", "claude"}, QByteArray(9000, 'x')), 0);
        {
            pet::Receiver receiver; QString error; QVERIFY2(receiver.start(error), qPrintable(error));
            pet::Receiver duplicate; QVERIFY(!duplicate.start(error));
            int received = 0;
            receiver.received = [&](const pet::Event &e) { ++received; QCOMPARE(e.session, "s"); QVERIFY(!e.id.isEmpty()); };
            QCOMPARE(run({"emit", "--provider", "claude"}, payload), 0);
            QTRY_COMPARE(received, 1);
            // Fill the queue while this thread deliberately does not drain it.
            for (int i = 0; i < 20; ++i) QCOMPARE(run({"hook", "--provider", "claude"}, R"({"session_id":"s","hook_event_name":"UserPromptSubmit"})"), 0);
            QCOMPARE(run({"hook", "--provider", "claude"}, "malformed"), 0);
        }
        { pet::Receiver restarted; QString error; QVERIFY(restarted.start(error)); }
        const QString directory = temp.path() + "/agent-pet-" + QString::number(getuid());
        QVERIFY(chmod(QFile::encodeName(directory).constData(), 0755) == 0);
        { pet::Receiver unsafe; QString error; QVERIFY(!unsafe.start(error)); }
        if (previous.isNull()) qunsetenv("XDG_RUNTIME_DIR"); else qputenv("XDG_RUNTIME_DIR", previous);
    }
};
QTEST_GUILESS_MAIN(EventTests)
#include "event_tests.moc"

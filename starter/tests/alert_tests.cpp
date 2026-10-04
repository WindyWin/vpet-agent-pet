#include "sessions/alerts.h"
#include "providers/host.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class AlertTests : public QObject {
    Q_OBJECT
    const qint64 now = 1700000000000;
    qint64 seq = 0;
    pet::Event event(QString provider, QString session, QString kind, QString project = {}, QString reason = {}) {
        ++seq; return {provider, session, QString::number(seq), kind, {}, {}, project, {}, now + seq, reason};
    }
    bool apply(pet::Sessions &state, const pet::Event &e) { return state.apply(e, now + seq); }
private slots:
    void reasonValidation() {
        pet::Event e; QString error;
        QJsonObject o{{"version", 1}, {"provider", "claude"}, {"session_id", "s"}, {"event_id", "1"},
                      {"kind", "attention"}, {"timestamp_ms", 1700000000000.0}, {"reason", "approval"}};
        QVERIFY2(pet::Event::parse(QJsonDocument(o).toJson(), e, error), qPrintable(error)); QCOMPARE(e.reason, "approval");
        o["reason"] = "input"; QVERIFY(pet::Event::parse(QJsonDocument(o).toJson(), e, error));
        o["reason"] = "other"; QVERIFY(!pet::Event::parse(QJsonDocument(o).toJson(), e, error));
        o["reason"] = "approval"; o["kind"] = "error"; QVERIFY(!pet::Event::parse(QJsonDocument(o).toJson(), e, error));
    }
    void concurrentIdentityAndPriority() {
        pet::Sessions state; pet::AlertQueue queue;
        QVERIFY(apply(state, event("claude", "b72c1111", "turn_finished", "/work/abc-web")));
        QVERIFY(apply(state, event("claude", "b72c2222", "error", "/personal/abc-web")));
        QVERIFY(apply(state, event("codex", "b72c1111", "attention", "/work/api", "approval")));
        const auto pending = state.pending(); queue.sync(pending);
        QCOMPARE(pending.size(), 3); QCOMPARE(queue.more(), 2);
        auto text = pet::describe(*queue.current(), pending);
        QCOMPARE(text.title, "Needs approval"); QCOMPARE(text.label, "api · Codex · b72c"); QCOMPARE(text.tooltip, "/work/api");
        queue.next(); text = pet::describe(*queue.current(), pending);
        QCOMPARE(text.title, "Tool error"); QCOMPARE(text.label, "abc-web (personal) · Claude Code · b72c2");
        queue.next(); text = pet::describe(*queue.current(), pending);
        QCOMPARE(text.title, "Turn finished"); QCOMPARE(text.label, "abc-web (work) · Claude Code · b72c1");
        queue.next(); QCOMPARE(queue.current()->kind, "attention"); // Next wraps around.
        QCOMPARE(state.aggregate(now), "attention");
    }
    void labelsWithoutAmbiguity() {
        QVector<pet::Alert> context{{"k1", "attention", "/a/web/", "claude", "abcdef", {}}, {"k2", "error", "/a/web", "claude", "x", {}}};
        QCOMPARE(pet::projectName("/a/web/", context), "web");
        QCOMPARE(pet::projectName({}, context), "Unknown project");
        QCOMPARE(pet::shortSessionId("claude", "ab", context), "ab");
        QCOMPARE(pet::shortSessionId("claude", "abcdef", context), "abcd");
        const auto text = pet::describe({"k", "attention", {}, "claude", "s1", {}}, {});
        QCOMPARE(text.title, "Needs attention"); QCOMPARE(text.tooltip, "Project path unavailable for this session");
        QCOMPARE(pet::describe({"k", "attention", "/p", "claude", "s1", "input"}, {}).title, "Needs input");
    }
    void newAlertsPreemptButNextIsKept() {
        pet::Sessions state; pet::AlertQueue queue;
        QVERIFY(apply(state, event("claude", "one", "turn_finished")));
        QVERIFY(apply(state, event("claude", "two", "turn_finished")));
        queue.sync(state.pending()); QCOMPARE(queue.current()->id, "one");
        queue.next(); queue.sync(state.pending()); QCOMPARE(queue.current()->id, "two");
        QVERIFY(apply(state, event("claude", "three", "turn_finished")));
        queue.sync(state.pending()); QCOMPARE(queue.current()->id, "two"); // Equal priority does not take over.
        QVERIFY(apply(state, event("codex", "four", "attention", {}, "input")));
        queue.sync(state.pending()); QCOMPARE(queue.current()->id, "four");
        queue.next(); queue.sync(state.pending()); QCOMPARE(queue.current()->id, "one");
        queue.sync(state.pending()); QCOMPARE(queue.current()->id, "one"); // Old attention does not snap back.
    }
    void dismissalKeepsAttention() {
        pet::Sessions state; pet::AlertQueue queue;
        QVERIFY(apply(state, event("claude", "s", "attention", "/p", "approval")));
        QVERIFY(apply(state, event("claude", "s", "attention", "/p", "approval")));
        queue.sync(state.pending());
        QCOMPARE(queue.current()->count, 2); QCOMPARE(pet::describe(*queue.current(), state.pending()).title, "Needs approval (×2)");
        queue.dismiss(state);
        QVERIFY(queue.empty()); QVERIFY(!queue.current());
        QCOMPARE(state.unresolvedAttention(), 1); QCOMPARE(state.aggregate(now), "attention");
        QVERIFY(apply(state, event("claude", "s", "prompt")));
        QCOMPARE(state.unresolvedAttention(), 0);
        QVERIFY(apply(state, event("claude", "s", "attention")));
        queue.sync(state.pending()); QCOMPARE(queue.current()->count, 1); // Fresh request, fresh alert.
    }
    void dismissShowsFollowingAlert() {
        pet::Sessions state; pet::AlertQueue queue;
        for (const auto &id : {"a", "b", "c"}) QVERIFY(apply(state, event("claude", id, "turn_finished")));
        queue.sync(state.pending()); queue.next(); QCOMPARE(queue.current()->id, "b");
        queue.dismiss(state); QCOMPARE(queue.current()->id, "c"); QCOMPARE(queue.more(), 1);
        queue.dismiss(state); QCOMPARE(queue.current()->id, "a"); QCOMPARE(queue.more(), 0);
    }
    void missedLateAndEndedSessions() {
        pet::Sessions state; pet::AlertQueue queue;
        auto late = event("claude", "s", "attention", "/p"); // No session_start was observed.
        QVERIFY(apply(state, event("claude", "s", "prompt", "/p")));
        QVERIFY(!state.apply(late, now + seq)); // Older than the session's newest event.
        QVERIFY(state.pending().isEmpty());
        QVERIFY(apply(state, event("claude", "s", "turn_finished")));
        queue.sync(state.pending()); QCOMPARE(pet::describe(*queue.current(), state.pending()).label, "p · Claude Code · s");
        QVERIFY(apply(state, event("claude", "s", "session_end")));
        queue.sync(state.pending()); QVERIFY(!queue.current());
        QVERIFY(apply(state, event("codex", "x", "error")));
        state.expire(now + seq + pet::Sessions::expiryMs);
        queue.sync(state.pending()); QVERIFY(queue.empty());
    }
    void reportsFadeRequestsStay() {
        pet::Sessions state;
        QVERIFY(apply(state, event("claude", "a", "turn_finished")));
        QVERIFY(apply(state, event("claude", "b", "error")));
        QVERIFY(apply(state, event("claude", "c", "attention", {}, "input")));
        state.expire(now + seq + pet::Sessions::finishedAlertMs);
        QCOMPARE(state.pending().size(), 2);
        state.expire(now + seq + pet::Sessions::errorAlertMs);
        QCOMPARE(state.pending().size(), 1); QCOMPARE(state.pending().first().kind, "attention");
        QCOMPARE(state.unresolvedAttention(), 1);
    }
    void hostFieldsAndSessionRows() {
        pet::Event e; QString error;
        QJsonObject o{{"version", 1}, {"provider", "claude"}, {"session_id", "s"}, {"event_id", "1"}, {"kind", "prompt"},
                      {"timestamp_ms", 1700000000000.0}, {"host", "konsole"}, {"host_pids", "12,7"}, {"host_window", "8388617"},
                      {"host_target", "org.kde.konsole-12|/Windows/1|/Sessions/3"}};
        QVERIFY2(pet::Event::parse(QJsonDocument(o).toJson(), e, error), qPrintable(error));
        QCOMPARE(e.host, "konsole"); QCOMPARE(e.hostPids, "12,7"); QCOMPARE(e.hostWindow, "8388617");
        o["host"] = "xterm"; QVERIFY(!pet::Event::parse(QJsonDocument(o).toJson(), e, error));
        o["host"] = "terminal"; o["host_pids"] = "1;rm"; QVERIFY(!pet::Event::parse(QJsonDocument(o).toJson(), e, error));
        o["host_pids"] = "12"; o["host_window"] = "0x1"; QVERIFY(!pet::Event::parse(QJsonDocument(o).toJson(), e, error));

        pet::Sessions state;
        auto hosted = [&](QString id, QString kind, QString host, QString parent = {}) {
            auto ev = event("claude", id, kind, "/work/" + id); ev.host = host; ev.parent = parent; return ev;
        };
        QVERIFY(apply(state, hosted("idle", "turn_finished", "vscode")));
        QVERIFY(apply(state, hosted("busy", "prompt", "herdr")));
        QVERIFY(apply(state, hosted("main", "prompt", "konsole")));
        QVERIFY(apply(state, hosted("helper", "attention", "konsole", "main")));
        state.expire(now + seq + 5000); // The finished reaction returns to idle.
        const auto rows = pet::sessionRows(state, now + seq + 5000);
        QCOMPARE(rows.size(), 3); // The subagent folds into its parent.
        QCOMPARE(rows[0].name, "main"); QCOMPARE(rows[0].status, "Needs attention");
        QCOMPARE(rows[0].detail, "Claude Code · main · Konsole · 1 subagent");
        QCOMPARE(rows[1].name, "busy"); QCOMPARE(rows[1].status, "Thinking"); QVERIFY(rows[1].detail.endsWith("herdr"));
        QCOMPARE(rows[2].name, "idle"); QCOMPARE(rows[2].status, "Idle"); QCOMPARE(rows[2].tooltip, "/work/idle");
        QCOMPARE(pet::sessionRows(state, now + seq + 5000 + 3 * 60000)[2].status, "Idle · 3 min");
    }
    void hostCaptureAndTargets() {
        QProcessEnvironment env;
        env.insert("KONSOLE_DBUS_SERVICE", "org.kde.konsole-42"); env.insert("KONSOLE_DBUS_WINDOW", "/Windows/1");
        env.insert("KONSOLE_DBUS_SESSION", "/Sessions/5"); env.insert("WINDOWID", "6291463");
        auto host = pet::hostContext(env, {300, 200, 100});
        QCOMPARE(host["host"].toString(), "konsole"); QCOMPARE(host["host_pids"].toString(), "300,200,100");
        QCOMPARE(host["host_window"].toString(), "6291463");
        pet::KonsoleTarget konsole;
        QVERIFY(pet::konsoleTarget(host["host_target"].toString(), konsole));
        QCOMPARE(konsole.service, "org.kde.konsole-42"); QCOMPARE(konsole.window, "/Windows/1"); QCOMPARE(konsole.session, 5);
        QVERIFY(!pet::konsoleTarget("org.kde.konsole-42|/Windows/1|/Sessions/5;x", konsole));
        env.insert("TMUX", "/tmp/tmux-1000/default,1234,0"); env.insert("TMUX_PANE", "%7");
        host = pet::hostContext(env, {300});
        QCOMPARE(host["host"].toString(), "tmux"); // The innermost multiplexer wins.
        auto commands = pet::hostCommands("tmux", host["host_target"].toString());
        QCOMPARE(commands.size(), 2);
        QCOMPARE(commands[1].arguments, (QStringList{"-S", "/tmp/tmux-1000/default", "select-pane", "-t", "%7"}));
        QVERIFY(pet::hostCommands("tmux", "/tmp/s|%7; rm").isEmpty());
        env.insert("HERDR_PANE_ID", "p_3"); env.insert("HERDR_TAB_ID", "t_2"); env.insert("HERDR_SOCKET_PATH", "/run/user/1/herdr.sock");
        host = pet::hostContext(env, {});
        QCOMPARE(host["host"].toString(), "herdr"); QVERIFY(!host.contains("host_pids"));
        commands = pet::hostCommands("herdr", host["host_target"].toString());
        QCOMPARE(commands.size(), 2);
        QCOMPARE(commands[0].arguments, (QStringList{"tab", "focus", "t_2"}));
        QCOMPARE(commands[1].arguments, (QStringList{"agent", "focus", "p_3"}));
        QCOMPARE(commands[1].environment.value("HERDR_SOCKET_PATH"), "/run/user/1/herdr.sock");
        QProcessEnvironment code; code.insert("TERM_PROGRAM", "vscode");
        QCOMPARE(pet::hostContext(code, {9})["host"].toString(), "vscode");
        QVERIFY(pet::hostContext({}, {}).isEmpty());
        QVERIFY(pet::processAncestors(QCoreApplication::applicationPid()).startsWith(QCoreApplication::applicationPid()));
    }
    void herdrAttachedClientIdentity() {
        QProcessEnvironment env;
        env.insert("HOME", "/home/test");
        QCOMPARE(pet::herdrClientSocket(env, {}), "/home/test/.config/herdr/herdr.sock");
        env.insert("HERDR_SESSION", "work");
        QCOMPARE(pet::herdrClientSocket(env, {}), "/home/test/.config/herdr/sessions/work/herdr.sock");
        env.insert("HERDR_SOCKET_PATH", "/tmp/custom.sock");
        QCOMPARE(pet::herdrClientSocket(env, {}), "/tmp/custom.sock");
        env.insert("XDG_CONFIG_HOME", "/config");
        QCOMPARE(pet::herdrClientSocket(env, {"--session", "other"}), "/config/herdr/sessions/other/herdr.sock");
        QCOMPARE(pet::herdrClientSocket(env, {"session", "attach", "work.2"}), "/config/herdr/sessions/work.2/herdr.sock");
        QCOMPARE(pet::herdrClientSocket(env, {"--session=default"}), "/config/herdr/herdr.sock");
        QVERIFY(pet::herdrClientSocket(env, {"server"}).isEmpty());
        QVERIFY(pet::herdrClientSocket(env, {"tab", "focus", "t_1"}).isEmpty());
        QVERIFY(pet::herdrClientSocket(env, {"--remote", "example"}).isEmpty());
        QVERIFY(pet::herdrClientSocket(env, {"--session", ".."}).isEmpty());
    }
    void chooseHostWindow() {
        const QVector<pet::HostWindow> windows{{11, 500, "notes — other — Visual Studio Code"},
                                               {12, 500, "main.cpp — abc-web — Visual Studio Code"},
                                               {21, 600, "~ : bash — Konsole"}, {22, 600, "abc-web : claude — Konsole"}};
        QCOMPARE(pet::chooseWindow({}, "900,500,1", "/work/abc-web", windows), 12ULL); // Title names the project.
        QCOMPARE(pet::chooseWindow({}, "900,500,1", "/work/unknown", windows), 11ULL);
        QCOMPARE(pet::chooseWindow("21", "900,600", "/work/abc-web", windows), 21ULL); // $WINDOWID wins.
        QCOMPARE(pet::chooseWindow("99", "900,600", "/work/abc-web", windows), 22ULL);
        QCOMPARE(pet::chooseWindow({}, "900", "/work/abc-web", windows), 0ULL);
    }
    void restartAndBounds() {
        pet::Sessions state;
        for (int i = 0; i < 100; ++i) QVERIFY(apply(state, event("claude", QString("s%1").arg(i), "turn_finished")));
        QCOMPARE(state.pending().size(), pet::Sessions::maxAlerts);
        pet::AlertQueue queue; queue.sync(state.pending()); QCOMPARE(queue.more(), pet::Sessions::maxAlerts - 1);
        pet::Sessions restarted; pet::AlertQueue fresh; fresh.sync(restarted.pending());
        QVERIFY(fresh.empty()); QVERIFY(!fresh.current()); QCOMPARE(restarted.unresolvedAttention(), 0);
    }
};
QTEST_MAIN(AlertTests)
#include "alert_tests.moc"

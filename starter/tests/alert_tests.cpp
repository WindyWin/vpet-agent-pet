#include "sessions/alerts.h"
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
        QVERIFY(apply(state, event("claude", "b72c1111", "turn_finished", "/work/fcis-web")));
        QVERIFY(apply(state, event("claude", "b72c2222", "error", "/personal/fcis-web")));
        QVERIFY(apply(state, event("codex", "b72c1111", "attention", "/work/api", "approval")));
        const auto pending = state.pending(); queue.sync(pending);
        QCOMPARE(pending.size(), 3); QCOMPARE(queue.more(), 2);
        auto text = pet::describe(*queue.current(), pending);
        QCOMPARE(text.title, "Needs approval"); QCOMPARE(text.label, "api · Codex · b72c"); QCOMPARE(text.tooltip, "/work/api");
        queue.next(); text = pet::describe(*queue.current(), pending);
        QCOMPARE(text.title, "Tool error"); QCOMPARE(text.label, "fcis-web (personal) · Claude Code · b72c2");
        queue.next(); text = pet::describe(*queue.current(), pending);
        QCOMPARE(text.title, "Turn finished"); QCOMPARE(text.label, "fcis-web (work) · Claude Code · b72c1");
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

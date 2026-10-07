#include "sessions/state.h"
#include "sessions/recap.h"
#include "ipc/local.h"
#include <QTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QProcess>
#include <QDateTime>
#include <QElapsedTimer>
#ifdef PET_TEST_POSIX
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ipc/session_store.h"
#endif

class EventTests : public QObject {
    Q_OBJECT
    const qint64 now = 1700000000000;
    pet::Event event(QString kind, int seq = 1) {
        return {"claude", "session", QString::number(seq), kind, "tool", {}, "/project", {}, now + seq};
    }
private slots:
    void waitingWireValidation() {
        QJsonObject o{{"version", 1}, {"provider", "claude"}, {"session_id", "s"}, {"event_id", "1"},
                      {"kind", "turn_finished"}, {"timestamp_ms", double(now)}, {"waiting", true}};
        pet::Event e; QString error;
        QVERIFY(pet::Event::parse(QJsonDocument(o).toJson(), e, error)); QVERIFY(e.waiting);
        for (const auto &value : {QJsonValue(false), QJsonValue(1), QJsonValue("true"), QJsonValue()}) {
            o["waiting"] = value; QVERIFY(!pet::Event::parse(QJsonDocument(o).toJson(), e, error));
        }
        o["waiting"] = true; o["tool_id"] = "tool";
        for (const auto *kind : {"session_start", "prompt", "tool_start", "tool_end", "attention", "error",
                                 "turn_failed", "interrupt", "session_end"}) {
            o["kind"] = kind; QVERIFY(!pet::Event::parse(QJsonDocument(o).toJson(), e, error));
        }
    }
    void waitingPreservesTurnAndRecap() {
        pet::Sessions sessions; pet::Recap recap; const QDate date(2026, 10, 7);
        QVERIFY(sessions.apply(event("prompt", 1), now + 1));
        QVERIFY(sessions.apply(event("tool_start", 2), now + 2));
        auto stop = event("turn_finished", 3); stop.waiting = true;
        QVERIFY(sessions.apply(stop, now + 3));
        QCOMPARE(sessions.aggregate(now + 3), QString("waiting"));
        QVERIFY(sessions.records().first().tools.isEmpty());
        QCOMPARE(sessions.records().first().reactionUntil, qint64(0));
        QCOMPARE(sessions.records().first().lastTurnMs, qint64(0));
        QVERIFY(!recap.record(stop, &sessions.records().first(), date));
        for (int i = 4; i < 8; ++i) {
            auto e = event(i == 4 ? "tool_end" : i == 5 ? "error" : "attention", i);
            if (i == 6 || i == 7) e.reason = "input";
            if (i == 5) e.tool.clear();
            QVERIFY(sessions.apply(e, now + i));
            QCOMPARE(sessions.aggregate(now + i), QString("waiting"));
            QVERIFY(sessions.pending().isEmpty());
            QVERIFY(!pet::failure(e, &sessions.records().first()));
            recap.record(e, &sessions.records().first(), date);
        }
        QCOMPARE(recap.day(date).errors, 0); QCOMPARE(recap.day(date).approvals, 0);
        stop = event("turn_finished", 8); stop.waiting = true;
        QVERIFY(sessions.apply(stop, now + 8));
        sessions.expire(now + 60000);
        QCOMPARE(sessions.aggregate(now + 60000), QString("waiting"));
        pet::Sessions restored;
        QVERIFY(restored.restore(sessions.checkpoint(), now + 60000, [](const pet::Session &) { return true; }));
        QCOMPARE(restored.aggregate(now + 60000), QString("waiting"));
        QCOMPARE(restored.records().first().turnStarted, now + 1);
        auto finish = event("turn_finished", 60001);
        QVERIFY(restored.apply(finish, now + 60001));
        QCOMPARE(restored.aggregate(now + 60001), QString("turn-finished"));
        QCOMPARE(restored.records().first().lastTurnMs, qint64(60000));
        QVERIFY(recap.record(finish, &restored.records().first(), date));
        QCOMPARE(recap.day(date).turns, 1); QCOMPARE(recap.day(date).longestTurnMs, qint64(60000));
        QCOMPARE(restored.pending().size(), 1);
        sessions.expire(now + 8 + pet::Sessions::expiryMs); QVERIFY(sessions.records().isEmpty());
    }
    void waitingTransitionsAndPriority() {
        for (const auto *kind : {"prompt", "tool_start", "interrupt", "turn_failed", "session_end", "attention"}) {
            for (const auto &reason : {QString(), QString("approval")}) {
                pet::Sessions s; auto stop = event("turn_finished", 1); stop.waiting = true;
                QVERIFY(s.apply(stop, now + 1));
                auto next = event(kind, 2); if (next.kind == "attention") next.reason = reason;
                QVERIFY(s.apply(next, now + 2)); QVERIFY(s.aggregate(now + 2) != "waiting");
                if (next.kind == "attention") QCOMPARE(s.unresolvedAttention(), 1);
            }
        }
        pet::Sessions s; auto stop = event("turn_finished", 1); stop.waiting = true;
        QVERIFY(s.apply(stop, now + 1));
        auto other = event("session_start", 2); other.session = "other";
        QVERIFY(s.apply(other, now + 2)); QCOMPARE(s.aggregate(now + 2), QString("waiting"));
        other = event("prompt", 3); other.session = "other";
        QVERIFY(s.apply(other, now + 3)); QCOMPARE(s.aggregate(now + 3), QString("thinking"));
    }
    void checkpointRestoresPendingToolsAndDeduplication() {
        pet::Sessions original;
        QVERIFY(original.apply(event("prompt", 1), now + 1));
        QVERIFY(original.apply(event("tool_start", 2), now + 2));
        auto request = event("attention", 3); request.reason = "approval";
        QVERIFY(original.apply(request, now + 3));
        pet::Sessions restored;
        QVERIFY(restored.restore(original.checkpoint(), now + 4, [](const pet::Session &) { return true; }));
        QCOMPARE(restored.unresolvedAttention(), 1);
        QCOMPARE(restored.pending().size(), 1);
        QCOMPARE(restored.pending().first().reason, QString("approval"));
        QVERIFY(!restored.apply(request, now + 4));
        QVERIFY(restored.apply(event("tool_end", 5), now + 5));
        QCOMPARE(restored.unresolvedAttention(), 0);
        QCOMPARE(restored.aggregate(now + 5), QString("thinking"));
        QVERIFY(restored.pending().isEmpty());
        QVERIFY(restored.apply(event("turn_finished", 6), now + 6));
        QCOMPARE(restored.records().first().lastTurnMs, qint64(5));
    }
    void checkpointFiltersDeadExpiredAndCorruptSessions() {
        pet::Sessions original, restored;
        QVERIFY(original.apply(event("attention"), now + 1));
        QVERIFY(restored.restore(original.checkpoint(), now + 2, [](const pet::Session &) { return false; }));
        QVERIFY(restored.records().isEmpty()); QVERIFY(restored.pending().isEmpty());
        QVERIFY(restored.restore(original.checkpoint(), now + pet::Sessions::expiryMs + 1,
                                  [](const pet::Session &) { return true; }));
        QVERIFY(restored.records().isEmpty());
        QVERIFY(!restored.restore(original.checkpoint().chopped(1), now + 2, [](const pet::Session &) { return true; }));
        QVERIFY(!restored.restore("invalid", now + 2, [](const pet::Session &) { return true; }));
        QVERIFY(restored.records().isEmpty());
        original.dismiss("claude" + QString(QChar(0x1f)) + "session", "attention");
        QVERIFY(restored.restore(original.checkpoint(), now + 2, [](const pet::Session &) { return true; }));
        QCOMPARE(restored.unresolvedAttention(), 1); QVERIFY(restored.pending().isEmpty());
    }
#if defined(PET_TEST_POSIX) && defined(Q_OS_LINUX)
    void checkpointChecksRealAgentProcess() {
        QTemporaryDir dir;
        // Give a harmless child the same executable name as an agent.
        const auto executable = dir.filePath("claude");
        QVERIFY(QFile::copy("/bin/sleep", executable));
        QProcess child; child.start(executable, {"30"}); QVERIFY(child.waitForStarted());
        auto e = event("attention"); e.host = "terminal"; e.hostPids = QString::number(child.processId());
        pet::Sessions original, restored;
        QVERIFY(original.apply(e, now + 1));
        const auto path = dir.filePath("sessions.json");
        QVERIFY(pet::saveSessions(path, original));
        QVERIFY(pet::loadSessions(path, restored, now + 2));
        QCOMPARE(restored.unresolvedAttention(), 1);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        auto object = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        auto processes = object["processes"].toObject();
        processes["claude" + QString(QChar(0x1f)) + "session"] = "different-boot-or-start-time";
        object["processes"] = processes;
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(object).toJson()); file.close();
        QVERIFY(pet::loadSessions(path, restored, now + 2));
        QVERIFY(restored.records().isEmpty());
        QVERIFY(pet::saveSessions(path, original));
        child.terminate(); QVERIFY(child.waitForFinished());
        QVERIFY(pet::loadSessions(path, restored, now + 3));
        QVERIFY(restored.records().isEmpty()); QVERIFY(restored.pending().isEmpty());
        QVERIFY(!pet::saveSessions(dir.path(), original));
    }
#endif
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
    void answeredConfirmationClearsAttention() {
        auto tool = [&](QString kind, QString id, int seq) { auto e = event(kind, seq); e.tool = id; return e; };
        auto attention = [&](int seq) { auto e = event("attention", seq); e.tool.clear(); e.reason = "approval"; return e; };
        pet::Sessions state;
        // Approved: the tool runs and completes, then the turn ends.
        QVERIFY(state.apply(tool("tool_start", "t1", 1), now + 1));
        QVERIFY(state.apply(attention(2), now + 2)); QCOMPARE(state.aggregate(now + 2), "attention");
        QCOMPARE(state.pending().size(), 1);
        QVERIFY(state.apply(tool("tool_end", "t1", 3), now + 3)); QCOMPARE(state.aggregate(now + 3), "thinking");
        QVERIFY(state.pending().isEmpty());
        QVERIFY(state.apply(event("turn_finished", 4), now + 4)); state.expire(now + 5000); QCOMPARE(state.aggregate(now + 5000), "idle");
        // Rejected: no completion callback; the next observable event resolves it.
        QVERIFY(state.apply(event("prompt", 5), now + 6));
        QVERIFY(state.apply(tool("tool_start", "t2", 6), now + 7));
        QVERIFY(state.apply(attention(7), now + 8)); QCOMPARE(state.aggregate(now + 8), "attention");
        QVERIFY(state.apply(event("turn_finished", 8), now + 9)); QCOMPARE(state.aggregate(now + 9), "turn-finished");
        QVERIFY(state.pending().size() == 1); // finished report only
        // Rejected, then the agent continues with another tool.
        QVERIFY(state.apply(event("prompt", 9), now + 10));
        QVERIFY(state.apply(tool("tool_start", "t3", 10), now + 11));
        QVERIFY(state.apply(attention(11), now + 12));
        QVERIFY(state.apply(tool("tool_start", "t4", 12), now + 13)); QCOMPARE(state.aggregate(now + 13), "working");
        // Another session's attention is untouched by unrelated tool ends.
        pet::Sessions other;
        QVERIFY(other.apply(tool("tool_start", "a", 1), now + 1));
        QVERIFY(other.apply(attention(2), now + 2));
        QVERIFY(other.apply(tool("tool_end", "unrelated", 3), now + 3)); QCOMPARE(other.aggregate(now + 3), "attention");
    }
    void interruptReturnsToIdle() {
        pet::Sessions state;
        QVERIFY(state.apply(event("prompt", 1), now + 1));
        QVERIFY(state.apply(event("tool_start", 2), now + 2)); QCOMPARE(state.aggregate(now + 2), "working");
        QVERIFY(state.apply(event("interrupt", 3), now + 3)); QCOMPARE(state.aggregate(now + 3), "idle");
        QVERIFY(state.records().first().tools.isEmpty());
        // Late callbacks of the interrupted turn cannot revive it.
        QVERIFY(!state.apply(event("tool_start", 4), now + 4));
        QVERIFY(!state.apply(event("tool_end", 5), now + 5));
        QVERIFY(!state.apply(event("error", 6), now + 6));
        state.expire(now + 10000); QCOMPARE(state.aggregate(now + 10000), "idle");
        QVERIFY(state.pending().isEmpty());
        // A new prompt resumes normal tracking.
        QVERIFY(state.apply(event("prompt", 7), now + 7)); QCOMPARE(state.aggregate(now + 7), "thinking");
        QVERIFY(state.apply(event("tool_start", 8), now + 8)); QCOMPARE(state.aggregate(now + 8), "working");
    }
    void quotaStopStaysUntilWorkResumes() {
        auto failed = [&](int seq, QString reason) { auto e = event("turn_failed", seq); e.tool.clear(); e.reason = reason; return e; };
        for (const auto *resume : {"prompt", "tool_start", "turn_finished", "session_end"}) {
            pet::Sessions state;
            QVERIFY(state.apply(event("prompt", 1), now + 1));
            QVERIFY(state.apply(event("tool_start", 2), now + 2));
            auto request = event("attention", 3); request.reason = "approval";
            QVERIFY(state.apply(request, now + 3));
            QVERIFY(state.apply(failed(4, "limit"), now + 4));
            QCOMPARE(state.aggregate(now + 4), "exhausted");
            QVERIFY(state.records().first().tools.isEmpty()); QCOMPARE(state.unresolvedAttention(), 0);
            QCOMPARE(state.records().first().reason, "limit");
            // No timer, no late report clears it.
            state.expire(now + 20 * 60000); QCOMPARE(state.aggregate(now + 20 * 60000), "exhausted");
            QVERIFY(state.apply(event("tool_end", 5), now + 5)); QCOMPARE(state.aggregate(now + 5), "exhausted");
            auto toolless = event("error", 6); toolless.tool.clear();
            QVERIFY(state.apply(toolless, now + 6)); QCOMPARE(state.aggregate(now + 6), "exhausted");
            auto asking = event("attention", 7); asking.reason = "input";
            QVERIFY(state.apply(asking, now + 7)); QCOMPARE(state.aggregate(now + 7), "exhausted");
            QCOMPARE(state.unresolvedAttention(), 0);
            QVERIFY(state.apply(failed(8, "billing"), now + 8)); QCOMPARE(state.records().first().reason, "billing");
            // Auto-resume or a retry: the next sign of work clears it.
            QVERIFY(state.apply(event(resume, 9), now + 9));
            QVERIFY(state.records().isEmpty() || state.records().first().state != "exhausted");
            QVERIFY(state.records().isEmpty() || state.records().first().reason.isEmpty());
        }
    }
    void failedTurnReactsThenIdles() {
        pet::Sessions state;
        QVERIFY(state.apply(event("prompt", 1), now + 1));
        QVERIFY(state.apply(event("tool_start", 2), now + 2));
        auto failed = event("turn_failed", 3); failed.tool.clear();
        QVERIFY(state.apply(failed, now + 3)); QCOMPARE(state.aggregate(now + 3), "error");
        QVERIFY(state.records().first().tools.isEmpty());
        QCOMPARE(state.records().first().reason, "turn");
        // A tool error in the same session stays a separate alert.
        auto toolError = event("error", 4); toolError.tool.clear();
        QVERIFY(state.apply(toolError, now + 4));
        int errors = 0; for (const auto &a : state.pending()) errors += a.kind == "error";
        QCOMPARE(errors, 2);
        state.expire(now + 4 + 4000); QCOMPARE(state.aggregate(now + 4 + 4000), "idle");
        QVERIFY(state.records().first().reason.isEmpty());
        // Work after the failure is still tracked: nothing about it is stale.
        QVERIFY(state.apply(event("tool_start", 5), now + 5)); QCOMPARE(state.aggregate(now + 5), "working");
    }
    void exhaustedPriority() {
        pet::Sessions state;
        auto at = [&](QString session, QString kind, int seq, QString reason = {}) {
            auto e = event(kind, seq); e.session = session; e.tool.clear(); e.reason = reason; return e;
        };
        QVERIFY(state.apply(at("a", "turn_failed", 1, "limit"), now + 1));
        QVERIFY(state.apply(at("b", "error", 2), now + 2));
        QVERIFY(state.apply(at("c", "prompt", 3), now + 3));
        QCOMPARE(state.aggregate(now + 3), "exhausted");
        QVERIFY(state.apply(at("d", "attention", 4, "approval"), now + 4));
        QCOMPARE(state.aggregate(now + 4), "attention");
        // The state and its reason survive a restart.
        pet::Sessions restored;
        QVERIFY(restored.restore(state.checkpoint(), now + 5, [](const pet::Session &) { return true; }));
        QCOMPARE(restored.records().value(QString("claude") + QChar(0x1f) + "a").state, "exhausted");
        QCOMPARE(restored.records().value(QString("claude") + QChar(0x1f) + "a").reason, "limit");
    }
    void activityHold() {
        pet::Sessions state;
        auto tool = [&](QString kind, QString id, int seq) { auto e = event(kind, seq); e.tool = id; e.activity = "working"; return e; };
        QVERIFY(state.apply(event("prompt", 1), now + 1));
        QVERIFY(state.apply(tool("tool_start", "a", 2), now + 2));
        QVERIFY(state.apply(tool("tool_end", "a", 3), now + 3));
        QCOMPARE(state.aggregate(now), "working"); // A brief tool stays visible.
        state.expire(now + 3 + pet::Sessions::activityHoldMs - 1); QCOMPARE(state.aggregate(now), "working");
        QVERIFY(state.apply(tool("tool_start", "b", 1000), now + 1000)); // Next tool continues the stretch.
        QVERIFY(state.apply(tool("tool_end", "b", 1001), now + 1001));
        state.expire(now + 1001 + pet::Sessions::activityHoldMs); QCOMPARE(state.aggregate(now), "thinking");
        QVERIFY(state.apply(tool("tool_start", "c", 9000), now + 9000));
        QVERIFY(state.apply(tool("tool_end", "c", 9001), now + 9001));
        QVERIFY(state.apply(event("error", 9002), now + 9002)); // Toolless error resumes thinking, not the held tool.
        state.expire(now + 9002 + 4000); QCOMPARE(state.aggregate(now), "thinking");
        QVERIFY(state.apply(tool("tool_start", "d", 20000), now + 20000));
        QVERIFY(state.apply(tool("tool_end", "d", 20001), now + 20001));
        QVERIFY(state.apply(event("turn_finished", 20002), now + 20002));
        QCOMPARE(state.aggregate(now), "turn-finished");
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
        auto unrelated = event("tool_end", 5); unrelated.tool = "other"; // Not the tool awaiting an answer.
        QVERIFY(state.apply(unrelated, now + 5));
        QCOMPARE(state.aggregate(now), "attention");
        QVERIFY(state.apply(event("prompt", 6), now + 6));
        QCOMPARE(state.aggregate(now), "thinking");
        QVERIFY(state.apply(event("error", 7), now + 7));
        QCOMPARE(state.aggregate(now), "error");
        state.expire(now + 5000); QCOMPARE(state.aggregate(now), "thinking");
    }
    void turnLength() {
        pet::Sessions state;
        auto at = [&](QString kind, int seq, qint64 stamp) { auto e = event(kind, seq); e.timestamp = stamp; return e; };
        auto record = [&] { return state.records().first(); };
        // A turn runs from its prompt to its finish.
        QVERIFY(state.apply(at("prompt", 1, now), now)); QCOMPARE(record().turnStarted, now);
        QVERIFY(state.apply(at("turn_finished", 2, now + 90000), now + 90000));
        QCOMPARE(record().lastTurnMs, qint64(90000)); QCOMPARE(record().turnStarted, qint64(0));
        // A finish without a prompt seen, or after an interrupt, has no known length.
        QVERIFY(state.apply(at("turn_finished", 3, now + 90001), now + 90001)); QCOMPARE(record().lastTurnMs, qint64(0));
        QVERIFY(state.apply(at("prompt", 4, now + 90002), now + 90002));
        QVERIFY(state.apply(at("interrupt", 5, now + 90003), now + 90003)); QCOMPARE(record().turnStarted, qint64(0));
        QVERIFY(state.apply(at("prompt", 6, now + 90004), now + 90004));
        QVERIFY(state.apply(at("turn_finished", 7, now + 95004), now + 95004)); QCOMPARE(record().lastTurnMs, qint64(5000));
    }
    void dailyRecap() {
        pet::Sessions state; pet::Recap recap;
        const QDate today(2026, 10, 6), tomorrow(2026, 10, 7);
        int seq = 0;
        auto feed = [&](QString kind, QString session, QString project, qint64 at, QDate day, QString reason = {}) {
            pet::Event e{"claude", session, QString::number(++seq), kind, {}, {}, project, {}, at, reason};
            if (!state.apply(e, at)) return false;
            const auto it = state.records().find(QString("claude") + QChar(0x1f) + session);
            return recap.record(e, it == state.records().end() ? nullptr : &*it, day);
        };
        QCOMPARE(pet::Recap::summary(recap.day(today)), QString("No agent work yet today."));
        // Turns count per project folder, with the longest one kept.
        QVERIFY(!feed("prompt", "a", "/work/abc-web", now, today));
        QVERIFY(feed("turn_finished", "a", "/work/abc-web", now + 22 * 60000, today));
        QVERIFY(!feed("prompt", "a", {}, now + 22 * 60000 + 1, today));
        QVERIFY(feed("turn_finished", "a", {}, now + 23 * 60000, today));
        QVERIFY(feed("turn_finished", "b", "/work/vpet/", now + 23 * 60000 + 1, today));
        auto day = recap.day(today);
        QCOMPARE(day.turns, 3); QCOMPARE(day.longestTurnMs, qint64(22 * 60000));
        QCOMPARE(day.projects, (QMap<QString, int>{{"abc-web", 2}, {"vpet", 1}}));
        // An approval counts when asked, its wait when answered; questions for the user are not approvals.
        const qint64 asked = now + 24 * 60000;
        QVERIFY(feed("attention", "a", {}, asked, today, "approval"));
        QVERIFY(!feed("attention", "a", {}, asked + 1, today, "approval")); // Still the same request.
        QVERIFY(feed("prompt", "a", {}, asked + 11 * 60000, today)); // Answered by moving on.
        QVERIFY(!feed("attention", "b", {}, asked, today, "input"));
        QVERIFY(feed("attention", "c", "/work/abc-web", asked, today, "approval"));
        QVERIFY(!feed("session_end", "c", {}, asked + 60000, today)); // Ended without an answer: no wait.
        QVERIFY(feed("error", "b", {}, asked + 2, today));
        day = recap.day(today);
        QCOMPARE(day.approvals, 2); QCOMPARE(day.longWaits, 1); QCOMPARE(day.longestWaitMs, qint64(11 * 60000)); QCOMPARE(day.errors, 1);
        QCOMPARE(pet::Recap::summary(day), QString("Today: 3 turns across 2 projects · 1 error · 1 approval waited 10+ min · longest run 22 min"));
        QCOMPARE(pet::Recap::breakdown(day), QString("Today's recap\nabc-web · 2 turns\nvpet · 1 turn\n"
                                                     "1 error · 2 approvals, longest wait 11 min · longest run 22 min"));
        // Each local day has its own counters.
        QVERIFY(feed("turn_finished", "b", {}, asked + 3, tomorrow));
        QCOMPARE(pet::Recap::summary(recap.day(tomorrow)), QString("Today: 1 turn in vpet"));
        QCOMPARE(recap.day(today).turns, 3);
        // Only a few weeks are kept, oldest dropped first; a day older than all of them is not kept.
        for (int i = 0; i < pet::Recap::keepDays; ++i) QVERIFY(feed("error", "b", {}, asked + 10 + i, tomorrow.addDays(i + 1)));
        QCOMPARE(recap.days().size(), pet::Recap::keepDays); QCOMPARE(recap.days().first().date, tomorrow.addDays(1));
        QVERIFY(feed("error", "b", {}, asked + 100, today));
        QCOMPARE(recap.days().size(), pet::Recap::keepDays); QCOMPARE(recap.day(today).errors, 0);
    }
    void recapStore() {
        QTemporaryDir directory; const auto path = directory.path() + "/data/recap.json";
        pet::RecapStore store(path);
        QCOMPARE(store.load().days().size(), 0); // Missing.
        pet::Recap recap; pet::Sessions state;
        pet::Event e{"codex", "s", "1", "turn_finished", {}, {}, "/work/abc-web", {}, now, {}};
        QVERIFY(state.apply(e, now));
        QVERIFY(recap.record(e, &state.records().first(), QDate(2026, 10, 6)));
        QVERIFY(store.save(recap));
        const auto loaded = store.load();
        QCOMPARE(loaded.days().size(), 1); QCOMPARE(loaded.day(QDate(2026, 10, 6)).projects.value("abc-web"), 1);
        QCOMPARE(QJsonDocument(loaded.toJson()), QJsonDocument(recap.toJson()));
        // Nothing but counts, dates and folder names is written.
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll(); QVERIFY(!bytes.contains("/work")); QVERIFY(!bytes.contains("codex"));
        file.close();
        // An invalid file loads as empty.
        for (const QByteArray &bad : {QByteArray("not json"), QByteArray(R"({"version":2,"days":[]})"),
                                      QByteArray(R"({"version":1,"days":[{"date":"2026-13-01","turns":1,"errors":0,"approvals":0,"long_waits":0,"longest_turn_ms":0,"longest_wait_ms":0,"projects":{}}]})"),
                                      QByteArray(R"({"version":1,"days":[{"date":"2026-10-06","turns":-1,"errors":0,"approvals":0,"long_waits":0,"longest_turn_ms":0,"longest_wait_ms":0,"projects":{}}]})")}) {
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write(bad); file.close();
            QCOMPARE(store.load().days().size(), 0);
        }
        QVERIFY(!pet::RecapStore({}).save(recap));
    }
    void riskyFlag() {
        const QByteArray base = R"({"version":1,"provider":"claude","session_id":"a","event_id":"b","timestamp_ms":1,)";
        pet::Event e; QString error;
        QVERIFY2(pet::Event::parse(base + R"("kind":"tool_start","tool_id":"t","risky":true})", e, error), qPrintable(error));
        QVERIFY(e.risky);
        QVERIFY(pet::Event::parse(base + R"("kind":"tool_start","tool_id":"t","risky":false})", e, error)); QVERIFY(!e.risky);
        QVERIFY(pet::Event::parse(base + R"("kind":"tool_start","tool_id":"t"})", e, error)); QVERIFY(!e.risky);
        // Only a boolean, and only on a tool start.
        QVERIFY(!pet::Event::parse(base + R"("kind":"tool_start","tool_id":"t","risky":"yes"})", e, error));
        QVERIFY(!pet::Event::parse(base + R"("kind":"tool_start","tool_id":"t","risky":1})", e, error));
        QVERIFY(!pet::Event::parse(base + R"("kind":"prompt","risky":true})", e, error));
        QVERIFY(!pet::Event::parse(base + R"("kind":"tool_end","tool_id":"t","risky":false})", e, error));
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
#ifdef PET_TEST_NATIVE
    void receiverValidatesTransportData() {
        struct Transport : pet::platform::EventTransport {
            bool start(QString &) override { return true; }
        };
        auto transport = std::make_unique<Transport>();
        auto *source = transport.get();
        pet::Receiver receiver(std::move(transport));
        QString error; QVERIFY(receiver.start(error));
        int delivered = 0;
        receiver.received = [&](const pet::Event &event) {
            ++delivered; QCOMPARE(event.session, QString("fake"));
        };
        source->received("not json");
        source->received(R"({"version":1,"provider":"claude","session_id":"fake","event_id":"1","kind":"session_start","timestamp_ms":1700000000000})");
        source->received(R"({"version":1,"provider":"claude","session_id":"fake","event_id":"2","kind":"unknown","timestamp_ms":1700000000000})");
        QCOMPARE(delivered, 1);
    }
    void transportAndCommands() {
#ifdef PET_TEST_POSIX
        // Short, so the socket path fits sockaddr_un (104 bytes on macOS) under any TMPDIR.
        QTemporaryDir temp("/tmp/agent-pet-XXXXXX"); QVERIFY(temp.isValid());
#else
        QTemporaryDir temp; QVERIFY(temp.isValid()); // Selects a separate pipe, away from a running pet.
#endif
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
#ifdef PET_TEST_POSIX
        const QString directory = temp.path() + "/agent-pet-" + QString::number(getuid());
        QVERIFY(chmod(QFile::encodeName(directory).constData(), 0755) == 0);
        { pet::Receiver unsafe; QString error; QVERIFY(!unsafe.start(error)); }
#endif
        if (previous.isNull()) qunsetenv("XDG_RUNTIME_DIR"); else qputenv("XDG_RUNTIME_DIR", previous);
    }
#endif
};
QTEST_GUILESS_MAIN(EventTests)
#include "event_tests.moc"

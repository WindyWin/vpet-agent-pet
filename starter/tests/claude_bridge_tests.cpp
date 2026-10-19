// The Claude Code interaction bridge (docs/adr/0049-claude-interaction-hooks.md): the hook handlers against a fake relay,
// the opt-in registration, and an end to end run of the real `agent-pet claude-interact` binary (hooks and the detached
// session host it starts) against the real interaction service, fed the way Claude Code feeds its hooks.
#include "fake_bridge.h"
#include "interactions/host.h"
#include "platform/contracts/interaction_transport.h"
#include "providers/adapters.h"
#include "providers/claude_bridge.h"
#include "providers/claude_internal.h"
#include "providers/integrations.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

using namespace pet;
using namespace pet::claude;
using namespace pet::interactions;
using namespace pet::interactions::testing;

namespace {

QJsonObject questionInput(const QString &id = "toolu_1", const QString &question = "Which framework?", bool multi = false) {
    const QJsonArray options{QJsonObject{{"label", "React"}, {"description", "Component library"}}, QJsonObject{{"label", "Vue"}, {"description", "Progressive"}}};
    return {{"session_id", "sess-1"}, {"hook_event_name", "PreToolUse"}, {"tool_name", "AskUserQuestion"}, {"tool_use_id", id}, {"cwd", "/x/proj"},
            {"tool_input", QJsonObject{{"questions", QJsonArray{QJsonObject{{"question", question}, {"header", "Framework"}, {"options", options}, {"multiSelect", multi}}}}}}};
}

// Scripted stand-in for the host: records what a hook sends and the order of side effects.
class FakeRelay : public Relay {
public:
    QVector<QJsonObject> sent;
    QList<QJsonObject> replies;
    QStringList trace; // "send:<op>", "emit", "send:ack", in the order they happened.
    bool sendFails = false;
    bool send(const QJsonObject &message) override {
        if (sendFails) return false;
        sent.append(message);
        trace << (message.contains("op") ? "send:" + message.value("op").toString() : QString("send:ack"));
        return true;
    }
    bool receive(QJsonObject &message, int) override {
        if (replies.isEmpty()) return false;
        message = replies.takeFirst();
        return true;
    }
};

HookResult run(const QString &event, const QJsonObject &input, FakeRelay &relay, int waitMs = 20000) {
    return handleHook(event, waitMs, input, relay, [&](const HookResult &) { relay.trace << "emit"; });
}

}

class ClaudeBridgeTests : public QObject {
    Q_OBJECT
    QTemporaryDir runtime_, config_, home_;
    QVector<std::shared_ptr<QProcess>> processes_;

    // ---- Helpers for the end to end tests ----------------------------------------------------------------------------

    static QStringList hookArguments(const QString &event, int wait = 0) {
        QStringList args{"claude-interact", "--event", event};
        if (wait) args << "--wait" << QString::number(wait);
        return args << "--registration" << interactionRegistration;
    }
    std::shared_ptr<QProcess> hook(const QString &event, const QJsonObject &input, int wait = 0) {
        auto process = std::make_shared<QProcess>();
        process->start(APP_PATH, hookArguments(event, wait));
        process->write(QJsonDocument(input).toJson(QJsonDocument::Compact));
        process->closeWriteChannel();
        processes_.append(process);
        return process;
    }
    static QJsonObject base(const QString &event) {
        return {{"session_id", "sess-1"}, {"hook_event_name", event}, {"cwd", "/x/proj"}, {"transcript_path", "/nowhere.jsonl"}};
    }
    void writeSession(qint64 pid, const QString &status, const QString &sessionId = "sess-1") {
        QDir().mkpath(config_.path() + "/sessions");
        QFile file(config_.path() + "/sessions/" + QString::number(pid) + ".json");
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(QJsonObject{{"pid", double(pid)}, {"sessionId", sessionId}, {"cwd", "/x/proj"}, {"startedAt", 1700000000000.0},
                                             {"status", status}, {"messagingSocketPath", "/never/read"}}).toJson());
    }
    static bool finished(const std::shared_ptr<QProcess> &p) { return p->state() == QProcess::NotRunning; }

private slots:
    void initTestCase() {
        QVERIFY(runtime_.isValid() && config_.isValid() && home_.isValid());
        qputenv("XDG_RUNTIME_DIR", runtime_.path().toUtf8());
        qputenv("CLAUDE_CONFIG_DIR", config_.path().toUtf8());
        qputenv("AGENT_PET_INTERACTION_ENDPOINT", platform::interactionEndpoint("claude-e2e").toUtf8());
    }

    // ---- Hook handlers -----------------------------------------------------------------------------------------------

    void parsesOneSingleSelectQuestion() {
        Question q;
        QVERIFY(parseQuestion(questionInput(), q));
        QCOMPARE(q.toolUseId, QString("toolu_1"));
        QCOMPARE(q.text, QString("Which framework?"));
        QCOMPARE(q.labels, (QStringList{"React", "Vue"}));
        QVERIFY(!parseQuestion(questionInput("toolu_1", "Which?", true), q)); // Multi-select stays in the terminal.
        QVERIFY(!parseQuestion(questionInput(""), q));                          // No tool_use_id.
        auto other = questionInput();
        other["tool_name"] = "Bash";
        QVERIFY(!parseQuestion(other, q));
        auto two = questionInput();
        auto tool = two["tool_input"].toObject();
        auto list = tool["questions"].toArray();
        list.append(list.first());
        tool["questions"] = list;
        two["tool_input"] = tool;
        QVERIFY(!parseQuestion(two, q)); // Several questions in one call.
        auto empty = questionInput();
        empty["tool_input"] = QJsonObject{};
        QVERIFY(!parseQuestion(empty, q));
        QVERIFY(!parseQuestion(questionInput("toolu_1", QString(13000, 'q')), q)); // Never shortened, so left to the terminal.
    }

    void answerJsonIsExactlyWhatClaudeCodeExpects() {
        Question q;
        QVERIFY(parseQuestion(questionInput(), q));
        const auto out = questionAnswerJson(q, "Vue");
        QVERIFY(out.endsWith("\n"));
        const auto doc = QJsonDocument::fromJson(out).object();
        QCOMPARE(doc.keys(), QStringList{"hookSpecificOutput"});
        const auto decision = doc["hookSpecificOutput"].toObject();
        QCOMPARE(decision.keys(), (QStringList{"hookEventName", "permissionDecision", "updatedInput"}));
        QCOMPARE(decision["hookEventName"].toString(), QString("PreToolUse"));
        QCOMPARE(decision["permissionDecision"].toString(), QString("allow"));
        const auto updated = decision["updatedInput"].toObject();
        QCOMPARE(updated["questions"], questionInput()["tool_input"].toObject()["questions"]); // The same questions, untouched.
        QCOMPARE(updated["answers"].toObject(), (QJsonObject{{"Which framework?", "Vue"}}));
        QCOMPARE(updated.keys(), (QStringList{"answers", "questions"}));
    }

    void anAnsweredQuestionIsPrintedBeforeTheHostIsToldItWas() {
        FakeRelay relay;
        relay.replies << QJsonObject{{"answer", "React"}};
        const auto result = run("PreToolUse", questionInput(), relay, 15000);
        QCOMPARE(result.exitCode, 0);
        QVERIFY(result.err.isEmpty());
        QCOMPARE(QJsonDocument::fromJson(result.out).object()["hookSpecificOutput"].toObject()["updatedInput"].toObject()["answers"].toObject(),
                 (QJsonObject{{"Which framework?", "React"}}));
        QCOMPARE(relay.trace, (QStringList{"send:ask", "emit", "send:ack"}));
        const auto ask = relay.sent.first();
        QCOMPARE(ask["id"].toString(), QString("toolu_1"));
        QCOMPARE(ask["question"].toString(), QString("Which framework?"));
        QCOMPARE(ask["options"].toArray(), (QJsonArray{"React", "Vue"}));
        QCOMPARE(ask["wait_ms"].toInt(), 15000);
    }

    void aQuestionThatIsNotAnsweredPrintsNothingSoTheTerminalDialogAppears() {
        for (const auto &reply : {QJsonObject{{"timeout", true}}, QJsonObject{{"unavailable", true}}, QJsonObject{{"answer", ""}}, QJsonObject{}}) {
            FakeRelay relay;
            relay.replies << reply;
            const auto result = run("PreToolUse", questionInput(), relay);
            QCOMPARE(result.exitCode, 0);
            QVERIFY2(result.out.isEmpty() && result.err.isEmpty(), qPrintable(QJsonDocument(reply).toJson()));
            QVERIFY(!relay.trace.contains("emit"));
        }
        FakeRelay silent; // The host went away mid-wait.
        QVERIFY(run("PreToolUse", questionInput(), silent).out.isEmpty());
        FakeRelay broken;
        broken.sendFails = true;
        const auto result = run("PreToolUse", questionInput(), broken);
        QVERIFY(result.out.isEmpty() && result.err.isEmpty() && result.exitCode == 0);
        FakeRelay unsupported; // Not our kind of question: the relay is never even asked.
        QVERIFY(run("PreToolUse", questionInput("toolu_1", "Which?", true), unsupported).out.isEmpty());
        QVERIFY(unsupported.sent.isEmpty());
    }

    void promptsAndRepliesGoToTheHostAndNothingIsPrinted() {
        FakeRelay relay;
        relay.replies << QJsonObject{{"ok", true}};
        auto input = base("UserPromptSubmit");
        input["prompt"] = "add tests";
        const auto result = run("UserPromptSubmit", input, relay);
        QCOMPARE(result.exitCode, 0);
        QVERIFY(result.out.isEmpty() && result.err.isEmpty());
        QCOMPARE(relay.sent.first(), (QJsonObject{{"op", "prompt"}, {"text", "add tests"}}));

        // Text Claude Code injects itself (our own wake, task notices) is a busy signal, never a message.
        for (const auto &injected : {wakeText("run it"), QString("<task-notification>\n<summary>Stop hook feedback</summary>\n</task-notification>")}) {
            FakeRelay woken;
            woken.replies << QJsonObject{{"ok", true}};
            input["prompt"] = injected;
            run("UserPromptSubmit", input, woken);
            QCOMPARE(woken.sent.first(), (QJsonObject{{"op", "prompt"}, {"text", ""}}));
        }
        input["prompt"] = "add tests";
        FakeRelay big;
        big.replies << QJsonObject{{"ok", true}};
        input["prompt"] = QString(40000, 'x');
        run("UserPromptSubmit", input, big);
        const auto text = big.sent.first()["text"].toString();
        QVERIFY(text.toUtf8().size() <= 12 * 1024 && text.endsWith(QChar(0x2026)));
    }

    void stopReportsTheReplyThenWaitsAndAFollowUpWakesWithExitCodeTwo() {
        FakeRelay relay;
        relay.replies << QJsonObject{{"ok", true}} << QJsonObject{{"wake", "run the tests"}};
        auto input = base("Stop");
        input["last_assistant_message"] = "Done.";
        const auto result = run("Stop", input, relay);
        QCOMPARE(result.exitCode, 2);
        QVERIFY(result.out.isEmpty());
        QCOMPARE(result.err, wakeText("run the tests").toUtf8());
        QVERIFY(result.err.contains("run the tests") && result.err.contains("Agent Pet"));
        QCOMPARE(relay.trace, (QStringList{"send:stop", "send:wait", "emit", "send:ack"}));
        QCOMPARE(relay.sent[0], (QJsonObject{{"op", "stop"}, {"text", "Done."}}));
        QCOMPARE(relay.sent[1], (QJsonObject{{"op", "wait"}, {"start", false}}));
    }

    void aReleasedOrOrphanedWaiterExitsQuietly() {
        for (const auto &script : {QList<QJsonObject>{QJsonObject{{"release", true}}}, QList<QJsonObject>{}, QList<QJsonObject>{QJsonObject{{"wake", ""}}}}) {
            FakeRelay relay;
            relay.replies = script;
            const auto result = run("SessionStart", base("SessionStart"), relay);
            QCOMPARE(result.exitCode, 0);
            QVERIFY(result.out.isEmpty() && result.err.isEmpty());
            QCOMPARE(relay.sent.first(), (QJsonObject{{"op", "wait"}, {"start", true}}));
        }
    }

    void sessionEndTellsTheHostAndAGarbledHostIsHarmless() {
        FakeRelay relay;
        relay.replies << QJsonObject{{"ok", true}};
        QCOMPARE(run("SessionEnd", base("SessionEnd"), relay).exitCode, 0);
        QCOMPARE(relay.sent.first(), (QJsonObject{{"op", "end"}}));
        FakeRelay garbled;
        garbled.replies << QJsonObject{{"wake", 7}} << QJsonObject{{"answer", 7}};
        QVERIFY(run("SessionStart", base("SessionStart"), garbled).err.isEmpty());
        QVERIFY(run("PreToolUse", questionInput(), garbled).out.isEmpty());
    }

    // ---- Registration ------------------------------------------------------------------------------------------------

    void registrationAddsOnlyItsOwnMarkedEntries() {
        const QJsonObject foreign{{"type", "command"}, {"command", "/usr/bin/true unrelated"}, {"timeout", 5}};
        const QJsonObject statusHook{{"type", "command"}, {"command", "'/opt/agent-pet' hook --provider claude --registration agent-pet-v1"}, {"timeout", 1}};
        const QJsonObject input{{"model", "keep"}, {"hooks", QJsonObject{{"Stop", QJsonArray{QJsonObject{{"hooks", QJsonArray{foreign, statusHook}}}}},
                                                                         {"PreToolUse", QJsonArray{QJsonObject{{"matcher", "Bash"}, {"hooks", QJsonArray{foreign}}}}}}}};
        QJsonObject output;
        int owned = -1;
        QString error;
        QVERIFY2(mergeInteractions(input, "/opt/agent pet's/agent-pet", true, 7, output, owned, error), qPrintable(error));
        QCOMPARE(owned, 0);
        QCOMPARE(output["model"].toString(), QString("keep"));
        const auto hooks = output["hooks"].toObject();
        QCOMPARE(hooks.keys(), (QStringList{"PreToolUse", "SessionEnd", "SessionStart", "Stop", "UserPromptSubmit"}));
        // Foreign and status handlers keep their place and the group they shared.
        QCOMPARE(hooks["Stop"].toArray().first().toObject()["hooks"].toArray(), (QJsonArray{foreign, statusHook}));
        QCOMPARE(hooks["PreToolUse"].toArray().first().toObject()["hooks"].toArray(), (QJsonArray{foreign}));
        QCOMPARE(hooks["PreToolUse"].toArray().size(), 2);

        int ours = 0;
        for (const auto &event : hooks.keys())
            for (const auto &group : hooks[event].toArray())
                for (const auto &handler : group.toObject()["hooks"].toArray())
                    if (ownedInteractionHandler(handler.toObject())) {
                        ++ours;
                        const auto command = handler.toObject()["command"].toString();
                        QVERIFY(command.startsWith("'/opt/agent pet'\\''s/agent-pet' claude-interact --event " + event));
                        QVERIFY(command.endsWith(" --registration agent-pet-interact-v1"));
                    }
        QCOMPARE(ours, 5);
        const auto ask = hooks["PreToolUse"].toArray().last().toObject();
        QCOMPARE(ask["matcher"].toString(), QString("AskUserQuestion"));
        const auto askHandler = ask["hooks"].toArray().first().toObject();
        QVERIFY(askHandler["command"].toString().contains(" --wait 7 "));
        QCOMPARE(askHandler["timeout"].toInt(), 17);
        QVERIFY(!askHandler.contains("async") && !askHandler.contains("asyncRewake"));
        for (const auto &event : {"SessionStart", "Stop"}) {
            const auto handler = hooks[event].toArray().last().toObject()["hooks"].toArray().first().toObject();
            QVERIFY(handler["asyncRewake"].toBool());
            QVERIFY(handler["timeout"].toInt() >= 3600);
        }
        for (const auto &event : {"UserPromptSubmit", "SessionEnd"})
            QVERIFY(hooks[event].toArray().last().toObject()["hooks"].toArray().first().toObject()["async"].toBool());

        // The status registration never claims ours, and ours never claims the status hooks.
        QVERIFY(!ownedInteractionHandler(statusHook) && !ownedInteractionHandler(foreign));
        QVERIFY(!ownedHookHandler(interactionHandler("/opt/agent-pet", "Stop", 20), "claude"));
    }

    void enablingTwiceChangesNothingAndDisablingRemovesOnlyOurs() {
        const QJsonObject foreign{{"type", "command"}, {"command", "/usr/bin/true unrelated"}};
        const QJsonObject input{{"hooks", QJsonObject{{"SessionStart", QJsonArray{QJsonObject{{"hooks", QJsonArray{foreign}}}}},
                                                      {"Notification", QJsonArray{}}}}};
        QJsonObject once, twice, off;
        int owned = 0;
        QString error;
        QVERIFY(mergeInteractions(input, "/opt/agent-pet", true, 20, once, owned, error));
        QVERIFY(mergeInteractions(once, "/opt/agent-pet", true, 20, twice, owned, error));
        QCOMPARE(owned, 5);
        QCOMPARE(twice, once);
        QJsonObject moved;
        QVERIFY(mergeInteractions(once, "/elsewhere/agent-pet", true, 30, moved, owned, error)); // Re-enabling replaces, never duplicates.
        QCOMPARE(moved["hooks"].toObject()["Stop"].toArray().size(), 1);
        QVERIFY(mergeInteractions(once, "/opt/agent-pet", false, 20, off, owned, error));
        QCOMPARE(owned, 5);
        QCOMPARE(off, input); // Including the foreign group and the pre-existing empty array.
        QVERIFY(!mergeInteractions(input, "relative/agent-pet", true, 20, off, owned, error));
        QVERIFY(!mergeInteractions(input, "/opt/agent-pet", true, 0, off, owned, error));
        QVERIFY(!mergeInteractions(input, "/opt/agent-pet", true, 121, off, owned, error));
        QVERIFY(!mergeInteractions({{"hooks", QJsonArray{}}}, "/opt/agent-pet", true, 20, off, owned, error));
        QVERIFY(!mergeInteractions({{"hooks", QJsonObject{{"Stop", QJsonObject{}}}}}, "/opt/agent-pet", true, 20, off, owned, error));
    }

    void theIntegrationCommandKeepsTheTwoRegistrationsApart() {
        const auto config = home_.path() + "/settings.json";
        QJsonObject report;
        QString error;
        QVERIFY(runIntegration("preview", "claude", config, APP_PATH, report, error, IntegrationOptions{true, 9}));
        QVERIFY(!QFile::exists(config));
        QVERIFY(report["proposed_config"].toObject()["hooks"].toObject().contains("SessionStart"));
        QCOMPARE(report["expected_handlers"].toInt(), 5);
        QVERIFY(runIntegration("enable", "claude", config, APP_PATH, report, error));                        // The status hooks.
        QVERIFY(runIntegration("enable", "claude", config, APP_PATH, report, error, IntegrationOptions{true, 9}));
        QVERIFY(report["changed"].toBool());
        QFile file(config);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto both = file.readAll();
        file.close();
        QVERIFY(runIntegration("enable", "claude", config, APP_PATH, report, error, IntegrationOptions{true, 9}));
        QVERIFY(!report["changed"].toBool()); // Idempotent.
        QVERIFY(runIntegration("inspect", "claude", config, APP_PATH, report, error, IntegrationOptions{true, 9}));
        QCOMPARE(report["owned_handlers"].toInt(), 5);
        QCOMPARE(report["entries"].toObject().keys(), (QStringList{"PreToolUse", "SessionEnd", "SessionStart", "Stop", "UserPromptSubmit"}));
        QVERIFY(runIntegration("inspect", "claude", config, APP_PATH, report, error));
        QCOMPARE(report["owned_handlers"].toInt(), hookEvents("claude").size()); // The status inspect still counts its own only.
        QVERIFY(!report["entries"].toObject()["SessionStart"].toArray().first().toObject()["command"].toString().contains("claude-interact"));
        // A plain disable leaves the interaction hooks; the interactions disable leaves the status hooks.
        QVERIFY(runIntegration("disable", "claude", config, APP_PATH, report, error));
        QVERIFY(runIntegration("inspect", "claude", config, APP_PATH, report, error, IntegrationOptions{true, 9}));
        QCOMPARE(report["owned_handlers"].toInt(), 5);
        QVERIFY(runIntegration("enable", "claude", config, APP_PATH, report, error));
        QVERIFY(runIntegration("inspect", "claude", config, APP_PATH, report, error, IntegrationOptions{true, 9}));
        QCOMPARE(report["owned_handlers"].toInt(), 5); // Both registered again, each once.
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll().size(), both.size());
        file.close();
        QVERIFY(runIntegration("disable", "claude", config, APP_PATH, report, error, IntegrationOptions{true, 9}));
        QVERIFY(runIntegration("inspect", "claude", config, APP_PATH, report, error));
        QCOMPARE(report["owned_handlers"].toInt(), hookEvents("claude").size());
        QVERIFY(!runIntegration("enable", "codex", config, APP_PATH, report, error, IntegrationOptions{true, 9}));
        QVERIFY(error.contains("claude"));
        QVERIFY(!runIntegration("enable", "claude", config, APP_PATH, report, error, IntegrationOptions{true, 0}));
    }

    void silentOnEveryFailurePath() {
        const auto invoke = [&](const QStringList &args, const QByteArray &input, int *code = nullptr) {
            QProcess p;
            p.start(APP_PATH, args);
            p.write(input);
            p.closeWriteChannel();
            if (!p.waitForFinished(5000)) { p.kill(); p.waitForFinished(); QTest::qFail("hook hung", __FILE__, __LINE__); }
            if (code) *code = p.exitCode();
            return p.readAllStandardOutput() + p.readAllStandardError();
        };
        const auto good = QJsonDocument(base("Stop")).toJson(QJsonDocument::Compact);
        int code = -1;
        for (const auto &args : {hookArguments("Stop"), hookArguments("Nonsense"), QStringList{"claude-interact"}, QStringList{"claude-interact", "--bogus"},
                                 QStringList{"claude-interact", "host"}, QStringList{"claude-interact", "host", "--pid", "0"},
                                 QStringList{"claude-interact", "--event", "Stop", "--registration", "other"}}) {
            for (const auto &input : {QByteArray("not json"), QByteArray(), good, QByteArray("{\"session_id\":\"nope\"}")}) {
                code = -1;
                QCOMPARE(invoke(args, input, &code), QByteArray());
                QCOMPARE(code, 0);
            }
        }
        // A session with a file, but no pet listening: nothing is started and nothing is printed.
        writeSession(QCoreApplication::applicationPid(), "idle");
        QElapsedTimer clock;
        clock.start();
        QCOMPARE(invoke(hookArguments("Stop"), good, &code), QByteArray());
        QCOMPARE(code, 0);
        QVERIFY2(clock.elapsed() < 2000, "a hook with no pet must not hold up the agent");
        QVERIFY(!QFile::exists(detail::relayEndpoint("sess-1")));
        QFile::remove(config_.path() + "/sessions/" + QString::number(QCoreApplication::applicationPid()) + ".json");
    }

    // ---- End to end --------------------------------------------------------------------------------------------------

    void realHooksAndTheirHostAgainstTheRealService() {
        Service service;
        Host host(service, platform::createInteractionTransport("claude-e2e"));
        QString error;
        QVERIFY2(host.start(error), qPrintable(error));
        QStringList results;
        service.subscribe([&](const Change &change) {
            if (change.kind == Change::Kind::CommandResult) results << change.result.commandId + ":" + outcomeName(change.result.outcome) + ":" + change.result.reason;
        });
        const qint64 pid = QCoreApplication::applicationPid();
        const QString epoch = QString::number(pid) + ".1700000000000";
        const ConversationKey k{"claude", "sess-1", epoch};
        writeSession(pid, "idle");

        // SessionStart: the host starts, registers the conversation, and the armed waiter makes it ready.
        auto start = hook("SessionStart", base("SessionStart"));
        QTRY_VERIFY_WITH_TIMEOUT(service.find(k) && service.find(k)->ready, 10000);
        const auto *c = service.find(k);
        QCOMPARE(c->link, Conversation::Link::Connected);
        QCOMPARE(c->title, QString("proj"));
        QVERIFY(c->has(Messages | SingleChoice | CustomAnswer | FollowUp | IdempotentCommands));
        QVERIFY(!c->has(Reconcile));
        QVERIFY(!finished(start)); // Still waiting to wake the session.

        // UserPromptSubmit: a user message, and the session is busy.
        auto input = base("UserPromptSubmit");
        input["prompt"] = "which framework?";
        auto prompt = hook("UserPromptSubmit", input);
        QTRY_VERIFY_WITH_TIMEOUT(finished(prompt), 5000);
        QCOMPARE(prompt->exitCode(), 0);
        QVERIFY(prompt->readAllStandardOutput().isEmpty() && prompt->readAllStandardError().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(c->messages.size() == 1 && !c->ready, 5000);
        QCOMPARE(c->messages[0].role, Role::User);
        QCOMPARE(c->messages[0].text, QString("which framework?"));
        QCOMPARE(c->unreadCount(), 0);

        // A question answered from the pet: the hook prints the answer, the service sees it accepted.
        auto ask = hook("PreToolUse", questionInput("toolu_1"), 20);
        QTRY_VERIFY_WITH_TIMEOUT(c->requests.contains("toolu_1"), 10000);
        const auto request = c->requests.value("toolu_1");
        QCOMPARE(request.state, RequestState::Pending);
        QCOMPARE(request.question, QString("Which framework?"));
        QCOMPARE(request.options.size(), 2);
        QCOMPARE(request.options[1].label, QString("Vue"));
        QVERIFY(request.customAllowed);
        QVERIFY(request.expiresAt > 0);
        QCOMPARE(service.attentionCount(), 1);
        QVERIFY(!finished(ask));
        QCOMPARE(service.submit(answer(k, "cmd-1", "toolu_1", 1, request.options[1].id)).outcome, Outcome::Pending);
        QTRY_VERIFY_WITH_TIMEOUT(finished(ask), 10000);
        QCOMPARE(ask->exitCode(), 0);
        QVERIFY(ask->readAllStandardError().isEmpty());
        const auto printed = QJsonDocument::fromJson(ask->readAllStandardOutput()).object()["hookSpecificOutput"].toObject();
        QCOMPARE(printed["permissionDecision"].toString(), QString("allow"));
        QCOMPARE(printed["updatedInput"].toObject()["answers"].toObject(), (QJsonObject{{"Which framework?", "Vue"}}));
        QCOMPARE(printed["updatedInput"].toObject()["questions"], questionInput()["tool_input"].toObject()["questions"]);
        QTRY_VERIFY2_WITH_TIMEOUT(results.contains("cmd-1:accepted:"), qPrintable(results.join(",")), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(service.find(k)->requests.value("toolu_1").state, RequestState::Answered, 5000);

        // A free-text answer goes in as typed.
        auto typed = hook("PreToolUse", questionInput("toolu_2", "Name it?"), 20);
        QTRY_VERIFY_WITH_TIMEOUT(c->requests.contains("toolu_2"), 10000);
        QCOMPARE(service.submit(customAnswer(k, "cmd-2", "toolu_2", 1, "Pet Shop")).outcome, Outcome::Pending);
        QTRY_VERIFY_WITH_TIMEOUT(finished(typed), 10000);
        QCOMPARE(QJsonDocument::fromJson(typed->readAllStandardOutput()).object()["hookSpecificOutput"].toObject()["updatedInput"].toObject()["answers"].toObject(),
                 (QJsonObject{{"Name it?", "Pet Shop"}}));

        // No answer in time: the hook prints nothing (the terminal dialog opens) and the pet's card expires.
        auto late = hook("PreToolUse", questionInput("toolu_3", "Too slow?"), 1);
        QTRY_VERIFY_WITH_TIMEOUT(finished(late), 10000);
        QCOMPARE(late->exitCode(), 0);
        QVERIFY(late->readAllStandardOutput().isEmpty() && late->readAllStandardError().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(c->requests.value("toolu_3").state, RequestState::Expired, 5000);
        QCOMPARE(service.submit(answer(k, "cmd-late", "toolu_3", 1, "1")).outcome, Outcome::Rejected); // Too late, rejected by the service itself.

        // The terminal user interrupts while the hook waits: the card is cancelled.
        auto interrupted = hook("PreToolUse", questionInput("toolu_4", "Interrupted?"), 60);
        QTRY_VERIFY_WITH_TIMEOUT(c->requests.contains("toolu_4") && c->requests.value("toolu_4").state == RequestState::Pending, 10000);
        interrupted->kill();
        QTRY_COMPARE_WITH_TIMEOUT(c->requests.value("toolu_4").state, RequestState::Cancelled, 5000);

        // Stop: the reply is shared, the old waiter is released, and the new one makes the conversation ready again.
        input = base("Stop");
        input["last_assistant_message"] = "Use Vue.";
        auto stop = hook("Stop", input);
        QTRY_VERIFY_WITH_TIMEOUT(c->messages.size() == 2 && c->ready, 10000);
        QCOMPARE(c->messages[1].role, Role::Agent);
        QCOMPARE(c->messages[1].text, QString("Use Vue."));
        QCOMPARE(c->unreadCount(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(finished(start), 5000); // Released: exit 0, nothing printed.
        QCOMPARE(start->exitCode(), 0);
        QVERIFY(start->readAllStandardOutput().isEmpty() && start->readAllStandardError().isEmpty());
        QVERIFY(!finished(stop));

        // Claude Code says it is busy (something started a turn without a prompt hook): the follow-up is refused, not queued.
        writeSession(pid, "busy");
        QCOMPARE(service.submit(followUp(k, "cmd-busy", "too early")).outcome, Outcome::Pending);
        QTRY_VERIFY_WITH_TIMEOUT(results.contains("cmd-busy:rejected:not_ready"), 10000);
        QVERIFY(!finished(stop));
        QTRY_VERIFY_WITH_TIMEOUT(!c->ready, 5000);
        writeSession(pid, "idle");
        auto again = hook("Stop", input); // The next Stop re-arms.
        QTRY_VERIFY_WITH_TIMEOUT(c->ready, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(finished(stop), 5000);

        // A follow-up wakes the idle session: exit code 2 and the text on stderr, once.
        QCOMPARE(service.submit(followUp(k, "cmd-follow", "now run them")).outcome, Outcome::Pending);
        QTRY_VERIFY_WITH_TIMEOUT(finished(again), 10000);
        QCOMPARE(again->exitCode(), 2);
        QVERIFY(again->readAllStandardOutput().isEmpty());
        QCOMPARE(again->readAllStandardError(), wakeText("now run them").toUtf8());
        QTRY_VERIFY_WITH_TIMEOUT(results.contains("cmd-follow:accepted:"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(c->messages.size() == 4, 5000); // The re-arming Stop reported its reply too.
        QCOMPARE(c->messages[3].role, Role::User);
        QCOMPARE(c->messages[3].text, QString("now run them"));
        QVERIFY(!c->ready); // The woken turn is running.
        QCOMPARE(service.submit(followUp(k, "cmd-follow", "now run them")).outcome, Outcome::Accepted); // The recorded outcome.

        // SessionEnd: the conversation ends and the host goes.
        auto end = hook("SessionEnd", base("SessionEnd"));
        QTRY_VERIFY_WITH_TIMEOUT(finished(end), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(service.find(k)->link, Conversation::Link::Ended, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(detail::relayEndpoint("sess-1")), 5000);
        QFile::remove(config_.path() + "/sessions/" + QString::number(pid) + ".json");
        for (const auto &p : processes_) if (!finished(p)) p->kill();
    }

    // The host belongs to the Claude process: when it is gone, the conversation ends without any hook saying so.
    void theHostEndsTheConversationWhenClaudeCodeIsGone() {
        Service service;
        Host host(service, platform::createInteractionTransport("claude-e2e"));
        QString error;
        QVERIFY2(host.start(error), qPrintable(error));
        // The shell stands in for Claude Code: the hook runs as its child and the shell exits a moment later.
        QProcess shell;
        shell.start("/bin/sh", {"-c", QString("read go; \"%1\" claude-interact --event UserPromptSubmit --registration agent-pet-interact-v1; sleep 2").arg(APP_PATH)});
        QVERIFY(shell.waitForStarted());
        writeSession(shell.processId(), "idle", "sess-2");
        auto input = base("UserPromptSubmit");
        input["session_id"] = "sess-2";
        input["prompt"] = "hello";
        shell.write("go\n" + QJsonDocument(input).toJson(QJsonDocument::Compact));
        shell.closeWriteChannel();
        const ConversationKey k{"claude", "sess-2", QString::number(shell.processId()) + ".1700000000000"};
        QTRY_VERIFY_WITH_TIMEOUT(service.find(k) && service.find(k)->link == Conversation::Link::Connected, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(service.find(k)->messages.size() == 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(service.find(k)->link, Conversation::Link::Ended, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(detail::relayEndpoint("sess-2")), 5000);
        QFile::remove(config_.path() + "/sessions/" + QString::number(shell.processId()) + ".json");
        shell.waitForFinished(5000);
    }

    void cleanupTestCase() {
        for (const auto &p : processes_)
            if (!finished(p)) { p->kill(); p->waitForFinished(2000); }
    }
};

QTEST_GUILESS_MAIN(ClaudeBridgeTests)
#include "claude_bridge_tests.moc"

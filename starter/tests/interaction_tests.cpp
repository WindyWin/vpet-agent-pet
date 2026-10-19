// The interaction service (docs/design/agent-interactions-spec.md): framing, validation, the request and
// command state machines, arbitration between interfaces, delivery uncertainty, reconnect reconciliation
// and the bounded store. Everything runs in memory against a scriptable fake bridge and a hand-set clock.
#include "fake_bridge.h"
#include "interactions/frame.h"
#include <QTest>

using namespace pet::interactions;
using namespace pet::interactions::testing;

namespace {
// A service with a hand-set clock, one connected bridge and a recorder of every change.
struct Rig {
    qint64 now = 1700000000000;
    Service service{[this] { return now; }};
    FakeBridge bridge{service, 1};
    QVector<Change> changes;
    ConversationKey a = key("session-a");
    Rig() {
        service.subscribe([this](const Change &c) { changes.append(c); });
        bridge.connect();
        bridge.hello({a});
    }
    const Conversation &conv(const ConversationKey &k) const { return *service.find(k); }
    const Conversation &conv() const { return conv(a); }
    RequestState state(const QString &id, const ConversationKey &k) const { return conv(k).requests.value(id).state; }
    RequestState state(const QString &id) const { return state(id, a); }
    QStringList results() const {
        QStringList list;
        for (const auto &c : changes)
            if (c.kind == Change::Kind::CommandResult) list << c.result.commandId + ":" + outcomeName(c.result.outcome) + ":" + c.result.reason;
        return list;
    }
    void ask(const QString &id = "q1", int revision = 1, const QStringList &options = {"tests", "skip"}, bool custom = false) {
        bridge.request(a, id, revision, "Add tests before continuing?", options, custom);
    }
    CommandResult answer1(const QString &commandId = "c1", const QString &option = "tests", const QString &source = "desktop") {
        return service.submit(answer(a, commandId, "q1", 1, option, source));
    }
};
QString reasonOf(const CommandResult &r) { return r.reason; }
}

class InteractionTests : public QObject {
    Q_OBJECT
private slots:
    // ---- Framing ---------------------------------------------------------------------------------

    void framesRoundTripAndReassemble() {
        const auto one = encodeFrame("{\"a\":1}"), two = encodeFrame("{\"b\":2}");
        FrameDecoder decoder;
        const auto all = one + two;
        for (const char byte : all) QVERIFY(decoder.feed(QByteArray(1, byte))); // One byte at a time.
        QByteArray payload;
        QVERIFY(decoder.next(payload));
        QCOMPARE(payload, QByteArray("{\"a\":1}"));
        QVERIFY(decoder.next(payload));
        QCOMPARE(payload, QByteArray("{\"b\":2}"));
        QVERIFY(!decoder.next(payload));
    }
    void oversizedAndEmptyFramesPoisonTheStream() {
        FrameDecoder decoder;
        // 64 KiB + 1 declared; nothing after the header is ever buffered.
        QVERIFY(!decoder.feed(QByteArray::fromHex("00010001")));
        QVERIFY(decoder.failed());
        QCOMPARE(decoder.buffered(), 0);
        QVERIFY(!decoder.feed(encodeFrame("{}"))); // Stays failed.
        FrameDecoder empty;
        QVERIFY(!empty.feed(QByteArray::fromHex("00000000")));
        FrameDecoder exact;
        QVERIFY(exact.feed(encodeFrame(QByteArray(Limits::frameBytes, 'x'))));
        QByteArray payload;
        QVERIFY(exact.next(payload));
        QCOMPARE(payload.size(), Limits::frameBytes);
    }

    // ---- Records ---------------------------------------------------------------------------------

    void commandsValidateTheirShape() {
        const auto c = answer(key("s"), "cmd-1", "q", 3, "opt");
        Command parsed;
        QString error;
        QVERIFY(Command::fromJson(c.toJson(), parsed, error));
        QCOMPARE(parsed.canonical(), c.canonical());
        QCOMPARE(parsed.requestRevision, 3);
        auto both = c.toJson();
        both["payload"] = QJsonObject{{"option_id", "opt"}, {"text", "x"}};
        QVERIFY(!Command::fromJson(both, parsed, error)); // Exactly one answer.
        auto none = c.toJson();
        none["payload"] = QJsonObject{};
        QVERIFY(!Command::fromJson(none, parsed, error));
        auto action = c.toJson();
        action["action"] = "run_shell";
        QVERIFY(!Command::fromJson(action, parsed, error)); // No arbitrary actions.
        QVERIFY(!Command::fromJson(followUp(key("s"), "f", "  ").toJson(), parsed, error));
        // The source does not change what a repeated ID must carry.
        QCOMPARE(answer(key("s"), "x", "q", 1, "o", "desktop").canonical(), answer(key("s"), "x", "q", 1, "o", "telegram").canonical());
    }
    void requestsValidateTheirShape() {
        InputRequest request;
        QString error;
        QVERIFY(InputRequest::fromJson(FakeBridge::requestJson("q", 1, "?", {"a", "b"}), request, error));
        QVERIFY(!InputRequest::fromJson(FakeBridge::requestJson("q", 1, "?", {"a", "a"}), request, error)); // Duplicate IDs.
        QVERIFY(!InputRequest::fromJson(FakeBridge::requestJson("q", 1, "?", {}), request, error));        // Nothing to answer with.
        QVERIFY(InputRequest::fromJson(FakeBridge::requestJson("q", 1, "?", {}, true), request, error));  // Free text.
        QVERIFY(!InputRequest::fromJson(FakeBridge::requestJson("q", 0, "?", {"a"}), request, error));
        QStringList many;
        for (int i = 0; i < Limits::options + 1; ++i) many << QString("o%1").arg(i);
        QVERIFY(!InputRequest::fromJson(FakeBridge::requestJson("q", 1, "?", many), request, error));
        QCOMPARE(error, QString(reason::tooLarge));
        auto longLabel = FakeBridge::requestJson("q", 1, "?", {"a"});
        longLabel["options"] = QJsonArray{QJsonObject{{"id", "a"}, {"label", QString(Limits::labelChars + 1, 'x')}}};
        QVERIFY(!InputRequest::fromJson(longLabel, request, error));
        QCOMPARE(error, QString(reason::tooLarge));
        longLabel["options"] = QJsonArray{QJsonObject{{"id", "a"}, {"label", QString(Limits::labelChars, 'x')}}};
        QVERIFY(InputRequest::fromJson(longLabel, request, error));
        QVERIFY(!InputRequest::fromJson(FakeBridge::requestJson("q", 1, QString(Limits::textBytes + 1, 'x'), {"a"}), request, error));
        QString bad;
        QCOMPARE(capabilitiesFromNames({"messages", "follow_up"}, bad), unsigned(Messages | FollowUp));
        QCOMPARE(capabilitiesFromNames({"telepathy"}, bad), 0u);
        QVERIFY(!bad.isEmpty());
    }

    // ---- Handshake and routing -------------------------------------------------------------------

    void handshakeNegotiatesAndRegistersConversations() {
        Rig rig;
        const auto welcome = rig.bridge.lastOfType("welcome");
        QCOMPARE(welcome.value("resync").toArray().size(), 1);
        QCOMPARE(rig.conv().link, Conversation::Link::Connected);
        QVERIFY(rig.conv().ready);
        QVERIFY(rig.conv().has(FollowUp | SingleChoice));
        QCOMPARE(rig.conv().title, QString("api-web"));
    }
    void incompatibleOrMissingHelloIsRefused() {
        Service service;
        FakeBridge old(service, 1);
        old.connect();
        old.send({{"type", "hello"}, {"version", 99}, {"provider", "fake"}, {"generation", "g"}});
        QCOMPARE(old.lastError().value("code").toString(), QString("unsupported_version"));
        QCOMPARE(old.lastError().value("supported").toArray().first().toInt(), protocolVersion);
        old.message(key("s"), 1, "m", "hi");
        QCOMPARE(old.errorCodes().last(), QString("no_hello"));
        QVERIFY(service.keys().isEmpty());
        old.sendRaw("not json");
        QCOMPARE(old.errorCodes().last(), QString("invalid_frame"));
        old.sendRaw("[1,2]");
        QCOMPARE(old.errorCodes().last(), QString("invalid_frame"));
        old.send({{"type", "hello"}, {"versions", QJsonArray{2, 1}}, {"provider", "fake"}, {"generation", "g"}}); // Picks 1.
        QVERIFY(!old.lastOfType("welcome").isEmpty());
        old.send({{"type", "hello"}, {"provider", "fake"}, {"generation", "g"}});
        QCOMPARE(old.errorCodes().last(), QString("duplicate_hello"));
    }
    void aBridgeCanOnlyTouchItsOwnConversations() {
        Rig rig;
        FakeBridge other(rig.service, 2, "other");
        other.connect();
        other.hello({key("session-z", "e1", "other")});
        other.message(rig.a, 1, "m1", "injected"); // Not registered by this connection.
        QCOMPARE(other.errorCodes().last(), QString(reason::unknownConversation));
        QVERIFY(rig.conv().messages.isEmpty());
        other.send({{"type", "hello"}, {"provider", "other"}, {"generation", "g"}}); // Second hello.
        QCOMPARE(other.errorCodes().last(), QString("duplicate_hello"));
        other.send({{"type", "conversation_open"}, {"conversation", key("s2", "e1", "fake").toJson()}}); // Wrong provider.
        QCOMPARE(other.errorCodes().last(), QString("invalid_conversation"));
        rig.bridge.send({{"type", "conversation_open"}, {"conversation", key("s2").toJson()}, {"title", "second"}, {"ready", true},
                         {"capabilities", QJsonArray{"messages"}}});
        QVERIFY(rig.service.find(key("s2")));
        QVERIFY(!rig.bridge.lastOfType("resync").isEmpty());
    }

    // ---- Answers ---------------------------------------------------------------------------------

    void answeringReachesTheBridgeAndClosesTheRequest() {
        Rig rig;
        rig.bridge.message(rig.a, 1, "m1", "I updated the handler.");
        rig.ask();
        QCOMPARE(rig.service.attentionCount(), 1);
        const auto pending = rig.answer1();
        QCOMPARE(pending.outcome, Outcome::Pending);
        QCOMPARE(rig.state("q1"), RequestState::Submitting);
        QCOMPARE(rig.bridge.commands.size(), 1);
        const auto &sent = rig.bridge.commands[0];
        QCOMPARE(sent.action, Action::AnswerRequest);
        QCOMPARE(sent.optionId, QString("tests"));
        QCOMPARE(sent.requestRevision, 1);
        QCOMPARE(sent.conversation, rig.a);
        QCOMPARE(sent.source, QString("desktop"));
        QCOMPARE(rig.service.attentionCount(), 0); // Submitting is no longer waiting for the user.
        rig.bridge.accept(sent);
        QCOMPARE(rig.state("q1"), RequestState::Answered);
        QCOMPARE(rig.results(), QStringList{"c1:accepted:"});
        QCOMPARE(rig.service.attentionCount(), 0);
    }
    void customAndFreeTextAnswers() {
        Rig rig;
        rig.ask("q1", 1, {"tests"}, true);
        QCOMPARE(reasonOf(rig.service.submit(customAnswer(rig.a, "c0", "q1", 1, "something else"))), QString()); // Pending.
        rig.bridge.result("c0", "accepted");
        rig.bridge.request(rig.a, "q2", 1, "Name?", {}, true); // Free text.
        QCOMPARE(rig.service.submit(customAnswer(rig.a, "c1", "q2", 1, "Ada")).outcome, Outcome::Pending);
        rig.bridge.request(rig.a, "q3", 1, "Pick", {"x"}, false);
        QCOMPARE(reasonOf(rig.service.submit(customAnswer(rig.a, "c2", "q3", 1, "y"))), QString(reason::customNotAllowed));
        QCOMPARE(rig.state("q3"), RequestState::Pending);
    }
    void staleOrInvalidAnswersNeverReachTheBridge() {
        Rig rig;
        rig.ask();
        QCOMPARE(reasonOf(rig.service.submit(answer(rig.a, "c1", "q1", 2, "tests"))), QString(reason::staleRevision));
        QCOMPARE(reasonOf(rig.service.submit(answer(rig.a, "c2", "q1", 1, "bogus"))), QString(reason::invalidOption));
        QCOMPARE(reasonOf(rig.service.submit(answer(rig.a, "c3", "nope", 1, "tests"))), QString(reason::unknownRequest));
        QCOMPARE(reasonOf(rig.service.submit(answer(key("ghost"), "c4", "q1", 1, "tests"))), QString(reason::unknownConversation));
        auto both = answer(rig.a, "c5", "q1", 1, "tests");
        both.text = "also";
        QCOMPARE(reasonOf(rig.service.submit(both)), QString(reason::invalid));
        QCOMPARE(reasonOf(rig.service.submit(customAnswer(rig.a, "c6", "q1", 1, QString(Limits::textBytes + 1, 'x')))), QString(reason::tooLarge));
        QCOMPARE(reasonOf(rig.service.submit(answer(rig.a, "", "q1", 1, "tests"))), QString(reason::invalid));
        QVERIFY(rig.bridge.commands.isEmpty());
        QCOMPARE(rig.state("q1"), RequestState::Pending);
        // The question changes: the old revision is stale, the new one is answerable.
        rig.bridge.request(rig.a, "q1", 2, "Different question?", {"yes", "no"});
        QCOMPARE(rig.conv().requests.value("q1").question, QString("Different question?"));
        QCOMPARE(reasonOf(rig.service.submit(answer(rig.a, "c7", "q1", 1, "yes"))), QString(reason::staleRevision));
        QCOMPARE(rig.service.submit(answer(rig.a, "c8", "q1", 2, "yes")).outcome, Outcome::Pending);
        rig.bridge.request(rig.a, "q1", 1, "Old news", {"a"}); // A lower revision never regresses it.
        QCOMPARE(rig.conv().requests.value("q1").revision, 2);
    }
    void aSecondInterfaceCannotDoubleAnswer() {
        Rig rig;
        rig.ask();
        QCOMPARE(rig.answer1("desk-1", "tests", "desktop").outcome, Outcome::Pending);
        // The Telegram stand-in races the desktop: the request is already reserved.
        QCOMPARE(reasonOf(rig.answer1("tg-1", "skip", "telegram")), QString(reason::alreadySubmitting));
        QCOMPARE(rig.bridge.commands.size(), 1);
        rig.bridge.accept(rig.bridge.commands[0]);
        QCOMPARE(reasonOf(rig.answer1("tg-2", "skip", "telegram")), QString(reason::alreadyAnswered));
        QCOMPARE(rig.bridge.commands.size(), 1);
    }
    void bothInterfacesSeeEveryChange() {
        Rig rig;
        QStringList desktop, telegram;
        rig.service.subscribe([&](const Change &c) { desktop << (c.kind == Change::Kind::CommandResult ? "result" : "state"); });
        const int id = rig.service.subscribe([&](const Change &c) { telegram << (c.kind == Change::Kind::CommandResult ? "result" : "state"); });
        rig.ask();
        rig.answer1("c1", "tests", "telegram");
        rig.bridge.accept(rig.bridge.commands[0]);
        QCOMPARE(desktop, telegram);
        QVERIFY(desktop.contains("result"));
        rig.service.unsubscribe(id);
        telegram.clear();
        rig.bridge.message(rig.a, 1, "m", "later");
        QVERIFY(telegram.isEmpty());
        QVERIFY(!desktop.isEmpty());
    }
    void repeatedCommandIdsAreIdempotent() {
        Rig rig;
        rig.ask();
        const auto first = rig.answer1("c1", "tests", "desktop");
        QCOMPARE(first.outcome, Outcome::Pending);
        QCOMPARE(rig.answer1("c1", "tests", "telegram").outcome, Outcome::Pending); // Same payload, any source: the known state.
        QCOMPARE(rig.bridge.commands.size(), 1);
        rig.bridge.accept(rig.bridge.commands[0]);
        QCOMPARE(rig.answer1("c1", "tests").outcome, Outcome::Accepted); // The known outcome, nothing resent.
        QCOMPARE(reasonOf(rig.answer1("c1", "skip")), QString(reason::commandIdReused)); // Changed payload under the same ID.
        QCOMPARE(rig.bridge.commands.size(), 1);
    }
    void aDefinitiveRejectionReturnsTheRequestOnlyIfStillAnswerable() {
        Rig rig;
        rig.ask("q1");
        rig.ask("q2");
        rig.answer1("c1");
        rig.bridge.result("c1", "rejected", "model_busy", true);
        QCOMPARE(rig.state("q1"), RequestState::Pending);
        QCOMPARE(rig.answer1("c1b").outcome, Outcome::Pending); // Retry with a new command.
        rig.bridge.result("c1b", "rejected", reason::alreadyAnswered, false);
        QCOMPARE(rig.state("q1"), RequestState::Answered);
        QCOMPARE(rig.state("q2"), RequestState::Pending);
        QCOMPARE(rig.service.submit(answer(rig.a, "c2", "q2", 1, "skip")).outcome, Outcome::Pending);
        rig.bridge.result("c2", "rejected", "gone", false);
        QCOMPARE(rig.state("q2"), RequestState::Cancelled);
        QVERIFY(rig.results().contains("c1:rejected:model_busy"));
    }
    void aDefinitiveResultIsFinal() {
        Rig rig;
        rig.ask();
        rig.answer1();
        rig.bridge.accept(rig.bridge.commands[0]);
        rig.bridge.result("c1", "rejected", "late", true);
        QCOMPARE(rig.state("q1"), RequestState::Answered);
        QCOMPARE(rig.results().size(), 1);
        rig.bridge.result("never-sent", "accepted"); // Unknown IDs are ignored.
        rig.bridge.send({{"type", "command_result"}, {"command_id", "c1"}, {"outcome", "pending"}});
        QCOMPARE(rig.bridge.errorCodes().last(), QString("invalid_result"));
    }

    // ---- Delivery uncertainty --------------------------------------------------------------------

    void aTimeoutIsUnknownNotSuccessAndNeverResent() {
        Rig rig;
        rig.ask();
        rig.answer1();
        rig.now += rig.service.commandTimeoutMs - 1;
        rig.service.expire();
        QCOMPARE(rig.state("q1"), RequestState::Submitting);
        rig.now += 1;
        rig.service.expire();
        QCOMPARE(rig.state("q1"), RequestState::Unknown);
        QCOMPARE(rig.results(), QStringList{QString("c1:unknown:") + reason::timedOut});
        QCOMPARE(rig.service.attentionCount(), 1); // Still needs the user's eyes.
        rig.now += 60000;
        rig.service.expire();
        QCOMPARE(rig.bridge.commands.size(), 1); // Never blindly retried.
        QCOMPARE(reasonOf(rig.answer1("c2", "skip")), QString(reason::deliveryUnconfirmed)); // Nor answered again by hand.
        QCOMPARE(rig.answer1("c1").outcome, Outcome::Unknown); // The known state of the original.
        rig.bridge.accept(rig.bridge.commands[0]); // A late authoritative result still settles it.
        QCOMPARE(rig.state("q1"), RequestState::Answered);
        QCOMPARE(rig.answer1("c1").outcome, Outcome::Accepted);
    }
    void aLateRejectionReopensAnUnconfirmedRequest() {
        Rig rig;
        rig.ask();
        rig.answer1();
        rig.now += rig.service.commandTimeoutMs;
        rig.service.expire();
        rig.bridge.result("c1", "rejected", "not_taken", true);
        QCOMPARE(rig.state("q1"), RequestState::Pending);
        QCOMPARE(rig.answer1("c2", "skip").outcome, Outcome::Pending);
    }
    void bridgeReportedUnknownOutcome() {
        Rig rig;
        rig.ask();
        rig.answer1();
        rig.bridge.result("c1", "unknown", "agent_busy");
        QCOMPARE(rig.state("q1"), RequestState::Unknown);
        rig.bridge.result("c1", "unknown"); // Repeating it changes nothing.
        QCOMPARE(rig.results().size(), 1);
    }
    void disconnectingMidSubmitIsUnknownAndKeepsDrafts() {
        Rig rig;
        rig.ask();
        rig.service.setDraft(rig.a, "q1", "half typed");
        rig.service.setDraft(rig.a, "", "follow-up draft");
        rig.answer1();
        rig.bridge.disconnect();
        QCOMPARE(rig.conv().link, Conversation::Link::Disconnected);
        QCOMPARE(rig.state("q1"), RequestState::Unknown);
        QCOMPARE(rig.results(), QStringList{QString("c1:unknown:") + reason::disconnected});
        QCOMPARE(rig.service.draft(rig.a, "q1"), QString("half typed"));
        QCOMPARE(rig.service.draft(rig.a, ""), QString("follow-up draft"));
        QCOMPARE(reasonOf(rig.answer1("c2")), QString(reason::disconnected));
        QVERIFY(!rig.conv().ready);
    }
    void sendFailureRollsBackBecauseNothingWasWritten() {
        Rig rig;
        rig.ask();
        rig.bridge.up = false; // The link drops between validation and write.
        QCOMPARE(reasonOf(rig.answer1()), QString(reason::disconnected));
        QCOMPARE(rig.state("q1"), RequestState::Pending);
        QVERIFY(!rig.conv().commands.contains("c1"));
    }

    // ---- Reconnect and reconciliation ------------------------------------------------------------

    void reconnectReconcilesAnAnswerThatWasAccepted() {
        Rig rig;
        rig.ask();
        rig.answer1();
        rig.bridge.disconnect();
        FakeBridge again(rig.service, 2, "fake", "g2");
        again.connect();
        again.hello({rig.a});
        QCOMPARE(rig.conv().link, Conversation::Link::Connected);
        QCOMPARE(rig.conv().generation, QString("g2"));
        QCOMPARE(rig.conv().key.epoch, QString("e1")); // Same conversation, new generation.
        again.snapshot(rig.a, 0, {FakeBridge::requestJson("q1", 1, "Add tests before continuing?", {"tests", "skip"}, false, "answered")},
                       {}, QJsonArray{QJsonObject{{"command_id", "c1"}, {"outcome", "accepted"}}});
        QCOMPARE(rig.state("q1"), RequestState::Answered);
        QCOMPARE(rig.answer1("c1").outcome, Outcome::Accepted);
        QCOMPARE(rig.service.attentionCount(), 0);
    }
    void reconnectReopensAQuestionThatWasNeverDelivered() {
        Rig rig;
        rig.ask();
        rig.answer1();
        rig.bridge.disconnect();
        FakeBridge again(rig.service, 2, "fake", "g2");
        again.connect();
        again.hello({rig.a});
        again.snapshot(rig.a, 0, {FakeBridge::requestJson("q1", 1, "Add tests before continuing?", {"tests", "skip"})});
        QCOMPARE(rig.state("q1"), RequestState::Pending);
        QCOMPARE(rig.answer1("c2", "skip").outcome, Outcome::Pending);
        QCOMPARE(again.commands.size(), 1);
        QCOMPARE(rig.bridge.commands.size(), 1); // Nothing replayed to the new bridge on its own.
    }
    void aQuestionAnsweredInTheTerminalClosesEverywhere() {
        Rig rig;
        rig.ask("q1");
        rig.ask("q2");
        rig.bridge.requestState(rig.a, "q1", "answered");
        QCOMPARE(rig.state("q1"), RequestState::Answered);
        QCOMPARE(reasonOf(rig.answer1("c1")), QString(reason::alreadyAnswered));
        // After a reconnect the provider's snapshot is authoritative: q2 vanished, so it is closed.
        rig.bridge.disconnect();
        FakeBridge again(rig.service, 2, "fake", "g2");
        again.connect();
        again.hello({rig.a});
        again.snapshot(rig.a, 0, {});
        QCOMPARE(rig.state("q2"), RequestState::Expired);
        QCOMPARE(rig.service.attentionCount(), 0);
        again.requestState(rig.a, "q1", "cancelled"); // A terminal state never changes.
        QCOMPARE(rig.state("q1"), RequestState::Answered);
        again.request(rig.a, "q1", 5, "Reopened?", {"a"}); // Nor does it reopen.
        QCOMPARE(rig.state("q1"), RequestState::Answered);
    }
    void replacementAndCancellation() {
        Rig rig;
        rig.ask("q1");
        rig.ask("q2");
        rig.bridge.send({{"type", "request_state"}, {"conversation", rig.a.toJson()}, {"request_id", "q1"}, {"state", "replaced"}, {"replaced_by", "q2"}});
        QCOMPARE(rig.state("q1"), RequestState::Replaced);
        QCOMPARE(rig.conv().requests.value("q1").replacedBy, QString("q2"));
        QCOMPARE(reasonOf(rig.answer1()), QString(reason::requestClosed));
        rig.bridge.requestState(rig.a, "q2", "cancelled");
        QCOMPARE(reasonOf(rig.service.submit(answer(rig.a, "c2", "q2", 1, "tests"))), QString(reason::requestClosed));
        rig.bridge.send({{"type", "request_state"}, {"conversation", rig.a.toJson()}, {"request_id", "q2"}, {"state", "pending"}});
        QCOMPARE(rig.bridge.errorCodes().last(), QString("invalid_request_state"));
    }
    void authoritativeExpiry() {
        Rig rig;
        rig.bridge.send({{"type", "request"}, {"conversation", rig.a.toJson()},
                         {"request", [&] { auto r = FakeBridge::requestJson("q1", 1, "Soon?", {"a"}); r["expires_at"] = double(rig.now + 5000); return r; }()}});
        rig.now += 4999;
        QCOMPARE(rig.service.submit(answer(rig.a, "c1", "q1", 1, "a")).outcome, Outcome::Pending);
        rig.bridge.result("c1", "rejected", "busy", true);
        rig.now += 1;
        QCOMPARE(reasonOf(rig.answer1("c2", "a")), QString(reason::requestClosed)); // Expired at submit time.
        QCOMPARE(rig.state("q1"), RequestState::Expired);
        rig.ask("q2");
        rig.bridge.send({{"type", "request"}, {"conversation", rig.a.toJson()},
                         {"request", [&] { auto r = FakeBridge::requestJson("q3", 1, "Timed?", {"a"}); r["expires_at"] = double(rig.now + 100); return r; }()}});
        rig.now += 100;
        rig.service.expire();
        QCOMPARE(rig.state("q3"), RequestState::Expired);
        QCOMPARE(rig.state("q2"), RequestState::Pending);
    }
    void historyGapsAreReportedNotInvented() {
        Rig rig;
        rig.bridge.message(rig.a, 1, "m1", "one");
        rig.bridge.message(rig.a, 2, "m2", "two");
        QVERIFY(!rig.conv().historyGap);
        rig.bridge.message(rig.a, 5, "m5", "five"); // 3 and 4 never arrived.
        QVERIFY(rig.conv().historyGap);
        Rig other;
        other.bridge.message(other.a, 1, "m1", "one");
        other.bridge.disconnect();
        FakeBridge again(other.service, 2, "fake", "g2");
        again.connect();
        again.hello({other.a});
        again.snapshot(other.a, 9, {}, {QJsonObject{{"message_id", "m9"}, {"sequence", 9}, {"role", "agent"}, {"text", "nine"}}}, {}, 8);
        QVERIFY(other.conv().historyGap); // Replay began at 8, we had 1.
        Rig contiguous;
        contiguous.bridge.message(contiguous.a, 1, "m1", "one");
        contiguous.bridge.snapshot(contiguous.a, 3, {}, {QJsonObject{{"message_id", "m2"}, {"sequence", 2}, {"role", "agent"}, {"text", "two"}},
                                                         QJsonObject{{"message_id", "m3"}, {"sequence", 3}, {"role", "agent"}, {"text", "three"}}}, {}, 2);
        QVERIFY(!contiguous.conv().historyGap);
        QCOMPARE(contiguous.conv().messages.size(), 3);
        QCOMPARE(contiguous.conv().watermark, qint64(3));
    }
    void aBadSnapshotChangesNothing() {
        Rig rig;
        rig.ask();
        rig.bridge.message(rig.a, 1, "m1", "one");
        const auto before = rig.conv().messages.size();
        rig.bridge.snapshot(rig.a, 7, {FakeBridge::requestJson("q9", 1, "x", {})}, {QJsonObject{{"message_id", "m2"}, {"sequence", 2}, {"role", "agent"}, {"text", "two"}}});
        QCOMPARE(rig.bridge.errorCodes().last(), QString("invalid_snapshot"));
        QCOMPARE(rig.conv().messages.size(), before);
        QCOMPARE(rig.conv().watermark, qint64(1));
        QVERIFY(!rig.conv().requests.contains("q9"));
        QCOMPARE(rig.state("q1"), RequestState::Pending);
    }
    void newerBridgeTakesOverAndOldOneLosesTheConversation() {
        Rig rig;
        rig.ask();
        FakeBridge second(rig.service, 2, "fake", "g2");
        second.connect();
        second.hello({rig.a});
        rig.bridge.message(rig.a, 1, "m1", "from the stale connection");
        QCOMPARE(rig.bridge.errorCodes().last(), QString(reason::unknownConversation));
        QCOMPARE(rig.answer1().outcome, Outcome::Pending);
        QCOMPARE(second.commands.size(), 1);
        QVERIFY(rig.bridge.commands.isEmpty());
        rig.bridge.disconnect(); // The old connection closing must not disconnect the new owner.
        QCOMPARE(rig.conv().link, Conversation::Link::Connected);
        QCOMPARE(rig.state("q1"), RequestState::Submitting);
    }

    // ---- Follow-ups ------------------------------------------------------------------------------

    void followUpsNeedAReadyConversationWithNothingOpen() {
        Rig rig;
        rig.bridge.ready(rig.a, false);
        QCOMPARE(reasonOf(rig.service.submit(followUp(rig.a, "f1", "continue"))), QString(reason::notReady));
        rig.bridge.ready(rig.a, true);
        rig.ask();
        QCOMPARE(reasonOf(rig.service.submit(followUp(rig.a, "f2", "continue"))), QString(reason::awaitingAnswer)); // No interrupting a question.
        rig.bridge.requestState(rig.a, "q1", "answered");
        const auto result = rig.service.submit(followUp(rig.a, "f3", "continue"));
        QCOMPARE(result.outcome, Outcome::Pending);
        QVERIFY(!rig.conv().ready); // Reserved while it travels.
        QCOMPARE(reasonOf(rig.service.submit(followUp(rig.a, "f4", "more"))), QString(reason::alreadySubmitting));
        QCOMPARE(rig.bridge.commands.size(), 1);
        QCOMPARE(rig.bridge.commands[0].action, Action::SendFollowUp);
        QCOMPARE(rig.bridge.commands[0].text, QString("continue"));
        rig.bridge.accept(rig.bridge.commands[0]);
        QVERIFY(!rig.conv().ready); // Busy until the bridge reports the next turn is ready.
        QVERIFY(rig.conv().followUpInFlight.isEmpty());
        QCOMPARE(reasonOf(rig.service.submit(followUp(rig.a, "f5", "more"))), QString(reason::notReady));
        rig.bridge.ready(rig.a, true);
        QCOMPARE(rig.service.submit(followUp(rig.a, "f6", "more")).outcome, Outcome::Pending);
    }
    void aRejectedFollowUpFreesTheComposer() {
        Rig rig;
        QCOMPARE(rig.service.submit(followUp(rig.a, "f1", "go")).outcome, Outcome::Pending);
        rig.bridge.result("f1", "rejected", "empty_model");
        QVERIFY(rig.conv().ready);
        QCOMPARE(rig.service.submit(followUp(rig.a, "f2", "go again")).outcome, Outcome::Pending);
        rig.bridge.result("f2", "rejected", reason::notReady);
        QVERIFY(!rig.conv().ready); // The bridge said it is busy.
    }
    void anUnconfirmedFollowUpBlocksUntilReconciled() {
        Rig rig;
        rig.service.submit(followUp(rig.a, "f1", "go"));
        rig.now += rig.service.commandTimeoutMs;
        rig.service.expire();
        QCOMPARE(rig.results(), QStringList{QString("f1:unknown:") + reason::timedOut});
        rig.bridge.ready(rig.a, true);
        QCOMPARE(reasonOf(rig.service.submit(followUp(rig.a, "f2", "go"))), QString(reason::deliveryUnconfirmed));
        QCOMPARE(rig.bridge.commands.size(), 1);
        rig.bridge.snapshot(rig.a, 0, {}, {}, QJsonArray{QJsonObject{{"command_id", "f1"}, {"outcome", "accepted"}}});
        QCOMPARE(rig.service.submit(followUp(rig.a, "f1", "go")).outcome, Outcome::Accepted);
        // Without an outcome, a reconcile pass still lifts the block; the result stays unknown.
        rig.bridge.ready(rig.a, true);
        rig.service.submit(followUp(rig.a, "f3", "again"));
        rig.now += rig.service.commandTimeoutMs;
        rig.service.expire();
        rig.bridge.snapshot(rig.a, 0);
        QCOMPARE(rig.service.submit(followUp(rig.a, "f3", "again")).outcome, Outcome::Unknown);
        QCOMPARE(rig.service.submit(followUp(rig.a, "f4", "other")).outcome, Outcome::Pending);
    }
    void unadvertisedActionsAreRefused() {
        Service service;
        FakeBridge bridge(service, 1);
        bridge.capabilities = Messages | SingleChoice;
        bridge.connect();
        const auto k = key("s");
        bridge.hello({k});
        bridge.request(k, "q1", 1, "?", {"a"}, true);
        QCOMPARE(reasonOf(service.submit(followUp(k, "f1", "go"))), QString(reason::unsupported));
        QCOMPARE(reasonOf(service.submit(customAnswer(k, "c1", "q1", 1, "mine"))), QString(reason::unsupported));
        QCOMPARE(service.submit(answer(k, "c2", "q1", 1, "a")).outcome, Outcome::Pending);
        Service blind;
        FakeBridge nothing(blind, 1);
        nothing.capabilities = 0;
        nothing.connect();
        nothing.hello({k});
        nothing.message(k, 1, "m", "x");
        QCOMPARE(nothing.errorCodes().last(), QString(reason::unsupported));
        QVERIFY(blind.find(k)->messages.isEmpty());
        QCOMPARE(reasonOf(blind.submit(answer(k, "c3", "q1", 1, "a"))), QString(reason::unsupported));
    }

    // ---- Identity --------------------------------------------------------------------------------

    void identicalProjectNamesNeverCrossAnswersOrDrafts() {
        Rig rig;
        const auto b = key("session-b");
        rig.bridge.open({rig.a, b}, true, "api-web"); // Same title.
        rig.ask("q1");
        rig.bridge.request(b, "q1", 1, "Question for b", {"tests", "skip"}); // Same request ID, other session.
        rig.service.setDraft(rig.a, "q1", "draft for a");
        QCOMPARE(rig.service.draft(b, "q1"), QString());
        QCOMPARE(rig.service.submit(answer(b, "cb", "q1", 1, "skip")).outcome, Outcome::Pending);
        QCOMPARE(rig.bridge.commands.last().conversation, b);
        QCOMPARE(rig.state("q1", rig.a), RequestState::Pending);
        QCOMPARE(rig.state("q1", b), RequestState::Submitting);
        QCOMPARE(rig.conv(b).requests.value("q1").question, QString("Question for b"));
        QCOMPARE(rig.service.attentionCount(), 1);
        // A command ID belongs to one conversation.
        QCOMPARE(rig.service.submit(answer(rig.a, "cb", "q1", 1, "tests")).outcome, Outcome::Pending);
        rig.bridge.accept(rig.bridge.commands.last());
        QCOMPARE(rig.state("q1", rig.a), RequestState::Answered);
        QCOMPARE(rig.state("q1", b), RequestState::Submitting);
    }
    void aNewEpochEndsTheOldConversation() {
        Rig rig;
        rig.ask();
        rig.answer1();
        const auto reused = key("session-a", "e2");
        rig.bridge.open({reused});
        QCOMPARE(rig.conv().link, Conversation::Link::Ended);
        QCOMPARE(rig.state("q1"), RequestState::Cancelled);
        QCOMPARE(reasonOf(rig.answer1("c9")), QString(reason::ended));
        QCOMPARE(rig.conv(reused).link, Conversation::Link::Connected);
        QCOMPARE(rig.service.attentionCount(), 0);
        QCOMPARE(rig.results(), QStringList{QString("c1:unknown:") + reason::ended});
        rig.bridge.ended(reused);
        QCOMPARE(reasonOf(rig.service.submit(followUp(reused, "f", "x"))), QString(reason::ended));
        rig.bridge.open({reused}); // An ended conversation cannot come back.
        QCOMPARE(rig.conv(reused).link, Conversation::Link::Ended);
    }
    // A refused conversation_open is answered with its error alone: a resync would make the bridge treat it as registered.
    void aRefusedOpenGetsAnErrorAndNoResync() {
        Rig rig;
        const auto resyncs = [&](const ConversationKey &k) {
            int n = 0;
            for (const auto &f : rig.bridge.inbox)
                if (f.value("type") == "resync" && f.value("conversation").toObject().value("conversation_epoch") == k.epoch) ++n;
            return n;
        };
        const auto ended = key("session-e", "e1");
        rig.bridge.open({ended});
        QCOMPARE(resyncs(ended), 1); // Accepted: the bridge is asked for its snapshot.
        rig.bridge.ended(ended);
        rig.bridge.inbox.clear();
        rig.bridge.open({ended});    // The identity is held as ended.
        QCOMPARE(resyncs(ended), 0);
        QCOMPARE(rig.bridge.errorCodes(), QStringList{QString(reason::ended)});
        FakeBridge other(rig.service, 2, "other"); // Another provider's name in the key.
        other.connect();
        other.hello({});
        other.send({{"type", "conversation_open"}, {"conversation", key("session-f", "e1").toJson()}, {"title", "x"}, {"ready", true},
                    {"capabilities", QJsonArray{"messages"}}});
        QCOMPARE(other.errorCodes().size(), 1);
        for (const auto &f : other.inbox) QVERIFY(f.value("type") != "resync");
    }
    void endedConversationsKeepReadableHistory() {
        Rig rig;
        rig.bridge.message(rig.a, 1, "m1", "keep me");
        rig.bridge.ended(rig.a);
        QCOMPARE(rig.conv().messages.size(), 1);
        QCOMPARE(rig.conv().messages[0].text, QString("keep me"));
    }

    // ---- Messages --------------------------------------------------------------------------------

    void messagesAreOrderedDeduplicatedAndReadTracked() {
        Rig rig;
        rig.bridge.message(rig.a, 2, "m2", "second");
        rig.bridge.message(rig.a, 1, "m1", "first");
        rig.bridge.message(rig.a, 1, "m1", "first"); // Repeated delivery.
        rig.bridge.message(rig.a, 3, "m3", "mine", "user");
        QCOMPARE(rig.conv().messages.size(), 3);
        QCOMPARE(rig.conv().messages[0].messageId, QString("m1"));
        QCOMPARE(rig.conv().messages[2].messageId, QString("m3"));
        QCOMPARE(rig.conv().unreadCount(), 2); // The user's own message is read.
        const auto changes = rig.changes.size();
        rig.bridge.message(rig.a, 1, "m1", "first");
        QCOMPARE(rig.changes.size(), changes); // No notification, so no timer restarts or sound.
        rig.service.markRead(rig.a, "m1");
        QCOMPARE(rig.conv().unreadCount(), 1);
        rig.ask();
        QCOMPARE(rig.state("q1"), RequestState::Pending); // Reading never answers or clears a question.
        rig.bridge.send({{"type", "message"}, {"conversation", rig.a.toJson()},
                         {"message", QJsonObject{{"message_id", "big"}, {"sequence", 9}, {"role", "agent"}, {"text", QString(Limits::textBytes + 1, 'x')}}}});
        QCOMPARE(rig.bridge.errorCodes().last(), QString(reason::tooLarge));
        QCOMPARE(rig.conv().messages.size(), 3);
    }

    void historyFromABridgeStartsReadButNewMessagesDoNot() {
        Rig rig;
        rig.bridge.send({{"type", "snapshot"}, {"conversation", rig.a.toJson()}, {"watermark", 2}, {"first_sequence", 1}, {"requests", QJsonArray{}},
                         {"messages", QJsonArray{QJsonObject{{"message_id", "old1"}, {"sequence", 1}, {"role", "agent"}, {"text", "before"}, {"history", true}},
                                                 QJsonObject{{"message_id", "new2"}, {"sequence", 2}, {"role", "agent"}, {"text", "after"}}}}});
        QCOMPARE(rig.conv().messages.size(), 2);
        QVERIFY(rig.conv().messages[0].read);
        QVERIFY(!rig.conv().messages[1].read);
        QCOMPARE(rig.conv().unreadCount(), 1);
        Message parsed;
        QString error;
        auto bad = QJsonObject{{"message_id", "m"}, {"sequence", 1}, {"role", "agent"}, {"text", "t"}, {"history", "yes"}};
        QVERIFY(!Message::fromJson(bad, parsed, error));
    }

    // ---- Bounds ----------------------------------------------------------------------------------

    void messageHistoryIsBoundedReadFirst() {
        Rig rig;
        rig.service.budget.messages = 4;
        for (int i = 1; i <= 4; ++i) rig.bridge.message(rig.a, i, QString("m%1").arg(i), "t");
        rig.service.markRead(rig.a, "m2");
        rig.bridge.message(rig.a, 5, "m5", "t");
        QStringList ids;
        for (const auto &m : rig.conv().messages) ids << m.messageId;
        QCOMPARE(ids, (QStringList{"m1", "m3", "m4", "m5"})); // The read one went first.
        rig.bridge.message(rig.a, 6, "m6", "t");
        ids.clear();
        for (const auto &m : rig.conv().messages) ids << m.messageId;
        QCOMPARE(ids, (QStringList{"m3", "m4", "m5", "m6"})); // Then the oldest.
    }
    void unsupportedQuestionsAreRefusedWholeNeverTruncated() {
        Rig rig;
        QStringList many;
        for (int i = 0; i < Limits::options + 1; ++i) many << QString("o%1").arg(i);
        rig.bridge.request(rig.a, "big", 1, "Which?", many);
        QCOMPARE(rig.bridge.lastError().value("code").toString(), QString("unsupported_content"));
        QCOMPARE(rig.bridge.lastError().value("request_id").toString(), QString("big"));
        QVERIFY(!rig.conv().requests.contains("big"));
        QVERIFY(rig.conv().unsupportedContent);
        rig.bridge.request(rig.a, "limit", 1, "Fits", many.mid(0, Limits::options));
        QVERIFY(rig.conv().requests.contains("limit"));
    }
    void openRequestsAreCappedAndNeverEvicted() {
        Rig rig;
        rig.service.budget.pendingRequests = 2;
        rig.ask("q1");
        rig.ask("q2");
        rig.ask("q3");
        QCOMPARE(rig.bridge.errorCodes().last(), QString(reason::capacity));
        QVERIFY(rig.conv().capacityExceeded);
        QVERIFY(!rig.conv().requests.contains("q3"));
        QCOMPARE(rig.state("q1"), RequestState::Pending);
        rig.bridge.requestState(rig.a, "q1", "answered");
        rig.ask("q3"); // Room again once one resolves.
        QVERIFY(rig.conv().requests.contains("q3"));
    }
    void contentBudgetEvictsReadMessagesThenInactiveConversationsButNotProtectedOnes() {
        Rig rig;
        const auto idle = key("idle"), drafted = key("drafted"), asking = key("asking");
        rig.bridge.open({rig.a, idle, drafted, asking});
        const QString text(1000, 'x');
        rig.bridge.message(idle, 1, "i1", text);
        rig.bridge.message(drafted, 1, "d1", text);
        rig.bridge.request(asking, "q", 1, "Open question", {"a"});
        rig.service.setDraft(drafted, "", "do not lose this");
        rig.now += 10;
        rig.bridge.message(rig.a, 1, "a1", text);
        rig.bridge.message(rig.a, 2, "a2", text);
        rig.service.markRead(rig.a, "a1");
        rig.bridge.disconnect(); // Every conversation is now inactive.
        FakeBridge again(rig.service, 2, "fake", "g2");
        again.connect();
        again.hello({rig.a});
        rig.service.budget.contentBytes = rig.service.contentBytes() + 500;
        again.message(rig.a, 3, "a3", text);
        QVERIFY(!rig.conv().capacityExceeded);
        bool a1 = false;
        for (const auto &m : rig.conv().messages) a1 = a1 || m.messageId == "a1";
        QVERIFY(!a1); // The read message went first.
        again.message(rig.a, 4, "a4", text);
        again.message(rig.a, 5, "a5", text);
        QVERIFY(rig.service.find(asking)); // Open request: protected.
        QVERIFY(rig.service.find(drafted)); // Draft: protected.
        QCOMPARE(rig.service.draft(drafted, ""), QString("do not lose this"));
        QCOMPARE(rig.service.find(asking)->requests.value("q").state, RequestState::Pending);
        QVERIFY(!rig.service.find(idle)); // The only unprotected inactive conversation.
        QVERIFY(rig.service.contentBytes() <= rig.service.budget.contentBytes);
    }
    void capacityExhaustedByProtectedRecordsRejectsNewOnes() {
        Rig rig;
        rig.ask("q1");
        rig.service.budget.contentBytes = rig.service.contentBytes() + 10;
        rig.bridge.message(rig.a, 1, "m1", QString(5000, 'x'));
        QVERIFY(rig.conv().capacityExceeded);
        QVERIFY(rig.conv().messages.isEmpty());
        QCOMPARE(rig.bridge.errorCodes().last(), QString(reason::capacity));
        QCOMPARE(rig.service.submit(answer(rig.a, "c1", "q1", 1, "tests")).outcome, Outcome::Pending); // Controls still work.
    }
    void conversationCountIsBounded() {
        Rig rig;
        rig.service.budget.conversations = 2;
        const auto second = key("s2"), third = key("s3");
        rig.bridge.open({second});
        QCOMPARE(rig.service.keys().size(), 2);
        rig.bridge.open({third}); // Both are connected, so none can go.
        QVERIFY(!rig.service.find(third));
        QCOMPARE(rig.bridge.errorCodes().last(), QString(reason::capacity));
        rig.bridge.ended(second);
        rig.bridge.open({third});
        QVERIFY(rig.service.find(third)); // The ended one made room.
        QVERIFY(!rig.service.find(second));
    }
    void trackedCommandsAreBoundedButInFlightOnesAreKept() {
        Rig rig;
        rig.service.budget.commands = 2;
        rig.ask("q1");
        rig.ask("q2");
        rig.ask("q3");
        QCOMPARE(rig.service.submit(answer(rig.a, "c1", "q1", 1, "tests")).outcome, Outcome::Pending);
        QCOMPARE(rig.service.submit(answer(rig.a, "c2", "q2", 1, "tests")).outcome, Outcome::Pending);
        QCOMPARE(reasonOf(rig.service.submit(answer(rig.a, "c3", "q3", 1, "tests"))), QString(reason::capacity)); // Both in flight.
        rig.bridge.accept(rig.bridge.commands[0]);
        QCOMPARE(rig.service.submit(answer(rig.a, "c3", "q3", 1, "tests")).outcome, Outcome::Pending); // c1 forgotten.
        QVERIFY(rig.conv().commands.contains("c2"));
        QVERIFY(!rig.conv().commands.contains("c1"));
    }
    void aHugeLengthPrefixNeverAllocates() {
        Rig rig;
        rig.bridge.sendRaw(QByteArray(Limits::frameBytes + 1, ' '));
        QCOMPARE(rig.bridge.errorCodes().last(), QString("frame_too_large"));
    }
};

QTEST_GUILESS_MAIN(InteractionTests)
#include "interaction_tests.moc"

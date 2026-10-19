// End to end: the real C++ interaction service on a real private socket, the real Codex bridge (Node) on the
// other end, and a fake Codex daemon behind the bridge. This is what proves the two independent implementations
// of the wire protocol agree. The bridge's own behavior is in codex_bridge_test.mjs.
#include "fake_bridge.h"
#include "interactions/host.h"
#include "platform/local_stream/interaction_client.h"
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace pet;
using namespace pet::interactions;
using namespace pet::interactions::testing;

class InteractionCodexBridgeTests : public QObject {
    Q_OBJECT
    QTemporaryDir runtime_;
    QProcess driver_;
    QVector<QJsonObject> lines_;
    QByteArray pending_;

    void pump() {
        pending_ += driver_.readAllStandardOutput();
        int at;
        while ((at = pending_.indexOf('\n')) >= 0) {
            lines_.append(QJsonDocument::fromJson(pending_.left(at)).object());
            pending_.remove(0, at + 1);
        }
    }
    QJsonObject line(const QString &field) {
        pump();
        for (const auto &l : lines_)
            if (l.contains(field)) return l;
        return {};
    }
    bool seen(const QString &field) { return !line(field).isEmpty(); }

private slots:
    void initTestCase() {
        QVERIFY(runtime_.isValid());
        qputenv("XDG_RUNTIME_DIR", runtime_.path().toUtf8());
    }

    void theRealBridgeAndTheRealServiceAgree() {
        Service service;
        const QString endpoint = "codex-e2e";
        Host host(service, platform::createInteractionTransport(endpoint));
        QString error;
        QVERIFY2(host.start(error), qPrintable(error));

        driver_.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        driver_.start(NODE_PATH, {DRIVER_PATH, platform::interactionEndpoint(endpoint)});
        QVERIFY2(driver_.waitForStarted(5000), "node did not start");
        QTRY_VERIFY_WITH_TIMEOUT((pump(), seen("ready")), 10000);
        const auto ready = line("ready");
        const ConversationKey k{"codex", ready.value("session").toString(), ready.value("epoch").toString()};
        const auto requestId = ready.value("request").toString();

        // History and the terminal's question arrive through hello → welcome → snapshot (the bridge resumed the
        // thread to receive the question).
        QTRY_VERIFY_WITH_TIMEOUT(service.find(k) && service.find(k)->requests.contains(requestId), 10000);
        const auto *c = service.find(k);
        QCOMPARE(c->link, Conversation::Link::Connected);
        QCOMPARE(c->title, QString("api-web"));
        QVERIFY(c->has(Messages | SingleChoice | CustomAnswer | FollowUp | Reconcile));
        QVERIFY(!c->has(IdempotentCommands));
        QCOMPARE(c->messages.size(), 3);
        QCOMPARE(c->messages[0].text, QString("fix the handler"));
        QCOMPARE(c->messages[1].role, Role::Agent);
        QCOMPARE(c->messages[2].text, QString("add tests"));
        QCOMPARE(c->unreadCount(), 0); // Predates the bridge: history starts read.
        QVERIFY(!c->ready);            // The question holds the turn.
        const auto &request = c->requests.value(requestId);
        QCOMPARE(request.question, QString("Add tests before continuing?"));
        QCOMPARE(request.options.size(), 2);
        QVERIFY(request.customAllowed);
        QCOMPARE(request.state, RequestState::Pending);
        QCOMPARE(service.attentionCount(), 1);

        // Answer from the pet: the bridge responds to the daemon's request, the daemon resolves it, the service sees acceptance.
        QStringList results;
        service.subscribe([&](const Change &change) {
            if (change.kind == Change::Kind::CommandResult) results << change.result.commandId + ":" + outcomeName(change.result.outcome);
        });
        QCOMPARE(service.submit(answer(k, "cmd-answer", requestId, 1, "tests")).outcome, Outcome::Pending);
        QTRY_VERIFY_WITH_TIMEOUT(results.contains("cmd-answer:accepted"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(service.find(k)->requests.value(requestId).state, RequestState::Answered, 5000);
        QTRY_VERIFY_WITH_TIMEOUT((pump(), seen("answer")), 5000);
        const auto given = line("answer").value("answer").toObject();
        QCOMPARE(given.value("answers").toObject().value("q0").toObject().value("answers").toArray().at(0).toString(), QString("tests"));
        QCOMPARE(service.attentionCount(), 0);

        // The turn finishes: a new message and the conversation becomes ready.
        QVERIFY(driver_.write("finish\n") > 0);
        QTRY_VERIFY_WITH_TIMEOUT(service.find(k)->ready && service.find(k)->messages.size() == 4, 10000);
        QCOMPARE(service.find(k)->messages.last().text, QString("Tests added."));
        QCOMPARE(service.find(k)->unreadCount(), 1);
        QCOMPARE(service.find(k)->messages.last().sequence, qint64(4));

        // A follow-up becomes a turn, once, with a client ID derived from the command ID.
        QCOMPARE(service.submit(followUp(k, "cmd-follow", "now run them")).outcome, Outcome::Pending);
        QTRY_VERIFY_WITH_TIMEOUT(results.contains("cmd-follow:accepted"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT((pump(), seen("turnStart")), 5000);
        const auto start = line("turnStart").value("turnStart").toObject();
        QCOMPARE(start.value("input").toArray().at(0).toObject().value("text").toString(), QString("now run them"));
        QVERIFY(start.value("clientUserMessageId").toString().startsWith("apet_"));
        QVERIFY(!service.find(k)->ready); // Busy again.
        QCOMPARE(service.submit(followUp(k, "cmd-follow", "now run them")).outcome, Outcome::Accepted); // Known outcome.

        QVERIFY(driver_.write("quit\n") > 0);
        QVERIFY(driver_.waitForFinished(10000));
        QTRY_COMPARE_WITH_TIMEOUT(service.find(k)->link, Conversation::Link::Disconnected, 5000);
    }

    void cleanupTestCase() {
        if (driver_.state() != QProcess::NotRunning) { driver_.kill(); driver_.waitForFinished(2000); }
    }
};

QTEST_GUILESS_MAIN(InteractionCodexBridgeTests)
#include "interaction_codex_bridge_tests.moc"

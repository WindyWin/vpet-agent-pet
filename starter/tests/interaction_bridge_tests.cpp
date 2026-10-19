// End to end: the real C++ interaction service on a real private socket, the real OpenCode bridge (Node)
// on the other end, and a fake OpenCode API behind the bridge. This is what proves the two independent
// implementations of the wire protocol agree. The bridge's own behavior is in opencode_bridge_test.mjs.
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

class InteractionBridgeTests : public QObject {
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
        const QString endpoint = "e2e";
        Host host(service, platform::createInteractionTransport(endpoint));
        QString error;
        QVERIFY2(host.start(error), qPrintable(error));

        driver_.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        auto wireEndpoint = platform::interactionEndpoint(endpoint);
#ifdef Q_OS_WIN
        wireEndpoint.prepend(QStringLiteral("\\\\.\\pipe\\")); // Node dials a pipe by its full path, Qt by its bare name.
#endif
        driver_.start(NODE_PATH, {DRIVER_PATH, wireEndpoint});
        QVERIFY2(driver_.waitForStarted(5000), "node did not start");
        QTRY_VERIFY_WITH_TIMEOUT((pump(), seen("ready")), 10000);
        const auto ready = line("ready");
        const ConversationKey k{"opencode", ready.value("session").toString(), ready.value("epoch").toString()};
        const auto formId = ready.value("form").toString();

        // History and the open question arrive through hello → welcome → snapshot.
        QTRY_VERIFY_WITH_TIMEOUT(service.find(k) && service.find(k)->requests.contains(formId), 10000);
        const auto *c = service.find(k);
        QCOMPARE(c->link, Conversation::Link::Connected);
        QCOMPARE(c->title, QString("api-web"));
        QVERIFY(c->has(Messages | SingleChoice | CustomAnswer | FollowUp | Reconcile | IdempotentCommands));
        QCOMPARE(c->messages.size(), 2);
        QCOMPARE(c->messages[0].text, QString("fix the handler"));
        QCOMPARE(c->messages[1].role, Role::Agent);
        QCOMPARE(c->unreadCount(), 0); // Predates the bridge: history starts read.
        QVERIFY(!c->ready);            // The question holds the turn.
        const auto &request = c->requests.value(formId);
        QCOMPARE(request.question, QString("Add tests before continuing?"));
        QCOMPARE(request.options.size(), 2);
        QCOMPARE(request.state, RequestState::Pending);
        QCOMPARE(service.attentionCount(), 1);

        // Answer from the pet: the bridge replies to OpenCode, the service sees acceptance.
        QStringList results;
        service.subscribe([&](const Change &change) {
            if (change.kind == Change::Kind::CommandResult) results << change.result.commandId + ":" + outcomeName(change.result.outcome);
        });
        QCOMPARE(service.submit(answer(k, "cmd-answer", formId, 1, "tests")).outcome, Outcome::Pending);
        QTRY_VERIFY_WITH_TIMEOUT(results.contains("cmd-answer:accepted"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(service.find(k)->requests.value(formId).state, RequestState::Answered, 5000);
        QVERIFY((pump(), seen("reply")));
        const auto reply = line("reply").value("reply").toObject();
        QCOMPARE(reply.value("formID").toString(), formId);
        QCOMPARE(reply.value("answer").toObject().value("q0").toString(), QString("tests")); // Option ID, not label.
        QCOMPARE(service.attentionCount(), 0);

        // The turn finishes: a new message and the conversation becomes ready.
        QVERIFY(driver_.write("finish\n") > 0);
        QTRY_VERIFY_WITH_TIMEOUT(service.find(k)->ready && service.find(k)->messages.size() == 3, 10000);
        QCOMPARE(service.find(k)->messages.last().text, QString("Tests added."));
        QCOMPARE(service.find(k)->unreadCount(), 1);
        QCOMPARE(service.find(k)->messages.last().sequence, qint64(3));

        // A follow-up becomes a prompt, once, with an ID derived from the command ID.
        QCOMPARE(service.submit(followUp(k, "cmd-follow", "now run them")).outcome, Outcome::Pending);
        QTRY_VERIFY_WITH_TIMEOUT(results.contains("cmd-follow:accepted"), 10000);
        QVERIFY((pump(), seen("prompt")));
        const auto prompt = line("prompt").value("prompt").toObject();
        QCOMPARE(prompt.value("text").toString(), QString("now run them"));
        QCOMPARE(prompt.value("id").toString(), QString("msg_apet_07931ed63e32591180e0e616b5dcff2a54909233"));
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

QTEST_GUILESS_MAIN(InteractionBridgeTests)
#include "interaction_bridge_tests.moc"

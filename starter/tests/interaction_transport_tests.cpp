// The interaction stream: a real private local socket between the service host and a bridge client.
// Protocol semantics are covered in interaction_tests.cpp; these check what only a real stream can show
// (framing on the wire, ownership, permissions, disconnects and backpressure).
#include "fake_bridge.h"
#include "interactions/host.h"
#include "platform/local_stream/interaction_client.h"
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

using namespace pet;
using namespace pet::interactions;
using namespace pet::interactions::testing;
using pet::platform::InteractionClient;

namespace {
// A bridge on the wire: JSON frames in and out.
struct WireBridge {
    InteractionClient client;
    QVector<QJsonObject> inbox;
    bool dropped = false;
    explicit WireBridge(const QString &name) {
        client.received = [this](const QByteArray &payload) { inbox.append(QJsonDocument::fromJson(payload).object()); };
        client.disconnected = [this] { dropped = true; };
        QString error;
        QVERIFY2(client.connectTo(name, error), qPrintable(error));
    }
    void send(QJsonObject frame) {
        if (!frame.contains("version")) frame["version"] = protocolVersion;
        QVERIFY(client.send(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
    }
    bool has(const QString &type) const {
        for (const auto &f : inbox)
            if (f.value("type") == type) return true;
        return false;
    }
    QJsonObject last(const QString &type) const {
        for (int i = inbox.size() - 1; i >= 0; --i)
            if (inbox[i].value("type") == type) return inbox[i];
        return {};
    }
};
}

class InteractionTransportTests : public QObject {
    Q_OBJECT
    QTemporaryDir runtime_;
    QString name() const { return "t" + QUuid::createUuid().toString(QUuid::Id128).left(8); }
private slots:
    void initTestCase() {
        QVERIFY(runtime_.isValid());
        qputenv("XDG_RUNTIME_DIR", runtime_.path().toUtf8());
    }

    void aBridgeTalksToTheServiceOverTheSocket() {
        const auto endpoint = name();
        Service service;
        Host host(service, platform::createInteractionTransport(endpoint));
        QString error;
        QVERIFY2(host.start(error), qPrintable(error));
        WireBridge bridge(endpoint);
        const auto k = key("wire-session");
        bridge.send({{"type", "hello"}, {"provider", "fake"}, {"generation", "g1"},
                     {"conversations", QJsonArray{QJsonObject{{"conversation", k.toJson()}, {"title", "api-web"}, {"ready", true},
                                                              {"capabilities", QJsonArray{"messages", "single_choice", "follow_up"}}}}}});
        QTRY_VERIFY(bridge.has("welcome"));
        QCOMPARE(bridge.last("welcome").value("resync").toArray().size(), 1);
        bridge.send({{"type", "request"}, {"conversation", k.toJson()},
                     {"request", FakeBridge::requestJson("q1", 1, "Add tests?", {"tests", "skip"})}});
        QTRY_VERIFY(service.find(k) && service.find(k)->requests.contains("q1"));
        QCOMPARE(service.submit(answer(k, "c1", "q1", 1, "tests")).outcome, Outcome::Pending);
        QTRY_VERIFY(bridge.has("command"));
        const auto command = bridge.last("command");
        QCOMPARE(command.value("command_id").toString(), QString("c1"));
        QCOMPARE(command.value("payload").toObject().value("option_id").toString(), QString("tests"));
        bridge.send({{"type", "command_result"}, {"command_id", "c1"}, {"outcome", "accepted"}});
        QTRY_COMPARE(service.find(k)->requests.value("q1").state, RequestState::Answered);
    }

    void aMalformedFrameGetsAnErrorAndKeepsTheConnection() {
        const auto endpoint = name();
        Service service;
        Host host(service, platform::createInteractionTransport(endpoint));
        QString error;
        QVERIFY(host.start(error));
        WireBridge bridge(endpoint);
        QVERIFY(bridge.client.send("this is not json"));
        QTRY_VERIFY(bridge.has("error"));
        QCOMPARE(bridge.last("error").value("code").toString(), QString("invalid_frame"));
        QVERIFY(!bridge.dropped);
    }

    void anOversizedFrameDropsTheConnectionAndDisconnectsItsConversations() {
        const auto endpoint = name();
        Service service;
        Host host(service, platform::createInteractionTransport(endpoint));
        QString error;
        QVERIFY(host.start(error));
        WireBridge bridge(endpoint);
        const auto k = key("s");
        bridge.send({{"type", "hello"}, {"provider", "fake"}, {"generation", "g1"},
                     {"conversations", QJsonArray{QJsonObject{{"conversation", k.toJson()}, {"ready", true}, {"capabilities", QJsonArray{"messages", "follow_up"}}}}}});
        QTRY_VERIFY(bridge.has("welcome"));
        QCOMPARE(service.find(k)->link, Conversation::Link::Connected);
        QCOMPARE(service.submit(followUp(k, "f1", "go")).outcome, Outcome::Pending);
        QVERIFY(bridge.client.sendRaw(QByteArray::fromHex("00010001") + QByteArray(100, 'x'))); // 64 KiB + 1 declared.
        QTRY_VERIFY(bridge.dropped);
        QTRY_COMPARE(service.find(k)->link, Conversation::Link::Disconnected);
        QCOMPARE(service.find(k)->commands.value("f1").result.outcome, Outcome::Unknown); // In flight when the link died.
    }

    void aClosedBridgeDisconnectsAndAReconnectTakesOver() {
        const auto endpoint = name();
        Service service;
        Host host(service, platform::createInteractionTransport(endpoint));
        QString error;
        QVERIFY(host.start(error));
        const auto k = key("s");
        const QJsonObject hello{{"type", "hello"}, {"provider", "fake"}, {"generation", "g1"},
                                {"conversations", QJsonArray{QJsonObject{{"conversation", k.toJson()}, {"ready", true}}}}};
        {
            WireBridge first(endpoint);
            first.send(hello);
            QTRY_VERIFY(first.has("welcome"));
            first.client.close();
        }
        QTRY_COMPARE(service.find(k)->link, Conversation::Link::Disconnected);
        WireBridge second(endpoint);
        auto again = hello;
        again["generation"] = "g2";
        second.send(again);
        QTRY_VERIFY(second.has("welcome"));
        QCOMPARE(service.find(k)->link, Conversation::Link::Connected);
        QCOMPARE(service.find(k)->generation, QString("g2"));
    }

    void onlyOneOwnerMayServeAnEndpoint() {
        const auto endpoint = name();
        QString error;
        auto first = platform::createInteractionTransport(endpoint);
        QVERIFY2(first->start(error), qPrintable(error));
        auto second = platform::createInteractionTransport(endpoint);
        QVERIFY(!second->start(error));
        QVERIFY(error.contains("running"));
        auto other = platform::createInteractionTransport(name()); // A different endpoint is independent.
        QVERIFY2(other->start(error), qPrintable(error));
        first.reset();
        auto third = platform::createInteractionTransport(endpoint); // Released with its owner.
        QVERIFY2(third->start(error), qPrintable(error));
    }

#ifdef Q_OS_UNIX
    void theEndpointIsPrivateToTheUser() {
        const auto endpoint = name();
        QString error;
        auto transport = platform::createInteractionTransport(endpoint);
        QVERIFY2(transport->start(error), qPrintable(error));
        const auto path = platform::interactionEndpoint(endpoint);
        struct stat socket {}, directory {};
        QVERIFY(stat(QFile::encodeName(path).constData(), &socket) == 0);
        QVERIFY(S_ISSOCK(socket.st_mode));
        QCOMPARE(int(socket.st_mode & 077), 0); // No group or other access.
        QVERIFY(stat(QFile::encodeName(QFileInfo(path).absolutePath()).constData(), &directory) == 0);
        QCOMPARE(int(directory.st_mode & 077), 0);
    }
#endif

    void aPeerThatNeverReadsIsDroppedInsteadOfBufferedForever() {
        const auto endpoint = name();
        auto transport = platform::createInteractionTransport(endpoint);
        int id = 0;
        bool gone = false;
        transport->connected = [&](int c) { id = c; };
        transport->disconnected = [&](int c) { gone = gone || c == id; };
        QString error;
        QVERIFY2(transport->start(error), qPrintable(error));
        InteractionClient client; // Connects and then never reads.
        QVERIFY(client.connectTo(endpoint, error));
        QTRY_VERIFY(id != 0);
        const QByteArray payload(Limits::frameBytes, 'x');
        bool refused = false;
        for (int i = 0; i < 400 && !refused; ++i) refused = !transport->send(id, payload);
        QVERIFY(refused);
        QVERIFY(gone);
        QVERIFY(!transport->send(id, "{}")); // The connection is closed.
        QVERIFY(!transport->send(12345, "{}"));
        QVERIFY(!transport->send(id, QByteArray(Limits::frameBytes + 1, 'x'))); // Never sends an oversized frame.
    }
};

QTEST_GUILESS_MAIN(InteractionTransportTests)
#include "interaction_transport_tests.moc"

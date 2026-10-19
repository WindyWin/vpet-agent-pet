#include "quota/service.h"
#include "desktop/session_list.h"
#include "desktop/quota_panel.h"
#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkReply>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstring>

using namespace pet::quota;
namespace {
const QByteArray codexUsage = R"({"rate_limit":{"primary_window":{"used_percent":37,"limit_window_seconds":18000,"reset_after_seconds":7200},"secondary_window":{"used_percent":82,"limit_window_seconds":604800,"reset_at":1792368000}}})";
const QByteArray claudeUsage = R"({"five_hour":{"utilization":21.5,"resets_at":"2026-10-18T12:00:00Z"},"seven_day":{"utilization":64,"resets_at":"2026-10-21T12:00:00Z"},"seven_day_opus":null})";
struct Answer { int status = 200; QByteArray body, retry; };
class Reply : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, Answer answer, QObject *parent) : QNetworkReply(parent), data_(answer.body) {
        setRequest(request); setUrl(request.url()); setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, answer.status);
        setRawHeader("Retry-After", answer.retry);
        open(ReadOnly | Unbuffered);
        QTimer::singleShot(0, this, [this] { emit readyRead(); if (isFinished()) return; setFinished(true); emit finished(); });
    }
    void abort() override { if (isFinished()) return; setError(OperationCanceledError, "aborted"); setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return data_.size() - offset_ + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *out, qint64 max) override {
        auto length = qMin(max, qint64(data_.size() - offset_));
        if (!length) return -1;
        std::memcpy(out, data_.constData() + offset_, size_t(length)); offset_ += length; return length;
    }
private:
    QByteArray data_;
    qint64 offset_ = 0;
};
class Network : public QNetworkAccessManager {
public:
    QVector<QNetworkRequest> requests;
    QMap<QString, Answer> answers{{"chatgpt.com", {200, codexUsage, {}}}, {"api.anthropic.com", {200, claudeUsage, {}}}};
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        requests.append(request);
        return new Reply(request, answers.value(request.url().host()), this);
    }
};
Service::Loader credentials() {
    return [](Provider p, Service::Loaded done) { Credential c; c.token = p == Provider::Codex ? "codex-test" : "claude-test";
        c.workspace = "workspace-test"; c.account = "test@example.com"; c.problem = Problem::None; done(c); };
}
bool idle(Service &service) { return !service.reading(Provider::Codex).loading && !service.reading(Provider::Claude).loading; }
QString panelText(QWidget &widget) {
    QStringList text; for (auto *label : widget.findChildren<QLabel *>()) text << label->text(); return text.join('\n');
}
}
class QuotaTests : public QObject {
    Q_OBJECT
private slots:
    void parsesProviderWindowsWithoutInventingZeros() {
        bool ok = false;
        const auto at = QDateTime::fromString("2026-10-18T09:00:00Z", Qt::ISODate);
        const auto codex = Service::parseUsage(Provider::Codex, codexUsage, at, &ok);
        QVERIFY(ok); QCOMPARE(codex.size(), 2); QCOMPARE(codex[0].used, 37.0);
        QCOMPARE(codex[0].kind, Window::Session); QCOMPARE(codex[1].kind, Window::Week);
        QCOMPARE(codex[0].resets, at.addSecs(7200));
        const auto claude = Service::parseUsage(Provider::Claude, claudeUsage, at, &ok);
        QVERIFY(ok); QCOMPARE(claude.size(), 2); QCOMPARE(claude[0].used, 21.5);
        QCOMPARE(claude[1].resets.toUTC(), QDateTime::fromString("2026-10-21T12:00:00Z", Qt::ISODate));
        for (const auto &data : {QByteArray("{}"), QByteArray("not json"), QByteArray(R"({"five_hour":{"utilization":"0"}})")}) {
            QVERIFY(Service::parseUsage(Provider::Claude, data, at, &ok).isEmpty()); QVERIFY(!ok);
        }
        const auto clamped = Service::parseUsage(Provider::Claude, R"({"five_hour":{"utilization":105},"seven_day":{"utilization":0}})", at, &ok);
        QVERIFY(ok); QCOMPARE(clamped[0].used, 100.0); QCOMPARE(clamped[1].used, 0.0);
    }
    void readsOnlyOAuthAndUsesExactHosts() {
        const QJsonObject claims{{"exp", 1792368000}, {"https://api.openai.com/profile", QJsonObject{{"email", "me@example.com"}}}};
        const auto token = "header." + QJsonDocument(claims).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding) + ".sig";
        const auto data = QJsonDocument(QJsonObject{{"tokens", QJsonObject{{"access_token", QString::fromLatin1(token)}, {"account_id", "team"}}}}).toJson();
        const auto credential = Service::parseCredential(Provider::Codex, data);
        QCOMPARE(credential.token, token); QCOMPARE(credential.workspace, "team"); QCOMPARE(credential.account, "me@example.com");
        QVERIFY(credential.expires.isValid());
        const auto request = Service::request(Provider::Codex, credential);
        QCOMPARE(request.url(), QUrl("https://chatgpt.com/backend-api/wham/usage"));
        QCOMPARE(request.rawHeader("Authorization"), "Bearer " + token); QCOMPARE(request.rawHeader("ChatGPT-Account-Id"), "team");
        QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
        QCOMPARE(Service::parseCredential(Provider::Codex, R"({"OPENAI_API_KEY":"test"})").problem, Problem::Missing);
        const auto claude = Service::parseCredential(Provider::Claude, R"({"claudeAiOauth":{"accessToken":"test-token","expiresAt":1792368000000}})");
        QCOMPARE(claude.problem, Problem::None); QVERIFY(claude.expires.isValid());
        const auto cr = Service::request(Provider::Claude, claude);
        QCOMPARE(cr.url(), QUrl("https://api.anthropic.com/api/oauth/usage"));
        QCOMPARE(cr.rawHeader("anthropic-beta"), "oauth-2025-04-20");
    }
    void configuredHomeIsAuthoritativeAndUnmodified() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QFile file(directory.path() + "/auth.json"); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bytes = R"({"tokens":{"access_token":"test-token"}})";
        file.write(bytes); file.close();
        QProcessEnvironment env; env.insert("CODEX_HOME", directory.path());
        bool done = false;
        Service::loadLocalCredential(Provider::Codex, directory.path() + "/unused", env, this, [&](Credential c) {
            done = true; QCOMPARE(c.token, "test-token");
        });
        QVERIFY(done); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes); file.close();
    }
    void cachesDeduplicatesAndHonorsRetryAfter() {
        Network network; Service service(&network); service.loadCredential = credentials();
        auto at = QDateTime::fromString("2026-10-18T09:00:00Z", Qt::ISODate); service.now = [&] { return at; };
        service.refresh(); service.refresh(true);
        QTRY_VERIFY(idle(service)); QCOMPARE(network.requests.size(), 2);
        service.refresh(); QCOMPARE(network.requests.size(), 2);
        at = at.addSecs(31); service.refresh(true); QTRY_VERIFY(idle(service)); QCOMPARE(network.requests.size(), 4);
        network.answers["chatgpt.com"] = {429, "{}", "600"};
        at = at.addSecs(301); service.refresh(); QTRY_VERIFY(idle(service)); QCOMPARE(network.requests.size(), 6);
        QCOMPARE(service.reading(Provider::Codex).problem, Problem::RateLimited);
        QCOMPARE(service.reading(Provider::Codex).windows[0].used, 37.0);
        at = at.addSecs(301); service.refresh(true); QTRY_VERIFY(idle(service));
        QCOMPARE(network.requests.size(), 7); // Only Claude; forced refresh cannot bypass Codex's backoff.
        at = at.addSecs(300); network.answers["chatgpt.com"] = {200, codexUsage, {}};
        service.refresh(); QTRY_VERIFY(idle(service)); QCOMPARE(network.requests.size(), 9);
        QCOMPARE(service.reading(Provider::Codex).problem, Problem::None);
    }
    void failuresKeepOldReadingsAndAccountChangesClearThem() {
        Network network; Service service(&network); service.loadCredential = credentials();
        auto at = QDateTime::fromString("2026-10-18T09:00:00Z", Qt::ISODate); service.now = [&] { return at; };
        service.refresh(); QTRY_VERIFY(idle(service));
        const auto checked = service.reading(Provider::Codex).checked;
        for (const auto &answer : {Answer{200, "{}", {}}, Answer{401, "{}", {}}, Answer{302, "{}", {}}, Answer{200, QByteArray(Service::maxBytes + 1, 'x'), {}}}) {
            network.answers["chatgpt.com"] = answer; at = at.addSecs(301);
            service.refresh(); QTRY_VERIFY(idle(service));
            QCOMPARE(service.reading(Provider::Codex).checked, checked);
            QCOMPARE(service.reading(Provider::Codex).windows.size(), 2);
            QVERIFY(service.reading(Provider::Codex).problem != Problem::None);
        }
        service.loadCredential = [](Provider, Service::Loaded done) { Credential c; c.token = "different-account"; c.problem = Problem::None; done(c); };
        at = at.addSecs(301); service.refresh(); QTRY_VERIFY(idle(service));
        QVERIFY(service.reading(Provider::Codex).windows.isEmpty());
    }
    void missingAndExpiredSignInsDoNotMakeRequests() {
        Network network; Service service(&network);
        service.loadCredential = [](Provider p, Service::Loaded done) {
            Credential c;
            if (p == Provider::Claude) { c.problem = Problem::None; c.token = "expired"; c.expires = QDateTime::currentDateTimeUtc().addSecs(-1); }
            done(c);
        };
        service.refresh(); QVERIFY(idle(service)); QVERIFY(network.requests.isEmpty());
        QCOMPARE(service.reading(Provider::Codex).problem, Problem::Missing);
        QCOMPARE(service.reading(Provider::Claude).problem, Problem::Expired);
    }
    void footerAppearsBelowSessionsAndStopsPollingWhenHidden() {
        Network network; Service service(&network); service.loadCredential = credentials();
        auto at = QDateTime::fromString("2026-10-18T09:00:00Z", Qt::ISODate); service.now = [&] { return at; };
        pet::SessionList list;
        pet::SessionRow row; row.key = "session"; row.name = "agent-pet"; row.status = "Working"; row.detail = "Codex · project"; row.state = "working";
        list.present({row}); list.setQuotaService(&service); list.show();
        QTRY_VERIFY(idle(service)); QTRY_COMPARE(list.findChildren<QProgressBar *>().size(), 4);
        QCOMPARE(list.rowCount(), 1);
        const auto text = panelText(list);
        QVERIFY(text.contains("Plan limits")); QVERIFY(text.contains("Account-wide"));
        QVERIFY(text.contains("37% used")); QVERIFY(text.contains("Resets in 2h 0m"));
        auto *panel = list.findChild<pet::QuotaPanel *>(); QVERIFY(panel);
        auto *sessionButton = list.findChildren<QPushButton *>().last();
        for (auto *button : list.findChildren<QPushButton *>()) if (button->text().startsWith("agent-pet")) sessionButton = button;
        QVERIFY(panel->y() > sessionButton->geometry().bottom());
        QVERIFY(list.rect().contains(panel->geometry())); // Rebuilt readings grow the popup; they are never clipped.
        const auto screenshot = qEnvironmentVariable("AGENT_PET_QUOTA_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(list.grab().save(screenshot));
        list.hide(); at = at.addSecs(301); QTest::qWait(20); QCOMPARE(network.requests.size(), 2);
        list.show(); QTRY_VERIFY(idle(service)); QCOMPARE(network.requests.size(), 4);
        QSignalSpy focused(&list, &pet::SessionList::focusRequested);
        list.activateRow(0); QCOMPARE(focused.size(), 1); QVERIFY(!list.isVisible());
    }
};
QTEST_MAIN(QuotaTests)
#include "quota_tests.moc"

#include "service.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QSysInfo>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace pet::quota {
namespace {
QJsonObject object(const QByteArray &data) { return QJsonDocument::fromJson(data).object(); }
QJsonObject claims(const QByteArray &token) {
    return object(QByteArray::fromBase64(token.split('.').value(1), QByteArray::Base64UrlEncoding));
}
QByteArray readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > Service::maxBytes) return {};
    return file.read(Service::maxBytes);
}
QDateTime seconds(double value) {
    return std::isfinite(value) && value > 0 && value < 1e11 ? QDateTime::fromMSecsSinceEpoch(qint64(value * 1000), Qt::UTC) : QDateTime();
}
QDateTime iso(const QJsonValue &value) {
    if (value.isDouble()) return seconds(value.toDouble());
    return QDateTime::fromString(value.toString(), Qt::ISODate);
}
}
Credential Service::parseCredential(Provider provider, const QByteArray &data) {
    Credential result;
    const auto root = object(data);
    if (provider == Provider::Codex) {
        const auto tokens = root["tokens"].toObject();
        result.token = tokens["access_token"].toString().toUtf8();
        const auto jwt = claims(result.token);
        const auto auth = jwt["https://api.openai.com/auth"].toObject();
        result.workspace = tokens["account_id"].toString(auth["chatgpt_account_id"].toString()).toUtf8();
        result.account = jwt["https://api.openai.com/profile"].toObject()["email"].toString();
        if (result.account.isEmpty()) result.account = claims(tokens["id_token"].toString().toUtf8())["email"].toString();
        result.expires = seconds(jwt["exp"].toDouble());
    } else {
        const auto oauth = root["claudeAiOauth"].toObject();
        result.token = oauth["accessToken"].toString().toUtf8();
        result.expires = seconds(oauth["expiresAt"].toDouble() / 1000);
    }
    result.problem = result.token.isEmpty() ? Problem::Missing : Problem::None;
    return result;
}
void Service::loadLocalCredential(Provider provider, const QString &home, const QProcessEnvironment &env,
                                  QObject *owner, Loaded done) {
    Credential file;
    QString service, account;
    if (provider == Provider::Codex) {
        const auto override = env.value("CODEX_HOME");
        const QStringList homes = override.isEmpty() ? QStringList{home + "/.config/codex", home + "/.codex"} : QStringList{override};
        for (const auto &dir : homes) {
            file = parseCredential(provider, readFile(dir + "/auth.json"));
            if (!file.token.isEmpty()) break;
        }
        service = "Codex Auth";
    } else {
        const auto override = env.value("CLAUDE_CONFIG_DIR");
        const auto dir = override.isEmpty() ? home + "/.claude" : override;
        file = parseCredential(provider, readFile(dir + "/.credentials.json"));
        const auto config = object(readFile(override.isEmpty() ? home + "/.claude.json" : dir + "/.claude.json"));
        account = config["oauthAccount"].toObject()["emailAddress"].toString();
        file.account = account;
        service = "Claude Code-credentials";
        if (!override.isEmpty()) service += "-" + QString::fromLatin1(QCryptographicHash::hash(dir.toUtf8(), QCryptographicHash::Sha256).toHex().left(8));
    }
    // The macOS CLI can hold a newer token in its Keychain than in its file. Read asynchronously;
    // never invoke a shell, request interactive access, or change either credential store.
    if (QSysInfo::kernelType() != "darwin" || (provider == Provider::Codex && !file.token.isEmpty())) { done(file); return; }
    auto *process = new QProcess(owner);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    auto completed = std::make_shared<bool>(false);
    auto finish = [process, completed, provider, file, account, done](bool success) mutable {
        if (std::exchange(*completed, true)) return;
        Credential item = success ? parseCredential(provider, process->readAllStandardOutput()) : Credential{};
        if (!account.isEmpty()) item.account = account;
        if (item.token.isEmpty() || (file.expires.isValid() && item.expires.isValid() && file.expires > item.expires)) item = file;
        if (!success && item.token.isEmpty()) item.problem = Problem::Locked;
        done(item);
        process->deleteLater();
    };
    QObject::connect(process, &QProcess::readyReadStandardOutput, process, [process] {
        if (process->bytesAvailable() > maxBytes) process->kill();
    });
    QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), owner,
                     [finish](int code, QProcess::ExitStatus status) mutable { finish(code == 0 && status == QProcess::NormalExit); });
    QObject::connect(process, &QProcess::errorOccurred, owner, [finish](QProcess::ProcessError error) mutable {
        if (error == QProcess::FailedToStart) finish(false);
    });
    QTimer::singleShot(5000, process, [process] { if (process->state() != QProcess::NotRunning) process->kill(); });
    process->start("/usr/bin/security", {"find-generic-password", "-s", service, "-w"});
}
QNetworkRequest Service::request(Provider provider, const Credential &credential) {
    QNetworkRequest result(QUrl(provider == Provider::Codex ? "https://chatgpt.com/backend-api/wham/usage" : "https://api.anthropic.com/api/oauth/usage"));
    result.setRawHeader("Authorization", "Bearer " + credential.token);
    result.setRawHeader("Accept", "application/json");
    result.setRawHeader("User-Agent", "agent-pet");
    if (provider == Provider::Codex && !credential.workspace.isEmpty()) result.setRawHeader("ChatGPT-Account-Id", credential.workspace);
    if (provider == Provider::Claude) result.setRawHeader("anthropic-beta", "oauth-2025-04-20");
    // These bearer tokens must never follow redirects to another endpoint.
    result.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    result.setTransferTimeout(15000);
    return result;
}
QVector<Window> Service::parseUsage(Provider provider, const QByteArray &data, const QDateTime &now, bool *ok) {
    QVector<Window> result;
    const auto root = object(data);
    auto add = [&](const QJsonObject &source, const char *key, Window::Kind kind, const QString &scope, const QDateTime &reset) {
        const auto used = source[key];
        if (!used.isDouble() || !std::isfinite(used.toDouble())) return;
        result.append({kind, scope, std::clamp(used.toDouble(), 0.0, 100.0), reset});
    };
    if (provider == Provider::Codex) {
        const auto limits = root["rate_limit"].toObject();
        for (const auto *key : {"primary_window", "secondary_window"}) {
            const auto source = limits[key].toObject();
            const auto duration = source["limit_window_seconds"].toDouble();
            const auto kind = duration <= 6 * 3600 ? Window::Session : duration <= 36 * 3600 ? Window::Day : duration <= 8 * 86400 ? Window::Week : Window::Month;
            auto reset = iso(source["reset_at"]);
            if (!reset.isValid() && source["reset_after_seconds"].isDouble()) {
                const auto after = source["reset_after_seconds"].toDouble();
                if (after >= 0 && after < 1e11) reset = now.addSecs(qint64(after));
            }
            add(source, "used_percent", kind, {}, reset);
        }
    } else {
        for (const auto *key : {"five_hour", "seven_day", "seven_day_opus", "seven_day_sonnet"}) {
            const auto source = root[key].toObject();
            const QString scope = QByteArray(key).endsWith("opus") ? "Opus" : QByteArray(key).endsWith("sonnet") ? "Sonnet" : "";
            add(source, "utilization", QByteArray(key) == "five_hour" ? Window::Session : Window::Week, scope, iso(source["resets_at"]));
        }
    }
    // A malformed/changed API response must not replace the last reading with zero usage.
    *ok = !result.isEmpty();
    return result;
}
Service::Service(QNetworkAccessManager *network, QObject *parent) : QObject(parent), network_(network ? network : &ownedNetwork_) {
    for (auto provider : {Provider::Codex, Provider::Claude}) entries_[provider].reading.provider = provider;
    loadCredential = [this](Provider provider, Loaded done) {
        loadLocalCredential(provider, QDir::homePath(), QProcessEnvironment::systemEnvironment(), this, std::move(done));
    };
}
Service::~Service() {
    // An injected manager may outlive us. Disconnect before abort (which can finish synchronously).
    for (auto *reply : network_->findChildren<QNetworkReply *>()) {
        if (reply->property("agentPetQuotaOwner").value<QObject *>() != this) continue;
        reply->disconnect(this); reply->abort(); reply->deleteLater();
    }
}
Reading Service::reading(Provider provider) const { return entries_.value(provider).reading; }
void Service::refresh(bool force) { for (auto provider : {Provider::Codex, Provider::Claude}) refresh(provider, force); }
void Service::refresh(Provider provider, bool force) {
    auto &entry = entries_[provider];
    const auto at = now().toMSecsSinceEpoch();
    if (entry.reading.loading || at < entry.backoff || (entry.attemptedOnce && at < (force ? entry.attempted + minRefreshMs : entry.next))) return;
    entry.attemptedOnce = true; entry.attempted = at; entry.reading.loading = true;
    emit changed();
    QPointer<Service> guard(this);
    loadCredential(provider, [guard, provider](Credential credential) {
        if (!guard) return;
        auto *self = guard.data();
        auto &entry = self->entries_[provider];
        // A different token cannot inherit another account's last numbers. Hashes stay in memory.
        const auto identity = QCryptographicHash::hash(credential.token + '\0' + credential.workspace, QCryptographicHash::Sha256);
        if (entry.identity != identity) { entry.reading.windows.clear(); entry.reading.checked = {}; }
        entry.identity = identity; entry.reading.account = credential.account;
        if (credential.problem != Problem::None || (credential.expires.isValid() && credential.expires <= self->now())) {
            entry.reading.problem = credential.problem == Problem::None ? Problem::Expired : credential.problem;
            entry.reading.loading = false;
            entry.next = self->now().toMSecsSinceEpoch() + retryMs;
            emit self->changed(); return;
        }
        auto *reply = self->network_->get(request(provider, credential));
        reply->setProperty("agentPetQuotaOwner", QVariant::fromValue<QObject *>(self));
        reply->setReadBufferSize(64 * 1024);
        auto body = std::make_shared<QByteArray>();
        QObject::connect(reply, &QNetworkReply::readyRead, self, [reply, body] {
            body->append(reply->readAll());
            if (body->size() > maxBytes) reply->abort();
        });
        QTimer::singleShot(15000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
        QObject::connect(reply, &QNetworkReply::finished, self, [self, provider, reply, body] {
            auto &entry = self->entries_[provider];
            const auto now = self->now();
            body->append(reply->readAll());
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            bool ok = false;
            const auto windows = body->size() <= maxBytes && status == 200 && reply->error() == QNetworkReply::NoError
                ? parseUsage(provider, *body, now, &ok) : QVector<Window>();
            entry.reading.problem = ok ? Problem::None : status == 401 || status == 403 ? Problem::Expired : status == 429 ? Problem::RateLimited : Problem::Failed;
            if (ok) { entry.reading.windows = windows; entry.reading.checked = now; }
            entry.reading.loading = false;
            entry.next = now.toMSecsSinceEpoch() + (ok ? freshMs : retryMs);
            if (status == 429) {
                const auto retry = reply->rawHeader("Retry-After");
                bool numeric = false;
                const double seconds = retry.toDouble(&numeric);
                const auto date = QDateTime::fromString(QString::fromLatin1(retry), Qt::RFC2822Date);
                const auto wait = numeric && std::isfinite(seconds) && seconds >= 0 && seconds < 1e11 ? qint64(seconds * 1000)
                    : date.isValid() ? now.msecsTo(date) : freshMs;
                entry.backoff = now.toMSecsSinceEpoch() + std::max(retryMs, wait);
                entry.next = entry.backoff;
            }
            reply->deleteLater(); emit self->changed();
        });
    });
}
}

#pragma once
#include <QDateTime>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QProcessEnvironment>
#include <functional>

namespace pet::quota {
enum class Provider { Codex, Claude };
enum class Problem { None, Missing, Expired, Locked, RateLimited, Failed };
struct Credential {
    QByteArray token, workspace;
    QString account;
    QDateTime expires;
    Problem problem = Problem::Missing;
};
struct Window {
    enum Kind { Session, Day, Week, Month } kind = Session;
    QString scope;
    double used = 0;
    QDateTime resets;
};
struct Reading {
    Provider provider = Provider::Codex;
    QString account;
    QVector<Window> windows;
    Problem problem = Problem::Missing;
    QDateTime checked;
    bool loading = false;
};
// Independent of sessions: these are subscription-wide limits, never a session's token count.
// Credentials are read only; the CLI alone renews them. No tokens or usage are persisted.
class Service : public QObject {
    Q_OBJECT
public:
    using Loaded = std::function<void(Credential)>;
    using Loader = std::function<void(Provider, Loaded)>;
    explicit Service(QNetworkAccessManager *network = nullptr, QObject *parent = nullptr);
    ~Service() override;
    static constexpr qint64 freshMs = 5 * 60 * 1000, minRefreshMs = 30000, retryMs = 60000;
    static constexpr qint64 maxBytes = 1024 * 1024;
    Reading reading(Provider provider) const;
    void refresh(bool force = false);
    // Test seams: a fake network, credential loader and clock keep tests off real accounts.
    Loader loadCredential;
    std::function<QDateTime()> now = [] { return QDateTime::currentDateTimeUtc(); };
    static Credential parseCredential(Provider provider, const QByteArray &data);
    static QVector<Window> parseUsage(Provider provider, const QByteArray &data, const QDateTime &now, bool *ok);
    static QNetworkRequest request(Provider provider, const Credential &credential);
    static void loadLocalCredential(Provider provider, const QString &home, const QProcessEnvironment &env,
                                    QObject *owner, Loaded done);
signals:
    void changed();
private:
    struct Entry {
        Reading reading;
        QByteArray identity;
        qint64 attempted = 0, next = 0, backoff = 0;
        bool attemptedOnce = false;
    };
    void refresh(Provider provider, bool force);
    QNetworkAccessManager ownedNetwork_;
    QNetworkAccessManager *network_;
    QMap<Provider, Entry> entries_;
};
}
